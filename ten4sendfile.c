/* ten4sendfile:  Implements sendfile(2), as missing from Mac OS 10.4.        *
 *                                                                            *
 * Sendfile() appears in the Mac OS 10.4 system headers, but was not actually *
 * implemented.  (No manpage for it was provided, either).  This tiny library *
 * rectifies the former oversight.                                            */

/* Derived from the Leopard manpage:                                          *
 *                                                                            *
 *    sendfile(fd, s, offset, &len, hdtr, flags);                             *
 *                                                                            *
 * The sendfile() function copies data from a regular file on descriptor {fd} *
 * to a stream socket {s}.  Descriptors are {int}s.                           *
 *                                                                            *
 * {offset} gives the point in the file from which to begin; if it is greater *
 *     than the file length, sendfile() returns successfully - reporting zero *
 *     octets sent (note this implies no header or trailer data shall be sent *
 *     either).  {offset} is of type {off_t}, a 64-bit signed integer.        *
 * {len} gives the count of file octets to send (if zero, all of them through *
 *     end-of-file), & returns the total count of octets actually sent.  This *
 *     is an {off_t} passed by reference.                                     *
 * {hdtr}, when present, points to a structure describing two variable-length *
 *     arrays of {struct iovec} (see writev(2) and uio.h).  These arrays hold *
 *     header and/or trailer data meant to bookend the file data.  The Mac OS *
 *     documentation does not explain this, but real-world examples show that *
 *     {hdtr} data should count towards the number of octets transmitted.     *
 * {flags} is a reserved {int}.                                               *
 *                                                                            *
 * sendfile() returns 0 on success, or -1 (with {errno} set appropriately) on *
 * failure.                                                                   *
 *                                                                            *
 * Upon failure, {errno} may equal any of the following values, for the given *
 * reasons:                                                                   *
 * EAGAIN   {s} is marked for nonblocking I/O, and sendfile() was pre-empted. *
 *          {len} returns the number of octets actually sent.                 *
 * EBADF    {fd} is not a valid file descriptor, or {s} is not a valid socket *
 *          descriptor.                                                       *
 * EFAULT   {len} is non-valid, or {hdtr} (or something it points to) is non- *
 *          valid.                                                            *
 * EINTR    sendfile() was interupted by signal.  {len} returns the number of *
 *          octets actually sent (potentially zero).                          *
 * EINVAL   {offset} is negative, or {len} is NULL, or {flags} is nonzero.    *
 * EIO      An error occurred while reading from {fd}.                        *
 * ENOTCONN {s} is not connected.                                             *
 * ENOTSOCK {s} does not represent a stream-oriented socket -- or it does not *
 *          represent a socket at all.                                        *
 * ENOTSUP  {fd} does not represent a regular file.                           *
 * EOPNOTSUPP {fd}'s filesystem does not support sendfile().                  *
 * EPIPE    The connection to {s} was closed from the other end.              */

#include <sys/errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUILDING_TEN4SENDFILE 1
#include "ten4sendfile.h"

typedef struct stat     Stat;
typedef struct timespec Timespec;

const Timespec a_third     = {0, 16666667};  /* 1/60 second = one "third".    */
const uint32_t MAX_RETRIES = 20;


/* check_iovv():                                                              *
 * For simultaneously sanity-checking and getting the total size of data in a *
 * variable-length {IOVec} array.  When the array, or any one of its members' *
 * base pointers, is NULL it sets {errno} to EFAULT and returns -1.  When the *
 * array size or the block size of any member is not positive it sets {errno} *
 * to EINVAL and returns -1.  In all other cases, it returns the total number *
 * of octets delineated by the array.                                         */
ssize_t
check_iovv(   IOVec *vary,  /* A variable-length array of {IOVec}.            */
           uint32_t  n_el   /* The number of elements in the array.           */
          )
{  ssize_t sum = 0;
  uint32_t i   = 0;
  if (vary == NULL) { errno = EFAULT;  return -1; }
  if (n_el == 0)    { errno = EINVAL;  return -1; }
  for (; i < n_el; i++)
  { if (vary[i].iov_base == NULL) { errno = EFAULT;  return -1; }
    if (vary[i].iov_len  <= 0)    { errno = EINVAL;  return -1; }
    sum += vary[i].iov_len;
  }
  if (sum == 0) { errno = EINVAL;  return -1; }
  return sum;
} /* end of check_iovv() */


