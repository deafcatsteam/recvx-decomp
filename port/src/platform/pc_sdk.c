/*
 * PC replacements for the rest of the Sony PS2 libraries the game calls:
 * DVD, IOP communication, GS setup, DMA, controllers and memory cards.
 *
 * DVD reads are served from the disc image (pc_disc.c); the IOP side lives in
 * pc_iop.c. The controller is
 * reported as a DualShock 2 whose state comes from pc_pad (filled by the
 * window/input layer), and the memory card is a folder (pc_memcard.c). The
 * rest are placeholders that report success, so the game's init sequences
 * run through; graphics and sound get real implementations in their own
 * layers.
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
#include "../host/pc_memcard.h"
#include "pc_platform.h"
#include "../gs/gs.h"
#include "../gs/gs_mem.h"

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
        /* The game retries forever, as a PS2 would wait for the right disc. */
        fprintf(stderr, "cdvd: '%s' not found on the disc. Is CVX_ISO an image of "
                        "Resident Evil Code: Veronica X (SLUS-20184)?\n", name);
        exit(1);
    }
    fp->lsn = lsn;
    fp->size = size;
    /* The name without its directories, like "MV_000.PSS;1". */
    {
        const char *base = strrchr(name, '\\');

        base = base != NULL ? base + 1 : name;
        strncpy(fp->name, base, sizeof(fp->name) - 1);
        fp->name[sizeof(fp->name) - 1] = 0;
    }
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

/* ---- IOP communication: see pc_iop.c ---------------------------------- */

/* ---- Sound libraries on the IOP --------------------------------------- */

int sceSdRemoteInit(void) { return 0; }
int sceSSyn_SetOutputMode(int mode) { return 0; }

/* ---- GS and DMA -------------------------------------------------------- */

/*
 * The libgraph structures are GIF packets: a GIF tag followed by
 * (value, register address) pairs. They are filled the way libgraph does and
 * sent to the software GS (port/src/gs) as they are.
 */

static int field;

static void put64(void *field_addr, uint64_t v)
{
    memcpy(field_addr, &v, 8);
}

static uint64_t get64(const void *field_addr)
{
    uint64_t v;
    memcpy(&v, field_addr, 8);
    return v;
}

static uint64_t gif_tag(int nloop, int eop, int flg, int nreg, uint64_t regs, uint64_t *hi)
{
    *hi = regs;
    return (uint64_t)nloop | ((uint64_t)eop << 15) | ((uint64_t)flg << 58) | ((uint64_t)nreg << 60);
}

int sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    gs_reset();
    return 0;
}

void sceGsResetPath(void) {}
int sceGsSyncPath(int mode, u_short timeout) { return 0; }

int sceGsSyncV(int mode)
{
    pc_wait_vblank();
    field ^= 1;
    return field;
}

/* A drawing environment: 8 (value, address) pairs for one context. */
static void set_draw_env(uint64_t *pairs, int ctx, uint32_t fbp, short psm, short w, short h,
                         uint32_t zbp, short ztest, short zpsm)
{
    uint64_t v[8] = {
        fbp | ((uint64_t)((w + 63) / 64) << 16) | ((uint64_t)psm << 24),
        zbp | ((uint64_t)(zpsm & 0xf) << 24) | ((uint64_t)(ztest == 0) << 32),
        ((uint64_t)(2048 - (w >> 1)) << 4) | ((uint64_t)(2048 - (h >> 1)) << 36),
        ((uint64_t)(w - 1) << 16) | ((uint64_t)(h - 1) << 48),
        1, /* PRMODECONT: use the PRIM register */
        1, /* COLCLAMP */
        0, /* DTHE */
        (1ull << 16) | ((uint64_t)(ztest ? ztest : 1) << 17),
    };
    int addr[8] = { 0x4c + ctx, 0x4e + ctx, 0x18 + ctx, 0x40 + ctx, 0x1a, 0x46, 0x45, 0x47 + ctx };

    for (int i = 0; i < 8; i++) {
        pairs[i * 2] = v[i];
        pairs[i * 2 + 1] = addr[i];
    }
}

