/*
 * Stand-in for the PS2's I/O processor (IOP) as seen from the game.
 *
 * On the PS2 the game hands work to programs running on the IOP: data goes
 * over SIF DMA into IOP memory and requests go through SIF RPC. Here the IOP
 * is a block of memory, and the game's sound driver (TSNDDRV, whose code is
 * not available) is replaced by port/src/audio/pc_snddrv.c: its requests
 * are passed there, and this file answers the state queries that locate
 * the driver's blocks in IOP memory.
 */
#include <eekernel.h>
#include <sif.h>
#include <sifrpc.h>

#include <stdio.h>
#include <string.h>

#include "../audio/pc_snddrv.h"

#define IOP_RAM_SIZE 0x200000
#define IOP_ADDR(a) ((unsigned int)(a) & (IOP_RAM_SIZE - 1))

/* Where the model sound driver keeps its blocks in IOP memory. */
#define SND_STATUS_ADDR 0x100000 /* SND_STATUS, read back by get_iopsnd_info() */
#define SND_TABLE_ADDR 0x1f0000  /* addresses of its header/sequence/data areas */
#define SND_HD_AREA 0x120000
#define SND_SQ_AREA 0x140000
#define SND_DATA_AREA 0x160000

#define SND_STATUS_SIZE 0x42 /* SND_STATUS in ps2_snddrv.h */

static unsigned char iop_ram[IOP_RAM_SIZE];
static unsigned int heap_next = 0x8000;

/* The last block received over SIF DMA, which the next "header set"
 * command refers to. */
static unsigned int last_dma_addr, last_dma_size;

/* ---- Memory and DMA -------------------------------------------------- */

int sceSifInitIopHeap(void) { return 0; }

void *sceSifAllocIopHeap(u_int size)
{
    unsigned int addr = heap_next;

    if (addr + size > SND_STATUS_ADDR) {
        fprintf(stderr, "iop: out of IOP heap\n");
        return NULL;
    }
    heap_next += (size + 63) & ~63u;
    return (void *)addr;
}

static u_int set_dma(sceSifDmaData *sdd, int len)
{
    for (int i = 0; i < len; i++) {
        unsigned int addr = IOP_ADDR(sdd[i].addr);
        unsigned int size = sdd[i].size;

        if (addr + size > IOP_RAM_SIZE)
            size = IOP_RAM_SIZE - addr;
        memcpy(iop_ram + addr, (const void *)sdd[i].data, size);
        last_dma_addr = addr;
        last_dma_size = size;
    }
    return 1; /* transfer id; it completes at once */
}

u_int sceSifSetDma(sceSifDmaData *sdd, int len) { return set_dma(sdd, len); }
u_int isceSifSetDma(sceSifDmaData *sdd, int len) { return set_dma(sdd, len); }
int sceSifDmaStat(u_int id) { return -1; /* finished */ }

int sceSifGetOtherData(sceSifReceiveData *rd, void *src, void *dest, int size, u_int mode)
{
    unsigned int addr = IOP_ADDR(src);

    if (addr + size > IOP_RAM_SIZE)
        return -1;
    /* The sound driver's status changes on the audio thread. */
    if (addr < SND_STATUS_ADDR + SND_STATUS_SIZE && addr + size > SND_STATUS_ADDR)
        pc_snddrv_status(iop_ram + SND_STATUS_ADDR);
    memcpy(dest, iop_ram + addr, size);
    return 0;
}

/* ---- Modules and RPC ------------------------------------------------- */

void sceSifInitRpc(u_int mode) {}
int sceSifRebootIop(const char *img) { return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifLoadModule(const char *filename, int args, const char *argp) { return 1; }

static int serve_dummy;

int sceSifBindRpc(sceSifClientData *bd, u_int request, u_int mode)
{
    if (request == 0)
        pc_snddrv_init(); /* the game binds to its sound driver */
    bd->command = request;
    bd->serve = (sceSifServeData *)&serve_dummy;
    return 0;
}

int sceSifCallRpc(sceSifClientData *bd, u_int fno, u_int mode, void *send, int ssize,
                  void *receive, int rsize, sceSifEndFunc end_function, void *end_param)
{
    if (bd->command == 0 && send != NULL) {
        /* TSNDDRV request stream */
        PcIopView view = { iop_ram, IOP_RAM_SIZE, last_dma_addr, last_dma_size, SND_DATA_AREA };

        pc_snddrv_requests(send, ssize, &view);
    } else if (bd->command == 1 && receive != NULL && rsize >= 4) {
        /* TSNDDRV state query: 18 and 19 locate its blocks in IOP memory. */
        unsigned int table[16] = { SND_HD_AREA, SND_SQ_AREA, SND_DATA_AREA };
        int answer = 0;

        memcpy(iop_ram + SND_TABLE_ADDR, table, sizeof(table));
        if (fno == 18)
            answer = SND_STATUS_ADDR;
        else if (fno == 19)
            answer = SND_TABLE_ADDR;
        *(int *)receive = answer;
    }
    if (end_function != NULL)
        end_function(end_param);
    return 0;
}
