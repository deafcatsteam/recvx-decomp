/*
 * Stand-in for the PS2's I/O processor (IOP) as seen from the game.
 *
 * On the PS2 the game hands work to programs running on the IOP: data goes
 * over SIF DMA into IOP memory and requests go through SIF RPC. Here the IOP
 * is a block of memory plus a minimal model of the game's sound driver
 * (TSNDDRV, whose code is not available): it answers the state queries and
 * acknowledges sound bank uploads with the checksum the game expects, so the
 * game's loading code goes through. Sound itself is still silent.
 * TODO: replace the model with a PC sound driver that plays the banks.
 */
#include <eekernel.h>
#include <sif.h>
#include <sifrpc.h>

#include <stdio.h>
#include <string.h>

#define IOP_RAM_SIZE 0x200000
#define IOP_ADDR(a) ((unsigned int)(a) & (IOP_RAM_SIZE - 1))

/* Where the model sound driver keeps its blocks in IOP memory. */
#define SND_STATUS_ADDR 0x100000 /* SND_STATUS, read back by get_iopsnd_info() */
#define SND_TABLE_ADDR 0x1f0000  /* addresses of its header/sequence/data areas */
#define SND_HD_AREA 0x120000
#define SND_SQ_AREA 0x140000
#define SND_DATA_AREA 0x160000

/* SND_STATUS from ps2_snddrv.h: the checksums the game compares with its own. */
#define SND_STATUS_MIDI_SUM 0x1e
#define SND_STATUS_SE_SUM 0x26

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
    bd->command = request;
    bd->serve = (sceSifServeData *)&serve_dummy;
    return 0;
}

static void put_short(unsigned int addr, short v)
{
    memcpy(iop_ram + addr, &v, sizeof(v));
}

/* Same sum as the game computes before sending a sound bank header. */
static short block_sum(unsigned int size)
{
    short sum = 0;

    if (size > last_dma_size)
        size = last_dma_size;
    for (unsigned int i = 0; i < size; i++)
        sum += (signed char)iop_ram[last_dma_addr + i];
    return sum;
}

/* Length of one request in the driver's command stream, as built by
 * sending_req() in ps2_snddrv.c; 0 when it cannot be known. */
static int request_length(int cd)
{
    if (cd < 0x10)
        return 4 + (cd & 1) + ((cd >> 1) & 1) + ((cd >> 2) & 1) * 2;
    switch (cd & 0xf0) {
    case 0x10:
        return cd == 0x11 ? 3 : 1;
    case 0x20:
        if (cd >= 0x22 && cd <= 0x25)
            return 3;
        if (cd == 0x26)
            return 4;
        if (cd == 0x20)
            return 5;
        if (cd == 0x27 || cd == 0x28 || cd == 0x29 || cd == 0x2c || cd == 0x2d)
            return 8;
        return 2;
    case 0x40:
        if ((cd >= 0x47 && cd <= 0x4a) || cd == 0x41 || cd == 0x42)
            return 2;
        if (cd == 0x4b)
            return 3;
        if (cd == 0x45 || cd == 0x4c)
            return 4;
        if (cd == 0x44)
            return 6;
        if (cd >= 0x4d)
            return 0; /* variable-length forms */
        return 1;
    case 0x50:
    case 0x60:
        return (cd >= 0x51 && cd <= 0x54) ? 8 : 2;
    }
    return 0;
}

static void sound_driver_requests(const unsigned char *buf, int size)
{
    int pos = 0;

    while (pos < size && buf[pos] != 0xff) {
        int cd = buf[pos];
        int len = request_length(cd);

        if (len == 0 || pos + len > size)
            break;
        if (cd == 0x28 || cd == 0x29) {
            /* Sound bank header received: report its checksum. */
            int port = buf[pos + 1];
            unsigned int bytes = buf[pos + 4] | (buf[pos + 5] << 8) |
                                 (buf[pos + 6] << 16) | ((unsigned int)buf[pos + 7] << 24);
            int sum_offset = cd == 0x28 ? SND_STATUS_MIDI_SUM : SND_STATUS_SE_SUM;

            if (port < (cd == 0x28 ? 4 : 5))
                put_short(SND_STATUS_ADDR + sum_offset + port * 2, block_sum(bytes));
        }
        pos += len;
    }
}

int sceSifCallRpc(sceSifClientData *bd, u_int fno, u_int mode, void *send, int ssize,
                  void *receive, int rsize, sceSifEndFunc end_function, void *end_param)
{
    if (bd->command == 0 && send != NULL) {
        /* TSNDDRV request stream */
        sound_driver_requests(send, ssize);
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