/* A clear: sprite over the whole buffer with the depth test always passing. */
static void set_clear(uint64_t *pairs, short w, short h, short ztest)
{
    uint64_t ofx = (uint64_t)(2048 - (w >> 1)) << 4, ofy = (uint64_t)(2048 - (h >> 1)) << 4;
    uint64_t v[6] = {
        (1ull << 16) | (1ull << 17),                           /* TEST: ZTE, ALWAYS */
        6,                                                     /* PRIM: sprite */
        0x3f80000000000000ull,                                 /* RGBAQ: black, Q 1 */
        ofx | (ofy << 16),                                     /* XYZ2 */
        (ofx + ((uint64_t)w << 4)) | ((ofy + ((uint64_t)h << 4)) << 16),
        (1ull << 16) | ((uint64_t)(ztest ? ztest : 1) << 17),  /* TEST restored */
    };
    int addr[6] = { 0x47, 0x00, 0x01, 0x05, 0x05, 0x47 };

    for (int i = 0; i < 6; i++) {
        pairs[i * 2] = v[i];
        pairs[i * 2 + 1] = addr[i];
    }
}

void sceGsSetDefDBuffDc(sceGsDBuffDc *db, short psm, short w, short h,
                        short ztest, short zpsm, short clear)
{
    uint32_t pages = ((w + 63) / 64) * ((h + 31) / 32);
    uint32_t buf[2] = { 0, pages }, zbp = pages * 2;
    int nloop = clear ? 22 : 16;
    uint64_t hi;

    memset(db, 0, sizeof(*db));
    for (int i = 0; i < 2; i++) {
        /* disp[i] shows the buffer that is not being drawn into. */
        put64(&db->disp[i].pmode, 1 | (1 << 2) | (1 << 5) | (0xff << 8));
        put64(&db->disp[i].smode2, 1 | (1 << 1));
        put64(&db->disp[i].dispfb, buf[i ^ 1] | ((uint64_t)((w + 63) / 64) << 9) | ((uint64_t)psm << 15));
        put64(&db->disp[i].display, 636 | (50 << 12) | ((uint64_t)((2560 + w - 1) / w - 1) << 23) |
                                        (2559ull << 32) | ((uint64_t)(h - 1) << 44));
    }

    uint64_t *g0 = (uint64_t *)&db->giftag0, *g1 = (uint64_t *)&db->giftag1;
    g0[0] = gif_tag(nloop, 1, 0, 1, 0xe, &hi);
    g0[1] = hi;
    g1[0] = g0[0];
    g1[1] = hi;
    set_draw_env((uint64_t *)&db->draw01, 0, buf[0], psm, w, h, zbp, ztest, zpsm);
    set_draw_env((uint64_t *)&db->draw02, 1, buf[0], psm, w, h, zbp, ztest, zpsm);
    set_draw_env((uint64_t *)&db->draw11, 0, buf[1], psm, w, h, zbp, ztest, zpsm);
    set_draw_env((uint64_t *)&db->draw12, 1, buf[1], psm, w, h, zbp, ztest, zpsm);
    set_clear((uint64_t *)&db->clear0, w, h, ztest);
    set_clear((uint64_t *)&db->clear1, w, h, ztest);
}

int sceGsSwapDBuffDc(sceGsDBuffDc *db, int id)
{
    const uint64_t *tag = (const uint64_t *)(id == 0 ? &db->giftag0 : &db->giftag1);

    id &= 1;
    gs_set_display(get64(&db->disp[id].dispfb), get64(&db->disp[id].display));
    /* The drawing environment follows its GIF tag in the structure. */
    gs_gif_write(tag, 1 + (uint32_t)(tag[0] & 0x7fff));
    return field;
}

int sceGsSetDefLoadImage(sceGsLoadImage *lp, short dbp, short dbw, short dpsm,
                         short x, short y, short w, short h)
{
    uint64_t *q = (uint64_t *)lp, hi;
    int bits = dpsm == SCE_GS_PSMT4 || dpsm == SCE_GS_PSMT4HL || dpsm == SCE_GS_PSMT4HH ? 4
             : dpsm == SCE_GS_PSMT8 || dpsm == SCE_GS_PSMT8H ? 8
             : dpsm == SCE_GS_PSMCT16 || dpsm == SCE_GS_PSMCT16S ? 16
             : dpsm == SCE_GS_PSMCT24 ? 24 : 32;

    memset(lp, 0, sizeof(*lp));
    q[0] = gif_tag(4, 0, 0, 1, 0xe, &hi);
    q[1] = hi;
    q[2] = ((uint64_t)dbp << 32) | ((uint64_t)dbw << 48) | ((uint64_t)dpsm << 56);
    q[3] = 0x50;
    q[4] = ((uint64_t)x << 32) | ((uint64_t)y << 48);
    q[5] = 0x51;
    q[6] = (uint64_t)w | ((uint64_t)h << 32);
    q[7] = 0x52;
    q[8] = 0;
    q[9] = 0x53;
    q[10] = gif_tag(((int)w * h * bits / 8 + 15) / 16, 1, 2, 0, 0, &hi);
    q[11] = hi;
    return 0;
}

