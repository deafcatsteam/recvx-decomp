/* compat: minimal sceCd* API surface — enough for ps2_sfd_mw.c. */
#ifndef RECVX_COMPAT_LIBCDVD_H
#define RECVX_COMPAT_LIBCDVD_H
#include <eetypes.h>

typedef struct sceCdRMode {
    unsigned char trycount;
    unsigned char spindlctrl;
    unsigned char datapattern;
    unsigned char pad;
} sceCdRMode;

typedef struct sceCdlFILE {
    unsigned int lsn;
    unsigned int size;
    char name[16];
    unsigned char date[8];
    unsigned int fp;
} sceCdlFILE;

int sceCdInit(int mode);
int sceCdSync(int mode);
int sceCdPause(void);
int sceCdDiskReady(int mode);
int sceCdSearchFile(void* fp, const char* name);
int sceCdRead(unsigned int lsn, unsigned int sectors, void* buf, void* mode);
int sceCdStInit(unsigned int bufmax, unsigned int bsize, void* bufaddr);
int sceCdStStart(unsigned int lsn, void* mode);
int sceCdStStop(void);
int sceCdStRead(unsigned int sectors, void* buf, unsigned int mode, unsigned int* err);

#endif
