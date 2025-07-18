/* ten4sendfile:  Implements sendfile(), as missing from Mac OS 10.4.         *
 *                                                                            *
 * Sendfile() appears in the Mac OS 10.4 system headers, but an actual imple- *
 * mentation was omitted (as was the manpage).  This little library rectifies *
 * the former oversight.                                                      */

/* Derived from the Leopard manpage:                                          *
 *                                                                            *
 *    sendfile(fd, s, offset, &len, hdtr, flags);                             *
 *                                                                            *
 * The sendfile() function copies data from a regular file on descriptor {fd} *
 * to a stream socket {s}.  Descriptors are {int}s.                           *
 *                                                                            *
 * {offset} gives the point in the file from which to begin; if it is greater *
 * than the file length, sendfile() returns successfully, reporting no octets *
 * transmitted (note this implies no header/trailer data shall be transmitted *
 * either).  {offset} is of type {off_t}, a 64-bit signed integer.            *
 *                                                                            *
 * {len} gives the count of file octets to send (if zero, all of them through *
 * end-of-file), and returns the total number of octets actually sent.  {len} *
 * is an {off_t} passed by reference.                                         *
 *                                                                            *
 * {hdtr}, when present, points to a structure describing two variable-length *
 * arrays of {struct iovec} (see writev(2) and uio.h), which hold header and/ *
 * or trailer data meant to bookend the file data.  (Real-world examples show *
 * {hdtr} data counts toward the total number of octets transmitted.)         *
 *                                                                            *
 * {flags} is a reserved {int}.                                               *
 *                                                                            *
 * sendfile() returns 0 on success, or -1 (with {errno} set appropriately) on *
 * failure.                                                                   *
 *                                                                            *
 * Upon failure, {errno} may equal any of the following values, for the given *
 * reasons:                                                                   *
 *                                                                            *
 * EAGAIN   {s} is marked for nonblocking I/O, and sendfile() was pre-empted. *
 *          {len} returns the number of octets actually sent.                 *
 *                                                                            *
 * EBADF    {fd} is not a valid file descriptor, or {s} is not a valid socket *
 *          descriptor.                                                       *
 *                                                                            *
 * EFAULT   {len} is non-valid, or {hdtr} (or something it points to) is non- *
 *          valid.                                                            *
 *                                                                            *
 * EINTR    sendfile() was interupted by signal.  {len} returns the number of *
 *          octets actually sent (potentially zero).                          *
 *                                                                            *
 * EINVAL   {offset} is negative, or {len} is NULL, or {flags} is nonzero.    *
 *                                                                            *
 * EIO      An error occurred while reading from {fd}.                        *
 *                                                                            *
 * ENOTCONN                                                                   *
 *          {s} is not connected.                                             *
 *                                                                            *
 * ENOTSOCK                                                                   *
 *          {s} does not represent a stream-oriented socket -- or it does not *
 *          represent a socket at all.                                        *
 *                                                                            *
 * ENOTSUP  {fd} does not represent a regular file.                           *
 *                                                                            *
 * EOPNOTSUPP                                                                 *
 *          {fd}'s filesystem does not support sendfile().                    *
 *                                                                            *
 * EPIPE    The connection to {s} was closed from the other end.              */

#include <sys/errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "ten4sendfile.h"

typedef struct stat     Stat;
typedef struct timespec Timespec;

const Timespec a_third = {0, 16666667};  /* 1/60 second = one "third".       */
const MAX_RETRIES = 20;


/* check_iovv():                                                              *
 * For simultaneously sanity-checking and getting the total size of data in a *
 * variable-length {IOVec} array.  When the array, or any one of its members' *
 * base pointers, is NULL it sets {errno} to EFAULT and returns -1.  When the *
 * array size, or the block size of any of its members, is negative or equals *
 * zero it sets {errno} to EINVAL and returns -1.  In any other case, it will *
 * return the total number of octets delineated by the array.                 */
ssize_t
check_iovv(IOVec *v,  /* A variable-length array of {IOVec}.                  */
             int  n   /* The number of elements in the array.                 */
          )
{ ssize_t sum = 0;
      int i   = 0;
  if (v == NULL) { errno = EFAULT;  return -1; }
  if (n <= 0)    { errno = EINVAL;  return -1; }
  for (; i < n; i++)
  { if (v[i].iov_base == NULL) { errno = EFAULT;  return -1; }
    if (v[i].iov_len  <= 0)    { errno = EINVAL;  return -1; }
    sum += v[i].iov_len;
  }
  if (sum < 0) { errno = EINVAL;  return -1; }
  return sum;
} /* end of check_iovv() */