int sceGsExecLoadImage(sceGsLoadImage *lp, u_long128 *srcaddr)
{
    const uint64_t *q = (const uint64_t *)lp;

    gs_gif_write(q, 6);
    gs_gif_write((const uint64_t *)srcaddr, (uint32_t)(q[10] & 0x7fff));
    return 0;
}

/* Local -> host transfers: the structure is kept and read back directly. */
int sceGsSetDefStoreImage(sceGsStoreImage *sp, short sbp, short sbw, short spsm,
                          short x, short y, short w, short h)
{
    memset(sp, 0, sizeof(*sp));
    put64(&sp->bitbltbuf, (uint64_t)sbp | ((uint64_t)sbw << 16) | ((uint64_t)spsm << 24));
    put64(&sp->trxpos, (uint64_t)x | ((uint64_t)y << 16));
    put64(&sp->trxreg, (uint64_t)w | ((uint64_t)h << 32));
    return 0;
}

int sceGsExecStoreImage(sceGsStoreImage *sp, u_long128 *dstaddr)
{
    uint64_t bb = get64(&sp->bitbltbuf), pos = get64(&sp->trxpos), reg = get64(&sp->trxreg);
    int psm = (bb >> 24) & 0x3f, bp = bb & 0x3fff, bw = (bb >> 16) & 0x3f;
    int x0 = pos & 0x7ff, y0 = (pos >> 16) & 0x7ff, w = reg & 0xfff, h = (reg >> 32) & 0xfff;
    uint8_t *out = (uint8_t *)dstaddr;
    int nib = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = gs_read_pixel(psm, bp, bw, x0 + x, y0 + y);
            switch (psm) {
            case SCE_GS_PSMCT32: memcpy(out, &v, 4); out += 4; break;
            case SCE_GS_PSMCT24: memcpy(out, &v, 3); out += 3; break;
            case SCE_GS_PSMCT16: case SCE_GS_PSMCT16S: memcpy(out, &v, 2); out += 2; break;
            case SCE_GS_PSMT8: *out++ = (uint8_t)v; break;
            default:
                if (nib)
                    *out++ |= (uint8_t)(v << 4);
                else
                    *out = (uint8_t)(v & 15);
                nib ^= 1;
                break;
            }
        }
    }
    return 0;
}

int sceDmaReset(int mode) { return 0; }
void sceDmaSend(sceDmaChan *d, void *tag) {}
int sceDmaSync(sceDmaChan *d, int mode, int timeout) { return 0; }
void sceVpu0Reset(void) {}

/* ---- Controllers ------------------------------------------------------- */

/* Buttons are active low on the PS2: a bit is 0 while the button is held. */
unsigned short pc_pad_buttons = 0xffff;
unsigned char pc_pad_sticks[4] = { 0x80, 0x80, 0x80, 0x80 };
/* Vibration, as the game aligns it: small motor (on/off), big motor
 * (0-255). pc_window.c passes it on to the controllers. */
unsigned char pc_pad_motor[2];
unsigned int pc_pad_motor_sets;

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
int scePadSetActDirect(int port, int slot, const u_char *data)
{
    if (port == 0) {
        pc_pad_motor[0] = data[0];
        pc_pad_motor[1] = data[1];
        pc_pad_motor_sets++;
    }
    return 1;
}

int scePadSetActAlign(int port, int slot, const u_char *data) { return 1; }

int scePadRead(int port, int slot, u_char *rdata)
{
    memset(rdata, 0, 32);
    rdata[1] = 0x79; /* DualShock 2, pressure mode */
    rdata[2] = pc_pad_buttons >> 8;
    rdata[3] = pc_pad_buttons & 0xff;
    memcpy(rdata + 4, pc_pad_sticks, 4);

    /* Pressure of right, left, up, down, triangle, circle, cross, square,
     * L1, R1, L2 and R2: full when held, as on keyboard there is no
     * in-between. */
    static const unsigned short pressure_bits[12] = {
        0x2000, 0x8000, 0x1000, 0x4000, 0x0010, 0x0020,
        0x0040, 0x0080, 0x0004, 0x0008, 0x0001, 0x0002,
    };
    for (int i = 0; i < 12; i++)
        rdata[8 + i] = (pc_pad_buttons & pressure_bits[i]) ? 0x00 : 0xff;
    return 32;
}

