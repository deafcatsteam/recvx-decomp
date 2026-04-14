#ifndef RECVX_COMPAT_LIBIPU_H
#define RECVX_COMPAT_LIBIPU_H
#include <eetypes.h>
typedef struct { int _pad; } sceIpuDmaEnv;
int sceIpuCtrl(unsigned int cmd);
int sceIpuWait(int ctrl);
#endif
