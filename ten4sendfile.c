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
 *     is another {off_t}, passed by reference.                               *
 * {hdtr}, when present, points to a structure describing two variable-length *
 *     arrays of {struct iovec} (see writev(2) and uio.h).  These arrays hold *
 *     header and/or trailer data meant to bookend the file data.  The Mac OS *
 *     documentation does not explain as much, but real-world examples reveal *
 *     that {hdtr} data should count towards the number of octets transmitted.*
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

#define TEN4SENDFILE 1
#include "ten4sendfile.h"

typedef struct stat     Stat;
typedef struct timespec Timespec;

const Timespec a_third     = {0, 16666667};  /* 1/60 second = one "third".    */
const uint32_t MAX_RETRIES = 50;


/* check_iovv():                                                              *
 * For simultaneously sanity-checking and getting the total size of data in a *
 * variable-length {IOVec} array.  When the array, or any one of its members' *
 * base pointers, is NULL it sets {errno} to EFAULT and returns -1.  When the *
 * array size or the block size of any member is negative or zero, {errno} is *
 * set to EINVAL and it returns -1.  In all other cases, it returns the total *
 * number of octets delineated by the array.  The return value is a {ssize_t},*
 * which is ultimately defined as a {long}, thus is either 32 or 64 bits wide *
 * depending on whether LP64 mode is active.                                  */
ssize_t
check_iovv(IOVec varray[],  /* A variable-length array of {IOVec}.            */
             int n_el       /* The number of elements in the array.           */
          )
{ ssize_t sum = 0;
  if (varray == NULL) { errno = EFAULT;  return -1; }
  if (  n_el <= 0)    { errno = EINVAL;  return -1; }
  for (int i = 0; i < n_el; i++)
  { if (varray[i].iov_base == NULL) { errno = EFAULT;  return -1; }
    if (varray[i].iov_len  <= 0)    { errno = EINVAL;  return -1; }
    sum += varray[i].iov_len;
  }
  return sum;  /* Guaranteed to be greater than zero.                         */
} /* end of check_iovv()                                                      */


/* spool_iovv():                                                              *
 * For streaming a variable-length IOVec array to a socket.  If the streaming *
 * gets interrupted, it adjusts the IOVec array to still accurately represent *
 * the data not yet sent.                                                     */
int
spool_iovv(  int   sd,    /* A streaming socket descriptor.                   */
           IOVec **iovv,  /* Variable-length {IOVec} vector, by reference.    */
             int  *n_el,  /* Number of elements in the vector, by reference.  */
           off_t  *len    /* Signed {int64} ref for number of octets written. */
          )
{  ssize_t result    = 0,
           left_2_go = check_iovv(*iovv, *n_el); /* Amount left to stream.    */
    size_t _result;        /* For comparisons to unsigned size_t values.      */
  uint32_t retries   = 0;  /* For pre-emptions and interruptions.             */
  *len = 0;
  if (left_2_go < 0) return -1;  /* check_iovv() already set {errno} to suit. */
  while (left_2_go > 0)  /* Note an initial left_2_go of 0 will bypass this.  */
  { result = writev(sd, *iovv, *n_el);
    /* Function prototype here is ssize_t writev(int s, const IOVec a, int n).
       writev() does all our actual work but doesn't adjust the IOVec data to
       reflect partial progress.                                              */
    if (result < 0) /* writev() suffered an error.                            */
    { switch (errno)  /* Possible errors (given we've already validated) are: */
      { case EAGAIN:
        if (retries++ < MAX_RETRIES)  /* Usually transient; sleep & retry.    */
        { if (nanosleep(&a_third, NULL) && errno == EINTR) break;
          /* The other potential nanosleep() errors ought to be impossible.   */
          continue;  /* In this case, no data was written; just pick up & go. */
        } else break;  /* All those retries still weren't enough.  Give up.   */
        case EBADF:   case EFAULT:   case EINTR:   case EINVAL:   case EIO:
          break; /* We may return these same values, & for these same reasons.*/
        case EDESTADDRREQ:   case EPIPE:
          errno = ENOTCONN;  break;  /* Only possible if socket disconnected. */
        case EDQUOT:   case EFBIG:   case ENOSPC:
          errno = ENOTSOCK;  break;  /* Only if writing to disk, not socket.  */
        default:  /* including case ENOBUFS                                   */
          errno = EIO;  break;
      } /* end of switch statement                                            */
      return -1;  /* Safe; args were adjusted at end of prior loop iteration. */
    } /* end writev() error handling                                          */
    if (result)  /* We moved some data!  Yay!                                 */
    { *len += result;  /* Track how much we've moved in total.                */
      left_2_go -= result;  /* Track how far is left to go.                   */
      if (left_2_go > 0)
      /* We didn't get it all, presumably due to being pre-empted/interrupted
         in some manner.  Adjust the IOVec array before we loop to try again. */
      { _result = (size_t) result;
        while ((**iovv).iov_len <= _result)  /* Can advance to next IOVec.    */
        { _result -= (**iovv).iov_len;
          (*iovv)++;       /* This should add sizeof(IOVec) to the pointer.   */
          (*n_el)--;
          if (*n_el < 1) { errno = EINVAL;  return -1; }  /* No more IOVecs?! */
        } /* end of the "inching up the vector" while loop                    */
        if ((_result) && ((**iovv).iov_len > _result))  /* IOVec partly sent. */
        { (**iovv).iov_len -= _result;  /* Record length of the unmoved data, */
          (**iovv).iov_base += _result;  /* and its beginning.                */
        }
      } /* end of the "we didn't get it all" block                            */
    } /* end of the "we got a result" block                                   */
  } /* end of the "there's still data left to stream" while loop              */
  return 0;
} /* end of spool_iovv()                                                      */


