/* ten4sendfile:  Implements sendfile(2), as missing from Mac OS 10.4.        *
 *                                                                            *
 * Sendfile() appears in the Mac OS 10.4 system headers, but was not actually *
 * implemented.  (No manpage for it was provided, either).  This tiny library *
 * rectifies the former oversight.                                            */

/* If this preprocessor constant is set true, the incorrect and unimplemented *
 * sendfile() prototype in the Mac OS 10.4.x system headers gets exposed.  We *
 * REALLY don't want that.                                                    */
#undef SENDFILE

/* This header file is meant to be installed, as <sys/socket.h>, to somewhere *
 * that gets searched for system headers before /usr/include.                 */
#include_next <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Per sys/uio.h, a {struct iovec} consists of a {void *} to a memory region, *
 * called "iov_base", & a {size_t} for its size, named "iov_len".  A {size_t} *
 * is defined as an {unsigned long}.                                          */
typedef struct iovec IOVec;

/* Per sys/socket.h, a {struct sf_hdtr} consists of four members, arranged as *
 * a pair of array descriptions:  The first {IOVec *} is called "headers" and *
 * its associated {int}, "hdr_cnt"; the second {IOVec *} is called "trailers" *
 * and its {int} is "trlr_cnt".  Even though this structure definition should *
 * by all rights be invisible because of the undefined constant above (not to *
 * mention that _POSIX_C_SOURCE is probably defined at that point), placing a *
 * copy here nevertheless SOMEHOW yields a duplicate-definition error.  HOW?! */
#ifdef BUILDING_TEN4SENDFILE
struct sf_hdtr {
    IOVec *headers;   /* Array of header data. */
      int  hdr_cnt;   /* Length of header array. */
    IOVec *trailers;  /* Array of trailer data. */
      int  trl_cnt;   /* Length of trailer array. */
};
#endif
typedef struct sf_hdtr Sf_HdTr;

int sendfile(int, int, off_t, off_t *, Sf_HdTr *, int);
