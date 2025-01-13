/* ten4sendfile:  Implements sendfile(), omitted from Mac OS 10.4.  *
 *                                                                  *
 * This routine is in the system headers but the implementation and *
 * the manpage were left out.  This one-function library makes good *
 * the first part of that deficit.                                  */

/* From the Leopard manpage:
 *
 *    sendfile(fd, s, offset, len, hdtr, flags);
 *
 * This copies the regular file on descriptor {fd} to the stream socket {s}.
 * {offset} is the point in the file at which to begin the copy.  {len} gives
 * how many bytes to send (if zero, everything to EOF) and returns the number
 * actually sent.  {hdtr}, if specified, points to two variable-length arrays
 * of [struct iovec]s (see writev(2)).  These define header and/or trailer
 * data to be sent with the file data.  Whether this data should be counted as
 * output in {len} is not stated (I'm assuming not).  {flags} is reserved.
 *
 * If {fd} is not a valid file descriptor, set EBADF.  If it is not a regular
 * file, set ENOTSUP.  If its file system does not support sendfile(), set
 * EOPNOTSUPP.  If an error occurs while reading from it, set EIO.
 *
 * If {s} is not a valid socket descriptor, set EBADF.  If it is not a socket,
 * or not a stream-oriented socket, set ENOTSOCK.  If it is not connected, set
 * ENOTCONN.  If it is marked for nonblocking I/O and sendfile() is pre-empted,
 * return the number of octets actually sent and set EAGAIN.  If the other end
 * closes the connection, set EPIPE.
 *
 * If {offset} is negative, set EINVAL.  If it exceeds the file length, report
 * success and zero octets sent.
 *
 * If {len} is null, set EINVAL.  If nonvalid, set EFAULT.
 *
 * If {hdtr} or anything in it is an invalid address, set EFAULT.
 *
 * If {flags} is nonzero, set EINVAL.
 *
 * If sendfile is interrupted by a signal, return the number of octets sent
 * (potentially zero) and set EINTR.
 *
 * Return 0 on success and -1 on failure, also setting {errno} appropriately.
 */

#include <sys/errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "ten4sendfile.h"

typedef struct stat     Stat_t;
typedef struct timespec Timespec_t;

const Timespec_t a_third = {0, 016666666};    /* 1/60 second = one third */

/* A routine for reading from an iovec array.  Call it repeatedly until the data runs out. */
int
read_iov_array(Iovec_t *in_data,    /* Flex-array of iovecs. */
                   int *in_dex,     /* Array index of currently-being-read iovec. */
                size_t *in_ptr,     /* Index of first unread octet in current iovec. */
                  char *outbuf,     /* The output buffer. */
                   int  insz,       /* Number of iovecs in the input array. */
                   int  bufsz       /* Length of the output buffer. */
              ) {
      char *new_base;
    size_t  new_len;
       int  cumulative = 0;

    while (*in_dex < insz) {
        new_base = (char *) in_data[*in_dex].iov_base;
        new_len  = in_data[*in_dex].iov_len;
        while (*in_ptr < new_len) {
            outbuf[cumulative++] = new_base[*in_ptr++];
            if (cumulative >= bufsz)
                return cumulative;  /* Return a full buffer, probably with data remaining. */
        }
        *in_dex++;
        *in_ptr = 0;
    }
    return cumulative;              /* Return a partial or empty buffer, with all data consumed. */
} /* end of read_iov_array() */

/* A routine for calling send() until the whole of what was to be sent actually has been. */
int
stubborn_send(char *bfr,    /* What to send. */
               int *b_sz,   /* In:  how much to send.  Error:  How much was sent.*/
               int  sock    /* Descriptor for the socket to send to. */
             ) {
     size_t octet_count;    /* How much to send, upsized for the convenience of the send() call. */
     size_t remaining;      /* How much is left to move. */
     size_t cumulative;     /* How much has been moved overall. */
    ssize_t sent_count;     /* How much actually got sent. */
    /* Both size_t and ssize_t are the same size as a long. */
        int retries;        /* How many times we have tried to send the same data. */
        int temp;           /* For temporary counters and return values we don't care about. */

    octet_count = *b_sz;
    remaining   = octet_count;
    do {
        retries    = 0;
Re_send:
        sent_count  = send(sock, bfr, octet_count, 0);
        if (sent_count == -1) {
            if (errno == EACCES || errno == EHOSTUNREACH) {
                errno  = ENOTCONN;      /* Per specs, can't set errno to these values. */
                *b_sz -= remaining;
                return -1;
            } else if (errno == EAGAIN || errno == ENOBUFS) {
                if (retries++ < 6) {
                    temp = nanosleep(&a_third, NULL);
                    goto Re_send;       /* Try repeatedly; these errors are usually transient. */
                } else {
                    errno  = EAGAIN;    /* Give up if 6 retries wasn't enough. */
                    *b_sz -= remaining;
                    return -1;
                }
            } else if (errno = EMSGSIZE) {
                octet_count = octet_count * 3 >> 2;     /* Try a value 3/4 as big. */
                goto Re_send;
            } else {
                *b_sz -= remaining;
                return -1;      /* All possible errno values are OK. */
            }
        }
        remaining -= sent_count;
        if (remaining > 0) {
            temp = 0;
            while (temp < remaining) {
                bfr[temp] = bfr[sent_count + temp];
                temp++;         /* Shuffle the unsent part down to the start of the buffer. */
            }
        }
    } while (remaining > 0);
    return 0;
} /* end of stubborn_send() */

