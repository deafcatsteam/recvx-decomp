/* PC port shim for the PS2 SDK <sif.h> (EE <-> IOP transfers). */
#ifndef _sif_h_
#define _sif_h_

#include <eetypes.h>
#include <eekernel.h>

typedef struct {
    u_int data;
    u_int addr;
    u_int size;
    u_int mode;
} sceSifDmaData;

#define SIF_DMA_INT_I 0x02
#define SIF_DMA_INT_O 0x04

u_int sceSifSetDma(sceSifDmaData *sdd, int len);
int sceSifDmaStat(u_int id);
int sceSifSyncIop(void);
int sceSifRebootIop(const char *img);
void *sceSifAllocIopHeap(u_int size);
int sceSifInitIopHeap(void);
int sceSifLoadModule(const char *filename, int args, const char *argp);

#endif
