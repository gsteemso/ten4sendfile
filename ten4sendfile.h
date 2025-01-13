/* ten4sendfile:  Implements sendfile(), omitted from Mac OS 10.4.  *
 *                                                                  *
 * This routine is in the system headers but the implementation and *
 * the manpage were left out.  This one-function library makes good *
 * the first part of that deficit.                                  */

#define SENDFILE 0

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

typedef struct iovec Iovec_t;

struct sf_hdtr {
    Iovec_t *headers;   /* Array of header data. */
        int  hdr_cnt;   /* Length of header array. */
    Iovec_t *trailers;  /* Array of trailer data. */
        int  trl_cnt;   /* Length of trailer array. */
};

typedef struct sf_hdtr Hdtr_t;

int sendfile(int, int, off_t, off_t *, Hdtr_t *, int);
