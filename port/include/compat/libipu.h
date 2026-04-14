#ifndef RECVX_COMPAT_LIBIPU_H
#define RECVX_COMPAT_LIBIPU_H
#include <eetypes.h>
int sceIpuCtrl(unsigned int cmd);
int sceIpuWait(int ctrl);
#endif