/* stubborn_send():                                                           *
 * Call send() until the whole of what was to be sent actually has been.      *
 * Returns 0 on success, and -1 (with errno set appropriately) otherwise.     */
int
stubborn_send(  char *bufr,  /* Data to send.                                 */
             ssize_t *b_sz,  /* In:  Number of octets to send.                *
                              * Out:  # octets sent (on error will be fewer). */
                 int  sd     /* Descriptor of the socket to send to.          */
             )
{     char *buf_index  = bufr;   /* Read pointer into the input buffer.       */
   ssize_t  cumulative = 0,      /* How much has been moved overall.          */
            to_send    = *b_sz,  /* How much to try to send per call.         */
            result;              /* How much actually got sent per call.      */
  uint32_t  retries    = 0;      /* How many times we have retried sending.   */
  /* Sanity checks:                                                           */
  if (bufr == NULL) { errno = EINVAL;  return -1; }
  if (to_send == 0) return 0;

  do
  { result = send(sd, buf_index, to_send, 0);
    if (result < 0)
    { if (errno == EACCES || errno == EHOSTUNREACH)
      { /* Per the specs, we can't validly return either of those.            */
        errno = ENOTCONN;  *b_sz = cumulative;  return -1;
      } else if (errno == EAGAIN || errno == ENOBUFS)  /* Usually transient.  */
      { if (retries++ < MAX_RETRIES)
        { if (nanosleep(&a_third, NULL) && errno == EINTR)  /* Interrupted;   */
          { *b_sz = cumulative;  return -1; }     /* is the caller's problem. */
          continue;  /* Retry.                                                */
        } else  /* All those retries still weren't enough.  Give up.          */
        { errno = EAGAIN;  *b_sz = cumulative;  return -1; }
      } else if (errno == EMSGSIZE)  /* Tried to send too much at once.       */
      { if (to_send > 1500) to_send = 1500;  /* Try an Ethernet payload size. */
        else to_send = to_send * 3 >> 2;  /* Try a value 3/4 as big.          */
        continue;
      } else  /* Other errors are fatal, but do have safe errno values.       */
      { *b_sz = cumulative;  return -1; }
    }
    /* If we got this far, send() didn't error out on us.  Yay!               */
    if (result == 0) break;  /* We must be done, regardless of measurements.  */
    cumulative += result;
    if (cumulative < *b_sz)
    { buf_index = &(bufr[cumulative]);
      /* Don't try to send more than we have available!                       */
      if ((*b_sz - cumulative) < to_send) to_send = *b_sz - cumulative;
    }
  } while (cumulative < *b_sz);
  *b_sz = cumulative;
  return 0;
} /* end of stubborn_send()                                                   */


