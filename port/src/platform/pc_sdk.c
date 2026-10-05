/*
 * PC replacements for the rest of the Sony PS2 libraries the game calls:
 * DVD, IOP communication, GS setup, DMA, controllers and memory cards.
 *
 * DVD reads are served from the disc image (pc_disc.c). The controller is
 * reported as a DualShock 2 whose state comes from pc_pad (filled by the
 * window/input layer). The rest are placeholders that report success, so the
 * game's init sequences run through; graphics, sound and memory cards get
 * real implementations in their own layers.
 */
#include <eekernel.h>
#include <libcdvd.h>
#include <libdma.h>
#include <libgraph.h>
#include <libmc.h>
#include <libpad.h>
#include <sif.h>
#include <sifrpc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pc_disc.h"
#include "pc_platform.h"

/* ---- DVD --------------------------------------------------------------- */

static int cd_error;

int sceCdInit(int init_mode)
{
    if (!pc_disc_open()) {
        fprintf(stderr, "The game needs its disc image to run.\n");
        exit(1);
    }
    return 1;
}

int sceCdMmode(int media) { return 1; }
int sceCdDiskReady(int mode) { return 2; /* SCECdComplete */ }
int sceCdGetError(void) { return cd_error; }
int sceCdSync(int mode) { return 0; }
int sceCdPause(void) { return 1; }

int sceCdSearchFile(sceCdlFILE *fp, const char *name)
{
    unsigned int lsn, size;

    if (!pc_disc_find(name, &lsn, &size)) {
        fprintf(stderr, "cdvd: '%s' not found on the disc\n", name);
        return 0;
    }
    fp->lsn = lsn;
    fp->size = size;
    return 1;
}

int sceCdRead(u_int lsn, u_int sectors, void *buf, sceCdRMode *mode)
{
    cd_error = pc_disc_read(lsn, sectors, buf) ? 0 : 0x32; /* SCECdErREAD */
    return 1;
}

int sceCdReadClock(sceCdCLOCK *rtc)
{
    /* The PS2 clock is in BCD. */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
#define BCD(v) ((u_char)((((v) / 10) << 4) | ((v) % 10)))
    memset(rtc, 0, sizeof(*rtc));
    rtc->second = BCD(t->tm_sec);
    rtc->minute = BCD(t->tm_min);
    rtc->hour = BCD(t->tm_hour);
    rtc->day = BCD(t->tm_mday);
    rtc->month = BCD(t->tm_mon + 1);
    rtc->year = BCD(t->tm_year % 100);
#undef BCD
    return 1;
}

/* Streaming is only used by the movie player, replaced as a whole. */
int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr) { return 1; }
int sceCdStStart(u_int lsn, sceCdRMode *mode) { return 1; }

int sceFsReset(void) { return 0; }

/* ---- IOP communication ------------------------------------------------- */