int
sendfile (int  fd,      /* Descriptor for the file to send. */
          int  sd,      /* Descriptor for the socket to send to. */
        off_t  offset,  /* Index of first unread octet in file. */
        off_t *len,     /* In:  how many octets to move.  Out:  how many were in fact moved. */
       Hdtr_t *hdtr,    /* Optional header/trailer data. */
          int  flags    /* Reserved, do not use. */
         ) {
/* A buffer size that should fit into a network packet.  Ends on a multiple of 4, not of 8. */
#define Buf_Sz 1500
#define True   1
#define False  0

         char buffer[Buf_Sz];
          int buf_ptr;          /* Points at end-of-data in the buffer. */
    /* -- now aligned on a multiple of 8 octets -- */
    socklen_t sz_or_rpt;        /* Same size as an int; used for two unrelated things. */
          int done_flag;        /* For loop control. */
          int result;           /* For system calls. */
          int curr_block;       /* In headers and trailers, index of the current data block. */
       size_t curr_off;         /* Same size as a long; in headers and trailers, points
                                   at start-of-unsent-data in the current data block. */
        off_t infile_ptr;       /* Mirrors the input file's file pointer. */
        off_t cumulative;       /* Tracks how many bytes have been sent. */
       Stat_t stat_block;       /* For the stat() calls. */

    /* Sanity-check the arguments. */
    if (offset < 0 || len == NULL || flags != 0) {
        errno = EINVAL;
        return -1;
    }

    /* Sanity-check the file descriptor. */
    if (fstat(fd, &stat_block) != 0)
        return -1;      /* All possible errno values are OK. */
    if (stat_block.st_mode & S_IFMT != S_IFREG) {
        errno = ENOTSUP;
        return -1;
    }
    if (offset > stat_block.st_size) {
        *len = 0;       /* Finished without having to do anything!  Awesome. */
        return 0;
    }

    /* Sanity-check the socket descriptor, insofar as is practical. */
    if (fstat(sd, &stat_block) != 0)
        return -1;      /* All possible errno values are OK. */
    if (stat_block.st_mode & S_IFMT != S_IFSOCK) {
        errno = ENOTSOCK;
        return -1;
    }
    sz_or_rpt = sizeof(result);
    if (getsockopt(sd, SOL_SOCKET, SO_TYPE, &result, &sz_or_rpt) != 0)
        return -1;      /* The possible errno values that are not OK ought to be unreachable. */
    if (result != SOCK_STREAM) {
        errno = ENOTSOCK;
        return -1;
    }

    /* Seek the file to the correct position.  Do this now so we can bail early if it fails. */
    infile_ptr = lseek(fd, offset, SEEK_SET);
    if (infile_ptr != offset) {
        errno = EIO;        /* Trying to recover from this is more trouble than it's worth. */
        return -1;
    }

    done_flag  = False;
    cumulative = 0;

    /* Read the header into the buffer / write the buffer into the socket, until done. */
    if (hdtr != NULL && (*hdtr).headers != NULL && (*hdtr).hdr_cnt != 0) {
        curr_block = 0;
        curr_off   = 0;
        do {
            buf_ptr = read_iov_array((*hdtr).headers, &curr_block, &curr_off, buffer, (*hdtr).hdr_cnt, Buf_Sz);
            if (buf_ptr != 0) {
                result = stubborn_send(buffer, &buf_ptr, sd);
                if (result == -1) {
                    *len = 0;       /* All possible errno values are OK. */
                    return -1;
                }
            }
        } while (buf_ptr != 0);
    }

    /* Read the file into the buffer / write the buffer into the socket, until done. */
    while (done_flag == False) {
        sz_or_rpt = 0;
Buf_Fill:
        buf_ptr = read(fd, &buffer, Buf_Sz);
        if (buf_ptr == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                if (sz_or_rpt++ < 6) {
                    goto Buf_Fill;      /* Temporary problem, try again. */
                } else {
                    *len = cumulative;  /* Give up if 6 retries doesn't get us anywhere. */
                    return -1;
                }
            } else {
                if (errno == EINVAL)
                    errno = EIO;        /* Per spec, can't set EINVAL for read()'s reason. */
                *len = cumulative;
                return -1;
            }
        } else if (buf_ptr == 0) {      /* Have reached EOF. */
            done_flag = True;
        } else {                        /* Have not reached EOF, got data to send. */
            result      = stubborn_send(buffer, &buf_ptr, sd);
            cumulative += buf_ptr;
            if (result == -1) {
                *len = cumulative;      /* All possible errno values are OK. */
                return -1;
            }
        }
    }

    /* Read the trailer into the buffer / write the buffer into the socket, until done. */
    if (hdtr != NULL && (*hdtr).trailers != NULL && (*hdtr).trl_cnt != 0) {
        curr_block = 0;
        curr_off   = 0;
        do {
            buf_ptr = read_iov_array((*hdtr).trailers, &curr_block, &curr_off, buffer, (*hdtr).trl_cnt, Buf_Sz);
            if (buf_ptr != 0) {
                result = stubborn_send(buffer, &buf_ptr, sd);
                if (result == -1) {
                    *len = cumulative;      /* All possible errno values are OK. */
                    return -1;
                }
            }
        } while (buf_ptr != 0);
    }
}