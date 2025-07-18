/* ten4sendfile:  Implements sendfile(), as missing from Mac OS 10.4.         *
 *                                                                            *
 * Sendfile() appears in the Mac OS 10.4 system headers, but an actual imple- *
 * mentation was omitted (as was the manpage).  This little library rectifies *
 * the former oversight.                                                      */

#define SENDFILE 0

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Per sys/uio.h, a {struct iovec} consists of a {void *iov_base} to a memory *
 * region and a {size_t iov_len} stating that region's length.  A {size_t} is *
 * an {unsigned long}.                                                        */

typedef struct iovec IOVec;

/* This structure definition actually is already present in the Mac OS 10.4.x *
 * system headers - but not useably.  The #define that would let the compiler *
 * see it _also_ exposes the incorrect and unimplemented sendfile system call *
 * prototype.                                                                 */

struct sf_hdtr {
    IOVec *headers;   /* Array of header data. */
      int  hdr_cnt;   /* Length of header array. */
    IOVec *trailers;  /* Array of trailer data. */
      int  trl_cnt;   /* Length of trailer array. */
};
typedef struct sf_hdtr Sf_HdTr;

int sendfile(int, int, off_t, off_t *, Sf_HdTr *, int);