int
spool_iovv(int   sock,  /* A streaming socket descriptor.                     */
         IOVec **vary,  /* A variable-length array of {IOVec}                 */
      uint32_t  *n_el,  /* The number of elements in the array.               */
         off_t  *lngth  /* 64-bit {int}.  Returns the number octets written.  */
          )
{   IOVec *v_bup  = *vary;              /* "vector backup"                    */
    IOVec  el_bup = *vary[0];           /* "element backup"                   */
  ssize_t  rslt   = 0,                  /* "result"                           */
           v_subt = 0,                  /* running "vector subtotal"          */
           v_tot  = check_iovv(*vary, *n_el); /* "vector total"               */
      int  v_idx  = 0;                  /* "vector index"                     */
  if (v_tot < 0)                        /* check_iovv() already set {errno}   */
  { lngth = 0;
    return -1;
  }
  while (v_subt < v_tot)
  { rslt = writev(sock, *vary, *n_el);
    if (rslt < 0)
    /* The possible errors (given we've already done validation) are:         *
     * EAGAIN  The write to {sock} would have blocked.  No data was written.  *
     * EBADF   {sock} is not a valid descriptor.  OK as is.                   *
     * EDESTADDRREQ  {sock}'s lost its destination.  Map to ENOTCONN.         *
     * EDQUOT  Only for disk writes.  Should never happen; map to ENOTSOCK.   *
     * EFAULT  {hdtr->headers} or something within it is invalid.  OK as is.  *
     * EFBIG   Only if {sock} was file.  Should never happen; map to ENOTSOCK.*
     * EINTR   A signal pre-empted writev.  OK as is.                         *
     * EINVAL  {hdtr->hdr_cnt} > sys max or {v_subt} overflowed.  OK as is.   *
     * EIO     There was a problem writing to {sock}.  OK as is.              *
     * ENOBUFS Ran out of buffers writing to {sock}.  Map to EIO.             *
     * ENOSPC  Only for disk writes.  Should never happen; map to ENOTSOCK.   *
     * EPIPE   {sock} hasn't got a peer streaming socket.  Map to ENOTCONN.   */
    { switch (errno)
      { case EINTR:  /*  4 */  case EIO:    /*  5 */  case EBADF:  /*  9 */
        case EFAULT: /* 14 */  case EINVAL: /* 22 */  case EAGAIN: /* 35 */
        /* We may return these same values, and for these same reasons.       */
          break;
        case EFBIG:  /* 27 */  case ENOSPC: /* 28 */  case EDQUOT: /* 69 */
        /* Only possible if writing to a disk, not a socket.                  */
          errno = ENOTSOCK;  break;
        case EPIPE:  /* 32 */  case EDESTADDRREQ: /* 39 */
        /* Only possible if the socket was disconnected.                      */
          errno = ENOTCONN;  break;
        default:  /* including ENOBUFS (55) */
          errno = EIO;  break;
      } /* end switch statement */
      *lngth += v_subt;  /* Record the progress thus far.                     */
      /* Put the vector data back the way we got it, just in case:            */
      if (*vary != v_bup) *vary = v_bup;
      if (((*vary)[v_idx].iov_base != el_bup.iov_base)
          && ((*vary)[v_idx].iov_len != el_bup.iov_len)) *vary[v_idx] = el_bup;
      return -1;
    } /* end error handling after writev()                                    */
    v_subt += rslt;
    if (rslt && (v_subt != v_tot))
    /* We didn't get it all, presumably due to being pre-empted / interrupted *
     * in some manner.  Adjust the relevant IOVec and the main vector pointer *
     * before we loop to try again.                                           */
    { /* {rslt} will keep our running total of preceding elements' sizes.     */
        size_t d = 0;  /* Difference between running subtotal sent & {rslt}.  */
      uint32_t i = 0;  /* Which element we're looking at.                     */
      /* Restore the vector data if we edited it:                             */
      if (*vary != v_bup) *vary = v_bup;
      if (((*vary)[v_idx].iov_base != el_bup.iov_base)
          && ((*vary)[v_idx].iov_len != el_bup.iov_len)) *vary[v_idx] = el_bup;
      for (rslt = 0; (rslt < v_subt) && (i < *n_el);)
      { d = v_subt - rslt;
        if ((*vary)[i].iov_len > d) /* We have found our target block.        */
        { v_idx = i;                /* Record which block that is.            */
          rslt += d;                /* Bring {rslt} equal with {v_subt}.      */
          el_bup = *vary[i];        /* Back up the block's state.             */
          (*vary)[i].iov_len -= d;  /* Record the length of unmoved data.     */
          (*vary)[i].iov_base += d; /* Point to start of unmoved data.  (GNU) */
          if (i > 0)                /* Have we gone past the first block?     */
          { v_bup = *vary;          /* Back up the master pointer.            */
            *vary += i;             /* Magical master-pointer arithmetic.     */
          }
        } else rslt += (*vary)[i++].iov_len;  /* Advance both {rslt} and {i}. */
      } /* end of "crawling the vector" for loop                              */
    } /* end of "we didn't get it all" block                                  */
  } /* end of "streaming the vector data" while loop                          */
  *lngth += v_subt;  /* Record that we streamed this much data.               */
  /* Put the vector data back the way we got it, just in case:                */
  if (*vary != v_bup) *vary = v_bup;
  if (((*vary)[v_idx].iov_base != el_bup.iov_base)
      && ((*vary)[v_idx].iov_len != el_bup.iov_len)) *vary[v_idx] = el_bup;
  return 0;
} /* end of spool_iovv() */


