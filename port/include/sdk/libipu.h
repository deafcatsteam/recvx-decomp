/* PC port shim for the PS2 SDK <libipu.h> (MPEG decoder hardware). */
#ifndef _libipu_h_
#define _libipu_h_

#include <eetypes.h>

typedef struct {
    u_int d4madr;
    u_int d4tadr;
    u_int d4qwc;
    u_int d4chcr;
    u_int d3madr;
    u_int d3qwc;
    u_int d3chcr;
    u_int ipubp;
    u_int ipuctrl;
} sceIpuDmaEnv;

typedef struct {
    u_int pix[256];
} sceIpuRGB32;

void sceIpuInit(void);
void sceIpuBCLR(int bp);
int sceIpuIsBusy(void);
void sceIpuSync(int mode, u_short timeout);

#endif