int
sendfile (int  fd,      /* Descriptor for the file to send.                   */
          int  sd,      /* Descriptor for the socket to send to.              */
        off_t  offset,  /* {int64_t}.  Index of the first file octet to send. */
        off_t *len,     /* In:  Number of octets to read from the file.       *
                         * Out:  Total number of octets written (headers,     *
                                 file, and trailers combined).                */
      Sf_HdTr *hdtr,    /* Optional header and/or trailer data.               */
          int  flags    /* Reserved.  Return an error if nonzero.             */
         )
{ const int BUF_SZ     = 8192;  /* Buffer size to suit a disk read.           */
       Stat stats;              /* For the stat() calls.                      */
       char buffer[BUF_SZ];     /* The actual buffer.                         */
      off_t len_2_read,         /* How much data to read from the file.       */
            infile_ptr,         /* Track the input-file file pointer.         */
            cumulative = 0;     /* Net number of octets sent.                 */
    ssize_t buf_ptr    = 0;     /* {long}.  Buffer index (end-of-data + 1).   */
   uint32_t retries;            /* How many times we have retried sending.    */
  socklen_t s_len;              /* {uint32_t}.  For the getsockinfo call.     */
        int result;             /* For subroutine & system calls.             */
  /* Sanity-check the arguments.                                              */
  if (len == NULL) { errno = EINVAL;  return -1; }
  len_2_read = *len;
  *len = 0;  /* This is the correct value for all the error returns below.    */
  if (offset < 0 || flags != 0) { errno = EINVAL;  return -1; }

  /* Sanity-check the file descriptor.                                        */
  if (fstat(fd, &stats)) return -1;  /* All 3 possible errnos are OK.         */
  if ((stats.st_mode & S_IFMT) != S_IFREG)  /* Not a regular file.  Fail.     */
  { errno = ENOTSUP; return -1; }
  if (offset > stats.st_size) len_2_read = 0;  /* So we can check {sd} too.   */

  /* Sanity-check the socket descriptor, insofar as is practical.             */
  if (fstat(sd, &stats)) return -1;  /* All 3 possible errnos are OK.         */
  if ((stats.st_mode & S_IFMT) != S_IFSOCK)  /* Not a socket.  Fail.          */
  { errno = ENOTSOCK;  return -1; }
  s_len = sizeof(result);
  if (getsockopt(sd, SOL_SOCKET, SO_TYPE, &result, &s_len)) /* Got an error.  */
  { /* All possible errors are OK as is, except these two:                    */
    if ((errno == EDOM) || (errno == ENOPROTOOPT)) { errno = EINVAL; }
    return -1;
  }
  if (result != SOCK_STREAM) { errno = ENOTSOCK;  return -1; }

  /* Seek file to correct pos'n.  Do this now so can die early if it fails.   */
  infile_ptr = lseek(fd, offset, SEEK_SET);
  /* If this errored, infile_ptr (= -1) will never equal offset (>= 0).  Even
     if it thinks it succeeded, if the two differ we still want to bail out.  */
  if (infile_ptr != offset) { errno = EIO;  return -1; }

  /* Spool any headers to the socket:                                         */
  if (hdtr && (hdtr->headers != NULL || hdtr->hdr_cnt != 0))
  { IOVec *temp_iovv = hdtr->headers;
      int  temp_n    = hdtr->hdr_cnt;
    off_t  temp_len  = 0;
    if (spool_iovv(sd, &temp_iovv, &temp_n, &temp_len)) return -1; /* errno OK*/
    *len += temp_len;
  }

  /* Spool the file via the buffer to the socket:                             */
  retries = 0;
  while (len_2_read > 0)
  { buf_ptr = read(fd, &buffer, (BUF_SZ < len_2_read ? BUF_SZ : len_2_read));
    if (buf_ptr < 0)  /* The read call failed.                                */
    { if (errno == EINTR || errno == EAGAIN)  /* Usually transient.           */
      { if (retries++ < MAX_RETRIES) continue;  /* Try again.                 */
        else { *len += cumulative;  return -1; }  /* Give up.                 */
      }
      else  /* It actually is a failure we can't ignore.                      */
      { if (errno == EINVAL) errno = EIO;  /* Can't EINVAL here.              */
        *len += cumulative;  return -1;
    } }  /* If we've gotten this far, it didn't error out on us!  Yay!        */
    if (buf_ptr == 0) break;  /* We've hit EOF.                               */
    /* buf_ptr > 0:  Got data to send.                                        */
    len_2_read -= buf_ptr;  /* Got this much left to go.                      */
    result = stubborn_send(buffer, &buf_ptr, sd);
    if (result == -1) { *len += cumulative;  return -1; }  /* All errors OK.  */
    cumulative += buf_ptr;
  }
  *len += cumulative;

  /* Spool any trailers to the socket:                                        */
  if (hdtr && (hdtr->trailers != NULL || hdtr->trlr_cnt != 0))
  { IOVec *temp_iovv = hdtr->trailers;
      int  temp_n    = hdtr->trlr_cnt;
    off_t  temp_len  = 0;
    if (spool_iovv(sd, &temp_iovv, &temp_n, &temp_len)) return -1; /* errno OK*/
    *len += temp_len;
  }

  return 0;
} /* end of sendfile()                                                        */