/* stubborn_send():                                                           *
 * Call send() until the whole of what was to be sent actually has been.      *
 * Returns 0 on success, and -1 (with errno set appropriately) otherwise.     */
int
stubborn_send(  char *bufr,  /* Data to send.                                 */
              size_t *b_sz,  /* In:  Number of octets to send.                *
                              * Out:  # octets sent (on error will be fewer). */
                 int  sock   /* Descriptor of the socket to send to.          */
             )
{     char *buf_index  = bufr;   /* Read pointer into the input buffer.       */
    size_t  cumulative = 0;      /* How much has been moved overall.          */
    size_t  to_be_sent = *b_sz;  /* How much to try to send per call.         */
   ssize_t  sent_count;          /* How much actually got sent per call.      */
  uint32_t  retries    = 0;      /* How many times we have retried sending.   */
  /* Sanity checks:                                                           */
  if (bufr == NULL) { errno = EINVAL; return -1; }
  if (to_be_sent == 0) return 0;

  do
  { sent_count = send(sock, buf_index, to_be_sent, 0);
    if (sent_count < 0)
    { if (errno == EACCES || errno == EHOSTUNREACH)
      { /* Per the specs, we can't validly return either of those.            */
        errno = ENOTCONN;
        *b_sz = cumulative;
        return -1;
      } else if (errno == EAGAIN || errno == ENOBUFS)
      { /* These errors are usually transient.  Retry.                        */
        if (retries++ < MAX_RETRIES)
        { if (nanosleep(&a_third, NULL) && errno == EINTR)
          { *b_sz = cumulative;
            return -1;
          }
          continue;
        } else  /* All those retries still weren't enough.  Give up.          */
        { errno = EAGAIN;
          *b_sz = cumulative;
          return -1;
        }
      } else if (errno == EMSGSIZE)        /* Sent too much at once.          */
      { to_be_sent = to_be_sent * 3 >> 2;  /* Try a value 3/4 as big.         */
        continue;
      } else  /* Other errors are fatal, but do have safe errno values.       */
      { *b_sz = cumulative;
        return -1;
    } }
    /* If we got this far, send() didn't error out on us.  Yay!               */
    cumulative += sent_count;
    if (cumulative < *b_sz)
    { buf_index = &(bufr[cumulative]);
      /* Don't try to send more than we have available!                       */
      if ((*b_sz - cumulative) < to_be_sent) to_be_sent = *b_sz - cumulative;
    }
  } while (cumulative < *b_sz);
  *b_sz = cumulative;
  return 0;
} /* end of stubborn_send() */