int
spool_iovv(int   s,  /* A streaming socket descriptor.                        */
         IOVec **v,  /* A variable-length array of {IOVec}.                   */
           int  *n,  /* The number of elements in the array.                  */
         off_t  *l   /* 64-bit {int}.  Returns the number of octets written.  */
          )
{   IOVec *v_bu  = *v;                  /* "vector backup"                    */
    IOVec  el_bu = *v[0];               /* "element backup"                   */
  ssize_t  r     = 0,                   /* "result"                           */
           v_st  = 0,                   /* running "vector subtotal"          */
           v_t   = check_iovv(*v, *n);  /* "vector total"                     */
      int  v_i   = 0;                   /* "vector index"                     */
  if (v_t < 0) return -1;               /* check_iovv() set {errno}; {*l} += 0*/
  while (v_st < v_t)
  { r = writev(s, *v, *n);
    if (r < 0)
    /* The possible errors (given we've already done validation) are:         *
     * EAGAIN  The write to {sd} would have blocked.  No data was written.    *
     * EBADF   {sd} is not a valid descriptor.  OK as is.                     *
     * EDESTADDRREQ  {sd}'s lost its destination.  Map to ENOTCONN.           *
     * EDQUOT  Only for disk writes.  Should never be, but map to ENOTSOCK.   *
     * EFAULT  {hdtr->headers} or something within it is invalid.  OK as is.  *
     * EFBIG   Only if {sd} was a file.  Should never be.  Map to ENOTSOCK.   *
     * EINTR   A signal pre-empted writev.  OK as is.                         *
     * EINVAL  {hdtr->hdr_cnt} > sys max or {v_st} overflowed.  OK as is.     *
     * EIO     There was a problem writing to {sd}.  OK as is.                *
     * ENOBUFS  Ran out of buffers writing to {sd}.  Map to EIO.              *
     * ENOSPC  Only for disk writes.  Should never be, but map to ENOTSOCK.   *
     * EPIPE   {sd} hasn't got a peer streaming socket.  Map to ENOTCONN.     */
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
      *l += v_st;  /* Record the progress thus far.                           */
      /* Put the vector data back the way we got it, just in case:            */
      if (*v != v_bu) *v = v_bu;
      if (((*v)[v_i].iov_base != el_bu.iov_base)
          && ((*v)[v_i].iov_len != el_bu.iov_len)) *v[v_i] = el_bu;
      return -1;
    } /* end error handling after writev()                                    */
    v_st += r;
    if (r && (v_st != v_t))
    /* We didn't get it all, presumably due to being pre-empted / interrupted *
     * in some manner.  Adjust the relevant IOVec and the main vector pointer *
     * before we loop to try again.                                           */
    { /* {r} will keep our running total of preceding elements' sizes.        */
      int d = 0,  /* Difference between the running subtotal sent and {r}.    */
          i = 0;  /* Which element we're looking at.                          */
      /* Restore the vector data if we edited it:                             */
      if (*v != v_bu) *v = v_bu;
      if (((*v)[v_i].iov_base != el_bu.iov_base)
          && ((*v)[v_i].iov_len != el_bu.iov_len)) *v[v_i] = el_bu;
      for (r = 0; (r < v_st) && (i < *n);)
      { d = v_st - r;
        if ((*v)[i].iov_len > d)  /* We have found our target block.            */
        { v_i = i;              /* Record which block that is.                */
          r += d;               /* Bring {r} equal with {v_st}.               */
          el_bu = *v[i];        /* Back up the block's state.                 */
          (*v)[i].iov_len -= d;   /* Record the length of unmoved data.         */
          (*v)[i].iov_base += d;  /* Point to start of unmoved data.  (GNU extn)*/
          if (i > 0)            /* Have we gone past the first block?         */
          { v_bu = *v;          /* Back up the master pointer.                */
            *v += i;            /* Magical master-pointer arithmetic.         */
          }
        } else r += (*v)[i++].iov_len;  /* Advance both {r} and {i}.            */
      } /* end of "crawling the vector" for loop                              */
    } /* end of "we didn't get it all" block                                  */
  } /* end of "streaming the vector data" while loop                          */
  *l += v_st;  /* Record that we streamed this much data.                     */
  /* Put the vector data back the way we got it, just in case:                */
  if (*v != v_bu) *v = v_bu;
  if (((*v)[v_i].iov_base != el_bu.iov_base)
      && ((*v)[v_i].iov_len != el_bu.iov_len)) *v[v_i] = el_bu;
  return 0;
} /* end of spool_iovv() */


