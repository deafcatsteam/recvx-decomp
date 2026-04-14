#ifndef RECVX_COMPAT_LIBDMA_H
#define RECVX_COMPAT_LIBDMA_H
#include <eetypes.h>
void sceDmaSend(int ch, void* addr);
int  sceDmaSync(int ch, int mode, int to);
#endif