void sceSifInitRpc(u_int mode) {}
int sceSifRebootIop(const char *img) { return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifInitIopHeap(void) { return 0; }
int sceSifLoadModule(const char *filename, int args, const char *argp) { return 1; }

void *sceSifAllocIopHeap(u_int size)
{
    /* Addresses in IOP memory are never dereferenced on the EE side. */
    static unsigned int next = 0x100000;
    unsigned int addr = next;
    next += (size + 63) & ~63u;
    return (void *)addr;
}

static int sif_serve_dummy;

int sceSifBindRpc(sceSifClientData *bd, u_int request, u_int mode)
{
    bd->command = request;
    bd->serve = (sceSifServeData *)&sif_serve_dummy;
    return 0;
}

int sceSifCallRpc(sceSifClientData *bd, u_int fno, u_int mode, void *send, int ssize,
                  void *receive, int rsize, sceSifEndFunc end_function, void *end_param)
{
    /* TODO: route the sound driver's requests to a PC sound driver. For now
     * every call completes at once without doing anything. */
    if (end_function != NULL)
        end_function(end_param);
    return 0;
}

int sceSifGetOtherData(sceSifReceiveData *rd, void *src, void *dest, int size, u_int mode)
{
    return 0;
}

u_int sceSifSetDma(sceSifDmaData *sdd, int len) { return 1; }
u_int isceSifSetDma(sceSifDmaData *sdd, int len) { return 1; }
int sceSifDmaStat(u_int id) { return -1; /* transfer finished */ }

/* ---- Sound libraries on the IOP --------------------------------------- */

int sceSdRemoteInit(void) { return 0; }
int sceSSyn_SetOutputMode(int mode) { return 0; }

/* ---- GS and DMA -------------------------------------------------------- */

static int field;

int sceGsResetGraph(short mode, short inter, short omode, short ffmode) { return 0; }
void sceGsResetPath(void) {}
int sceGsSyncPath(int mode, u_short timeout) { return 0; }

int sceGsSyncV(int mode)
{
    pc_vblank();
    field ^= 1;
    return field;
}

void sceGsSetDefDBuffDc(sceGsDBuffDc *db, short psm, short w, short h,
                        short ztest, short zpsm, short clear)
{
    memset(db, 0, sizeof(*db));
}

int sceGsSwapDBuffDc(sceGsDBuffDc *db, int id) { return 0; }

int sceGsSetDefLoadImage(sceGsLoadImage *lp, short dbp, short dbw, short dpsm,
                         short x, short y, short w, short h)
{
    memset(lp, 0, sizeof(*lp));
    return 0;
}

int sceGsExecLoadImage(sceGsLoadImage *lp, u_long128 *srcaddr) { return 0; }

int sceGsSetDefStoreImage(sceGsStoreImage *sp, short sbp, short sbw, short spsm,
                          short x, short y, short w, short h)
{
    memset(sp, 0, sizeof(*sp));
    return 0;
}

int sceGsExecStoreImage(sceGsStoreImage *sp, u_long128 *dstaddr) { return 0; }

int sceDmaReset(int mode) { return 0; }
void sceDmaSend(sceDmaChan *d, void *tag) {}
int sceDmaSync(sceDmaChan *d, int mode, int timeout) { return 0; }
void sceVpu0Reset(void) {}

/* ---- Controllers ------------------------------------------------------- */

/* Buttons are active low on the PS2: a bit is 0 while the button is held. */
unsigned short pc_pad_buttons = 0xffff;
unsigned char pc_pad_sticks[4] = { 0x80, 0x80, 0x80, 0x80 };

int scePadInit(int mode) { return 1; }
int scePadPortOpen(int port, int slot, u_long128 *addr) { return 1; }
int scePadPortClose(int port, int slot) { return 1; }

int scePadGetState(int port, int slot)
{
    return port == 0 ? scePadStateStable : scePadStateDisconn;
}

int scePadGetReqState(int port, int slot) { return 0; /* scePadReqStateComplete */ }

int scePadInfoMode(int port, int slot, int term, int offs)
{
    if (term == 1) /* InfoModeCurID: DualShock */
        return 7;
    if (term == 2) /* InfoModeCurExID */
        return 7;
    return 0;
}

int scePadSetMainMode(int port, int slot, int offs, int lock) { return 1; }
int scePadInfoPressMode(int port, int slot) { return 1; }
int scePadEnterPressMode(int port, int slot) { return 1; }
int scePadSetActDirect(int port, int slot, const u_char *data) { return 1; }
int scePadSetActAlign(int port, int slot, const u_char *data) { return 1; }

int scePadRead(int port, int slot, u_char *rdata)
{
    memset(rdata, 0, 32);
    rdata[1] = 0x79; /* DualShock 2, pressure mode */
    rdata[2] = pc_pad_buttons >> 8;
    rdata[3] = pc_pad_buttons & 0xff;
    memcpy(rdata + 4, pc_pad_sticks, 4);
    return 32;
}

/* ---- Memory cards ------------------------------------------------------ */

/* TODO: store saves in files. For now no card is ever inserted. */
static int mc_result;

int sceMcInit(void) { return 0; }

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    *type = 0;
    *free = 0;
    *format = 0;
    mc_result = -10; /* no card */
    return 0;
}

int sceMcOpen(int port, int slot, const char *name, int mode) { mc_result = -4; return 0; }
int sceMcClose(int fd) { mc_result = 0; return 0; }
int sceMcRead(int fd, void *buff, int size) { mc_result = -10; return 0; }
int sceMcWrite(int fd, const void *buff, int size) { mc_result = -10; return 0; }
int sceMcMkdir(int port, int slot, const char *name) { mc_result = -10; return 0; }
int sceMcChdir(int port, int slot, const char *newDir, char *oldDir) { mc_result = -10; return 0; }
int sceMcFormat(int port, int slot) { mc_result = -10; return 0; }

int sceMcGetDir(int port, int slot, const char *name, unsigned mode, int maxent,
                sceMcTblGetDir *table)
{
    mc_result = 0;
    return 0;
}

int sceMcSync(int mode, int *cmd, int *result)
{
    if (cmd != NULL)
        *cmd = 0;
    if (result != NULL)
        *result = mc_result;
    return 1; /* sceMcExecFinish */
}