/* stubborn_send():                                                           *
 * Call send() until the whole of what was to be sent actually has been.      *
 * Returns 0 on success, and -1 (with errno set appropriately) otherwise.     */
int
stubborn_send(char *bufr,  /* Data to send.                                   */
               int *b_sz,  /* In:  Number of octets to send.                  *
                            * Out:  # octets sent (on error will be fewer).   */
               int  sock   /* Descriptor of the socket to send to.            */
             )
{    char *buf_index  = bufr;   /* Read pointer into the input buffer.        */
   size_t  cumulative = 0;      /* How much has been moved overall.           */
   size_t  to_be_sent = *b_sz;  /* How much to try to send per call.          */
  ssize_t  sent_count;          /* How much actually got sent per call.       */
      int  retries    = 0;      /* How many times we have retried sending.    */
  do
  {
Re_send:
    sent_count = send(sock, buf_index, to_be_sent, 0);
    if (sent_count == -1)
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
          goto Re_send;
        } else  /* All those retries still weren't enough.  Give up.          */
        { errno = EAGAIN;
          *b_sz = cumulative;
          return -1;
        }
      } else if (errno = EMSGSIZE)         /* Sent too much at once.          */
      { to_be_sent = to_be_sent * 3 >> 2;  /* Try a value 3/4 as big.         */
        goto Re_send;
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
{ const Buf_Sz = 1500;  /* Buffer size that should fit in a network packet.   */
  const True   = 1;
  const False  = 0;

       char buffer[Buf_Sz];
        int buf_ptr    = 0;      /* Index of (end-of-data + 1) in the buffer. */
      off_t infile_ptr;          /* 64-bit {int}.  Input file's file pointer. */
      off_t cumulative = 0;      /* 64-bit {int}.  Net number of octets sent. */
        int done_flag  = False;  /* For loop control.                         */
        int temp1;               /* For one-off uses.                         */
  socklen_t temp2;               /* int-sized; for one-off uses.              */
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
  temp2 = sizeof(temp1);
  result = getsockopt(sd, SOL_SOCKET, SO_TYPE, &temp1, &temp2);
  if (result)
  /* The possible errors are:                                                 *
   * EBADF   {sd} is not a valid descriptor.  OK as is.                       *
   * ENOTSOCK  {sd} is not a socket.  OK as is.                               *
   * ENOPROTOOPT  Inapplicable and should never be.  Map to EINVAL.           *
   * EFAULT  temp1 or temp2 is outside our address space.  OK as is.          *
   * EDOM    Some parameter is outside its acceptable range.  Map to EINVAL.  */
  { if ((errno == EDOM /*33*/) || (errno == ENOPROTOOPT /*42*/)) errno = EINVAL;
    return -1;
  }
  if (temp1 != SOCK_STREAM) { errno = ENOTSOCK;  return -1; }
  /* Seek file to correct pos'n.  Do this now so can die early if it fails.   */
  infile_ptr = lseek(fd, offset, SEEK_SET);
  /* If this errored, infile_ptr (= -1) will never equal offset (> 0).        */
  if (infile_ptr != offset) { errno = EIO;  return -1; }

  /* Spool any headers to the socket:                                         */
  if (hdtr != NULL && hdtr->headers != NULL && hdtr->hdr_cnt != 0)
    if (spool_iovv(sd, &(hdtr->headers), &(hdtr->hdr_cnt), len)) return -1;

  /* Spool the file via the buffer to the socket:                             */
  while (done_flag == False)
  { temp1 = 0;
Buf_Fill:
    buf_ptr = read(fd, &buffer, Buf_Sz);
    switch (buf_ptr)
    { case -1:
        if (errno == EINTR || errno == EAGAIN)          /* Usually temporary. */
        { if (temp1++ < MAX_RETRIES) goto Buf_Fill;     /* Try again.         */
          else { *len += cumulative;  return -1; }      /* Give up.           */
        }
        else
        { if (errno == EINVAL) errno = EIO;     /* Can't use EINVAL for this. */
          *len += cumulative;  return -1;
        }
        break;
      case 0:  done_flag = True;  break;                /* Have reached EOF.  */
      default:                                          /* Got data to send.  */
        result      = stubborn_send(buffer, &buf_ptr, sd);
        cumulative += buf_ptr;
        if (result == -1) { *len += cumulative;  return -1; } /* All errs OK. */
  } }

  /* Spool any trailers to the socket:                                        */
  if (hdtr != NULL && hdtr->trailers != NULL && hdtr->trl_cnt != 0)
    if (spool_iovv(sd, &(hdtr->trailers), &(hdtr->trl_cnt), len)) return -1;

  return 0;
} /* end of sendfile() */