/* ---- Memory cards ------------------------------------------------------ */

/* Each call registers its work and finishes it at once (pc_memcard.c keeps
 * the card in a folder); sceMcSync then reports it finished, once, and
 * "idle" afterwards, as the PS2 library does. The game's card check runs
 * while the library is idle. */
static int mc_pending, mc_cmd, mc_result;
static int mc_seen[2];

static int mc_done(int cmd, int result)
{
    mc_pending = 1;
    mc_cmd = cmd;
    mc_result = result;
    return 0;
}

int sceMcInit(void) { return 0; }

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    int card = port >= 0 && port < 2 && pc_mc_card(port);
    int result = PC_MC_NOCARD;

    /* The game passes NULL for the values it does not need. */
    if (type != NULL)
        *type = card ? 2 : 0; /* PS2 card */
    if (free != NULL)
        *free = card ? pc_mc_free_kb(port) : 0;
    if (format != NULL)
        *format = card ? 1 : 0;
    if (card) {
        result = mc_seen[port] ? 0 : -1; /* -1: a card newly seen */
        mc_seen[port] = 1;
    }
    return mc_done(sceMcFuncNoCardInfo, result);
}

int sceMcOpen(int port, int slot, const char *name, int mode)
{
    return mc_done(sceMcFuncNoOpen, pc_mc_open(port, name, mode));
}

int sceMcClose(int fd) { return mc_done(sceMcFuncNoClose, pc_mc_close(fd)); }

int sceMcRead(int fd, void *buff, int size)
{
    return mc_done(sceMcFuncNoRead, pc_mc_read(fd, buff, size));
}

int sceMcWrite(int fd, const void *buff, int size)
{
    return mc_done(sceMcFuncNoWrite, pc_mc_write(fd, buff, size));
}

int sceMcMkdir(int port, int slot, const char *name)
{
    return mc_done(sceMcFuncNoMkdir, pc_mc_mkdir(port, name));
}

int sceMcChdir(int port, int slot, const char *newDir, char *oldDir)
{
    return mc_done(sceMcFuncNoChDir, pc_mc_chdir(port, newDir, oldDir));
}

int sceMcDelete(int port, int slot, const char *name)
{
    return mc_done(sceMcFuncNoDelete, pc_mc_delete(port, name));
}

int sceMcFormat(int port, int slot)
{
    /* The card always reads as formatted; never erase the player's saves. */
    return mc_done(sceMcFuncNoFormat, pc_mc_card(port) ? 0 : PC_MC_NOCARD);
}

static void mc_date(sceMcStDateTime *d, int64_t mtime)
{
    time_t t = (time_t)mtime;
    struct tm *tm = mtime != 0 ? localtime(&t) : NULL;

    memset(d, 0, sizeof(*d));
    if (tm != NULL) {
        d->Sec = tm->tm_sec;
        d->Min = tm->tm_min;
        d->Hour = tm->tm_hour;
        d->Day = tm->tm_mday;
        d->Month = tm->tm_mon + 1;
        d->Year = tm->tm_year + 1900;
    }
}

int sceMcGetDir(int port, int slot, const char *name, unsigned mode, int maxent,
                sceMcTblGetDir *table)
{
    PcMcEntry list[64];
    int n, i;

    if (maxent > 64)
        maxent = 64;
    n = pc_mc_getdir(port, name, maxent, list);
    for (i = 0; i < n; i++) {
        sceMcTblGetDir *t = &table[i];

        memset(t, 0, sizeof(*t));
        mc_date(&t->_Create, list[i].mtime);
        mc_date(&t->_Modify, list[i].mtime);
        t->FileSizeByte = list[i].size;
        /* exists, closed, readable, writable, executable; file or folder */
        t->AttrFile = 0x8087 | (list[i].is_dir ? 0x20 : 0x10);
        memcpy(t->EntryName, list[i].name, sizeof(t->EntryName));
    }
    return mc_done(sceMcFuncNoGetDir, pc_mc_card(port) ? n : PC_MC_NOCARD);
}

int sceMcSync(int mode, int *cmd, int *result)
{
    if (!mc_pending)
        return -1; /* sceMcExecIdle */
    mc_pending = 0;
    if (cmd != NULL)
        *cmd = mc_cmd;
    if (result != NULL)
        *result = mc_result;
    return 1; /* sceMcExecFinish */
}