int
sendfile (int  fd,      /* Descriptor for the file to send.                   */
          int  sd,      /* Descriptor for the socket to send to.              */
        off_t  offset,  /* Index of the first file octet to send.             */
        off_t *len,     /* In:  Number of octets to read from the file.       *
                         * Out:  Total number of octets written (headers,     *
                                 file, and trailers combined).                */
      Sf_HdTr *hdtr,    /* Optional header and/or trailer data.               */
          int  flags    /* Reserved.  Return an error if nonzero.             */
         )
{ const uint32_t Buf_Sz = 1400;  /* Buffer size that should fit a net packet. */
  const uint32_t True   = 1;
  const uint32_t False  = 0;

       char buffer[Buf_Sz];
    ssize_t buf_ptr    = 0;      /* {long}.  Index (data-end + 1) in buffer.  */
     size_t positive_value;      /* For sizes that are definitely unsigned.   */
      off_t infile_ptr;          /* {uint64_t}.  Input file's file pointer.   */
      off_t cumulative = 0;      /* {uint64_t}.  Net number of octets sent.   */
   uint32_t done_flag  = False,  /* For loop control.                         */
            temp;                /* For one-off uses.                         */
  socklen_t s_len;               /* int-sized; for one-off uses.              */
        int result;              /* For system calls.                         */
       Stat stats;               /* For the stat() calls.                     */
  /* Sanity-check the arguments.                                              */
  if (len == NULL) { errno = EINVAL;  return -1; }
  *len = 0;
  if (offset < 0 || flags != 0) { errno = EINVAL;  return -1; }
  /* Sanity-check the file descriptor.                                        */
  if (fstat(fd, &stats)) return -1;  /* All 3 possible errnos are OK.         */
  if ((stats.st_mode & S_IFMT) != S_IFREG) { errno = ENOTSUP;  return -1; }
  if (offset > stats.st_size) return 0;
                         /* Finished without having to do anything!  Awesome. */
  /* Sanity-check the socket descriptor, insofar as is practical.             */
  if (fstat(sd, &stats)) return -1;  /* All 3 possible errnos are OK.         */
  if ((stats.st_mode & S_IFMT) != S_IFSOCK) { errno = ENOTSOCK;  return -1; }
  s_len = sizeof(temp);
  if (getsockopt(sd, SOL_SOCKET, SO_TYPE, &temp, &s_len))
  /* The possible errors are:                                                 *
   * EBADF   {sd} is not a valid descriptor.  OK as is.                       *
   * EDOM    Some parameter is outside its acceptable range.  Map to EINVAL.  *
   * EFAULT  temp or s_len is outside our address space.  OK as is.           *
   * ENOPROTOOPT  Inapplicable and should never be.  Map to EINVAL.           *
   * ENOTSOCK     {sd} is not a socket.  OK as is.                            */
  { if ((errno == EDOM /*33*/) || (errno == ENOPROTOOPT /*42*/)) errno = EINVAL;
    return -1;
  }
  if (temp != SOCK_STREAM) { errno = ENOTSOCK;  return -1; }
  /* Seek file to correct pos'n.  Do this now so can die early if it fails.   */
  infile_ptr = lseek(fd, offset, SEEK_SET);
  /* If this errored, infile_ptr (= -1) will never equal offset (> 0).        */
  if (infile_ptr != offset) { errno = EIO;  return -1; }

  /* Spool any headers to the socket:                                         */
  if (hdtr != NULL && hdtr->headers != NULL && hdtr->hdr_cnt > 0)
    temp = (uint32_t)(hdtr->hdr_cnt);             /* This is UNSIGNED dammit. */
    if (spool_iovv(sd, &(hdtr->headers), &temp, len))
      return -1;

  /* Spool the file via the buffer to the socket:                             */
  while (done_flag == False)
  { temp = 0;
Buf_Fill:
    buf_ptr = read(fd, &buffer, Buf_Sz);
    if (buf_ptr < 0)
    { if (errno == EINTR || errno == EAGAIN)            /* Usually temporary. */
      { if (temp++ < MAX_RETRIES) goto Buf_Fill;        /* Try again.         */
        else { *len += cumulative;  return -1; }        /* Give up.           */
      }
      else
      { if (errno == EINVAL) errno = EIO;               /* Can't EINVAL here. */
        *len += cumulative;  return -1;
      }
    } else if (buf_ptr > 0)  /* Got data to send.                             */
    { positive_value = (size_t)buf_ptr;
      result         = stubborn_send(buffer, &positive_value, sd);
      cumulative    += buf_ptr;
      if (result == -1) { *len += cumulative;  return -1; } /* All errors OK. */
    } else done_flag = True;                            /* Zero, have hit EOF.*/
  }

  /* Spool any trailers to the socket:                                        */
  if (hdtr != NULL && hdtr->trailers != NULL && hdtr->trl_cnt > 0)
    temp = (uint32_t)(hdtr->trl_cnt);             /* This is UNSIGNED dammit. */
    if (spool_iovv(sd, &(hdtr->trailers), &temp, len)) return -1;

  return 0;
} /* end of sendfile() */
