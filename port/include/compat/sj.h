/* compat: CRI stream-joint placeholder. Opaque types — FMV path will
 * be replaced wholesale by FFmpeg, so layout accuracy is irrelevant. */
#ifndef RECVX_COMPAT_SJ_H
#define RECVX_COMPAT_SJ_H
#include <eetypes.h>

typedef struct _SJ_OBJ  { int _pad; } SJ_OBJ;
typedef SJ_OBJ *SJ;

typedef struct _SJCK {
    void    *sjd;
    int      sjd_len;
    int      id;
    int      info;
    int      rewind;
} SJCK;

#endif
