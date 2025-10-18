/* ten4sendfile:  Implements sendfile(), as missing from Mac OS 10.4.         *
 *                                                                            *
 * Sendfile() appears in the Mac OS 10.4 system headers, but was not actually *
 * implemented.  (No manpage for it was provided, either).  This tiny library *
 * rectifies the former oversight.                                            */

/* If this preprocessor constant is defined to anything else, the incorrect & *
 * unimplemented sendfile() system call prototype in the Mac OS 10.4.x system *
 * headers gets exposed.  We REALLY don't want that.                          */
#define SENDFILE 0

/* This header file is meant to be installed, as <sys/socket.h>, to somewhere *
 * that gets searched for system headers before /usr/include.                 */
#include_next <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Per sys/uio.h, a {struct iovec} consists of a {void *iov_base} to a memory *
 * region and a {size_t iov_len} stating that region's length.  A {size_t} is *
 * an {unsigned long}.                                                        */
typedef struct iovec IOVec;

/* This structure definition actually is already present in the Mac OS 10.4.x *
 * system headers - but it is only visible if the SENDFILE constant above has *
 * a nonzero definition, so we can't actually use it.                         */
struct sf_hdtr {
    IOVec *headers;   /* Array of header data. */
      int  hdr_cnt;   /* Length of header array. */
    IOVec *trailers;  /* Array of trailer data. */
      int  trl_cnt;   /* Length of trailer array. */
};
typedef struct sf_hdtr Sf_HdTr;

int sendfile(int, int, off_t, off_t *, Sf_HdTr *, int);
