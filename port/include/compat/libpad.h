#ifndef RECVX_COMPAT_LIBPAD_H
#define RECVX_COMPAT_LIBPAD_H
#include <eetypes.h>
#define scePadStateStable    (6)
#define scePadStateFindCTP1  (2)
#define InfoModeCurExID      (2)
int scePadInit(int mode);
int scePadEnd(void);
int scePadPortOpen(int port, int slot, void* addr);
int scePadPortClose(int port, int slot);
int scePadRead(int port, int slot, unsigned char* data);
int scePadGetState(int port, int slot);
int scePadGetReqState(int port, int slot);
int scePadInfoMode(int port, int slot, int term, int off);
int scePadSetMainMode(int port, int slot, int off, int lock);
int scePadSetActDirect(int port, int slot, const unsigned char* data);
#endif
