/* PC port shim for the PS2 SDK <libdma.h>. */
#ifndef _libdma_h_
#define _libdma_h_

#include <eetypes.h>

typedef struct sceDmaChan {
    u_int chcr;  u_int p0[3];
    void *madr;  u_int p1[3];
    u_int qwc;   u_int p2[3];
    void *tadr;  u_int p3[3];
    void *as0;   u_int p4[3];
    void *as1;   u_int p5[3];
    u_int p6[4];
    u_int p7[4];
    void *sadr;  u_int p8[3];
} sceDmaChan;

#define SCE_DMA_VIF0    0
#define SCE_DMA_VIF1    1
#define SCE_DMA_GIF     2
#define SCE_DMA_fromIPU 3
#define SCE_DMA_toIPU   4

int sceDmaReset(int mode);
sceDmaChan *sceDmaGetChan(int id);
void sceDmaSend(sceDmaChan *d, void *tag);
void sceDmaSendN(sceDmaChan *d, void *addr, int size);
int sceDmaSync(sceDmaChan *d, int mode, int timeout);

#endif
