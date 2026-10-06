/*
 * Software GS: GIF packet parsing, register state, image transfers and the
 * pixel pipeline (see gs.h). References: GS User's Manual (registers,
 * drawing and texture functions) and the EE User's Manual (GIF, DMA).
 */
#include "gs.h"
#include "gs_gpu.h"
#include "gs_mem.h"
#include "gs_priv.h"
#include "gs_threads.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GsStats gs_stats;
int64_t (*gs_clock_ns)(void);

/* Extracts bits [lo, lo+n) of v. */
#define BITS(v, lo, n) ((uint32_t)(((v) >> (lo)) & ((1ull << (n)) - 1)))

/* ---- Register state ---------------------------------------------------- */

static struct {
    uint64_t prim, prmode, prmodecont;
    uint64_t rgbaq, st, uv, fog;
    float q;
    uint64_t tex0[2], clamp[2], tex1[2], xyoffset[2], scissor[2], alpha[2];
    uint64_t test[2], fba[2], frame[2], zbuf[2];
    uint64_t texa, fogcol, texclut, dthe, colclamp, pabe, dimx;
    uint64_t bitbltbuf, trxpos, trxreg, trxdir;
    uint64_t dispfb, display;

    Vertex queue[3];
    int queued;     /* vertices waiting for a primitive */
    int strip_odd;

    uint32_t clut[512]; /* current CLUT as raw CT32/CT16 values */
    uint32_t cbp[2];    /* CLUT buffer pointers latched by CLD 2..5 */
} gs;

/* Image transfer in progress (host -> local). */
static struct {
    int active;
    int psm;
    uint32_t bp, bw;
    int x0, y0, w, h, x, y;
    uint8_t pending[16];
    int pending_n;
} trx;

/* GIF state across DMA transfers. */
static struct {
    uint64_t tag_lo, tag_hi;
    uint32_t nloop, nreg, reg, flg;
    int in_tag;
} gif;

/* Primitives waiting to be drawn (see "Batches"). */
static int batch_touches(int psm, uint32_t bp, uint32_t bw, int x0, int y0, int x1, int y1, int reads);
static void batch_flush(void);

/* ---- Helpers ----------------------------------------------------------- */

static int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static uint32_t rgba(int r, int g, int b, int a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

/* Converts a stored CT16 value to RGBA with the TEXA alpha rules. */
static uint32_t expand16(uint32_t c, int use_texa)
{
    int r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
    int a;

    if (!use_texa) {
        a = (c & 0x8000) ? 0x80 : 0;
    } else if (c & 0x8000) {
        a = BITS(gs.texa, 32, 8);
    } else {
        a = (BITS(gs.texa, 15, 1) && (c & 0x7fff) == 0) ? 0 : BITS(gs.texa, 0, 8);
    }
    return rgba(r, g, b, a);
}

/* ---- CLUT -------------------------------------------------------------- */

static uint64_t clut_gen = 1; /* counts CLUT loads */
static uint64_t clut_hash;

static void load_clut(uint64_t tex0)
{
    int psm = BITS(tex0, 20, 6);
    uint32_t cbp = BITS(tex0, 37, 14);
    int cpsm = BITS(tex0, 51, 4);
    int csm = BITS(tex0, 55, 1);
    int cld = BITS(tex0, 61, 3);
    int entries;

    switch (cld) {
    case 0: return;
    case 1: break;
    case 2: gs.cbp[0] = cbp; break;
    case 3: gs.cbp[1] = cbp; break;
    case 4: if (gs.cbp[0] == cbp) return; gs.cbp[0] = cbp; break;
    case 5: if (gs.cbp[1] == cbp) return; gs.cbp[1] = cbp; break;
    default: return;
    }

    if (psm == GS_PSMT8 || psm == GS_PSMT8H)
        entries = 256;
    else if (psm == GS_PSMT4 || psm == GS_PSMT4HL || psm == GS_PSMT4HH)
        entries = 16;
    else
        return;

    clut_gen++; /* decoded indexed textures use the CLUT they were made with */
    if (gs_gpu_on) {
        uint32_t cbw = BITS(gs.texclut, 0, 6), cou = BITS(gs.texclut, 6, 6) * 16;
        uint32_t cov = BITS(gs.texclut, 12, 10);
        int cpsm_ = cpsm == 0 ? GS_PSMCT32 : cpsm;
        if (csm == 0)
            gs_gpu_sync(cpsm_, cbp, 1, 0, 0, 15, 15);
        else
            gs_gpu_sync(cpsm_, cbp, cbw, cou, cov, cou + entries - 1, cov);
    }
    {
        /* A CLUT drawn by primitives still waiting is read once they are. */
        int cpsm_ = cpsm == 0 ? GS_PSMCT32 : cpsm;
        uint32_t cbw = BITS(gs.texclut, 0, 6), cou = BITS(gs.texclut, 6, 6) * 16;
        uint32_t cov = BITS(gs.texclut, 12, 10);
        if (csm == 0 ? batch_touches(cpsm_, cbp, 1, 0, 0, 15, 15, 0)
                     : batch_touches(cpsm_, cbp, cbw, cou, cov, cou + entries - 1, cov, 0))
            batch_flush();
    }
    if (csm == 0) {
        /* CSM1: 8x2 blocks of entries; for 256 colours, bits 3 and 4 of the
         * index are swapped in the 16x16 arrangement. */
        for (int i = 0; i < entries; i++) {
            int x, y;
            if (entries == 256) {
                int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
                x = j & 15;
                y = j >> 4;
            } else {
                x = i & 7;
                y = i >> 3;
            }
            gs.clut[i] = gs_read_pixel_nosync(cpsm == 0 ? GS_PSMCT32 : cpsm, cbp, 1, x, y);
        }
    } else {
        /* CSM2: a plain row at TEXCLUT's offset. */
        uint32_t cbw = BITS(gs.texclut, 0, 6), cou = BITS(gs.texclut, 6, 6) * 16;
        uint32_t cov = BITS(gs.texclut, 12, 10);
        for (int i = 0; i < entries; i++)
            gs.clut[i] = gs_read_pixel_nosync(cpsm == 0 ? GS_PSMCT32 : cpsm, cbp, cbw, cou + i, cov);
    }
    /* What the CLUT holds, for the GPU's texture cache (FNV-1a). */
    clut_hash = 14695981039346656037ull;
    for (int i = 0; i < entries; i++)
        clut_hash = (clut_hash ^ gs.clut[i]) * 1099511628211ull;
}

/* ---- Image transfers ---------------------------------------------------- */

/* Counts changes to GS memory and display settings, so the display is only
 * converted again when it may have changed. */
static uint32_t changes, shown_changes = ~0u;

static void trx_start(void)
{
    changes++;
    int dir = BITS(gs.trxdir, 0, 2);

    trx.active = 0;
    if (dir != 0 && dir != 2)
        batch_flush(); /* local -> host: reads what was drawn */
    if (dir == 0) {
        trx.active = 1;
        trx.psm = BITS(gs.bitbltbuf, 56, 6);
        trx.bp = BITS(gs.bitbltbuf, 32, 14);
        trx.bw = BITS(gs.bitbltbuf, 48, 6);
        trx.x0 = BITS(gs.trxpos, 32, 11);
        trx.y0 = BITS(gs.trxpos, 48, 11);
        trx.w = BITS(gs.trxreg, 0, 12);
        trx.h = BITS(gs.trxreg, 32, 12);
        trx.x = trx.y = 0;
        trx.pending_n = 0;
        gs_stats.uploads++;
        if (gs_gpu_on)
            gs_gpu_sync(trx.psm, trx.bp, trx.bw, trx.x0, trx.y0, trx.x0 + trx.w - 1, trx.y0 + trx.h - 1);
        /* Primitives still waiting that read or draw this memory are drawn
         * before it changes. */
        if (trx.x0 + trx.w > 2048 || trx.y0 + trx.h > 2048 ||
            batch_touches(trx.psm, trx.bp, trx.bw, trx.x0, trx.y0, trx.x0 + trx.w - 1,
                          trx.y0 + trx.h - 1, 1))
            batch_flush();
    } else if (dir == 2) {
        /* Local -> local copy, through a temporary buffer. */
        int spsm = BITS(gs.bitbltbuf, 24, 6), dpsm = BITS(gs.bitbltbuf, 56, 6);
        uint32_t sbp = BITS(gs.bitbltbuf, 0, 14), sbw = BITS(gs.bitbltbuf, 16, 6);
        uint32_t dbp = BITS(gs.bitbltbuf, 32, 14), dbw = BITS(gs.bitbltbuf, 48, 6);
        int sx = BITS(gs.trxpos, 0, 11), sy = BITS(gs.trxpos, 16, 11);
        int dx = BITS(gs.trxpos, 32, 11), dy = BITS(gs.trxpos, 48, 11);
        int w = BITS(gs.trxreg, 0, 12), h = BITS(gs.trxreg, 32, 12);
        static uint32_t tmp[2048 * 64];

        if (gs_gpu_on) {
            gs_gpu_sync(spsm, sbp, sbw, sx, sy, sx + w - 1, sy + h - 1);
            gs_gpu_sync(dpsm, dbp, dbw, dx, dy, dx + w - 1, dy + h - 1);
        }

        if (sx + w > 2048 || sy + h > 2048 || dx + w > 2048 || dy + h > 2048 ||
            batch_touches(spsm, sbp, sbw, sx, sy, sx + w - 1, sy + h - 1, 0) ||
            batch_touches(dpsm, dbp, dbw, dx, dy, dx + w - 1, dy + h - 1, 1))
            batch_flush();

        for (int y0 = 0; y0 < h; y0 += 64) {
            int rows = h - y0 < 64 ? h - y0 : 64;
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < w && x < 2048; x++)
                    tmp[y * 2048 + x] = gs_read_pixel_nosync(spsm, sbp, sbw, (sx + x) & 2047, (sy + y0 + y) & 2047);
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < w && x < 2048; x++)
                    gs_write_pixel_nosync(dpsm, dbp, dbw, (dx + x) & 2047, (dy + y0 + y) & 2047, tmp[y * 2048 + x]);
        }
    }
}

static void trx_put(uint32_t v)
{
    if (trx.y >= trx.h)
        return;
    gs_write_pixel_nosync(trx.psm, trx.bp, trx.bw, (trx.x0 + trx.x) & 2047, (trx.y0 + trx.y) & 2047, v);
    if (++trx.x >= trx.w) {
        trx.x = 0;
        if (++trx.y >= trx.h)
            trx.active = 0;
    }
}

/* Consumes image data: len bytes (a multiple of 4, or any length for the
 * 24-bit formats, whose pixels straddle quadwords). */
static void trx_bytes(const uint8_t *q, int len)
{
    int i;

    if (!trx.active)
        return;
    switch (trx.psm) {
    case GS_PSMCT32: case GS_PSMZ32:
        for (i = 0; i + 4 <= len; i += 4) {
            uint32_t v;
            memcpy(&v, q + i, 4);
            trx_put(v);
        }
        break;
    case GS_PSMCT24: case GS_PSMZ24:
        for (i = 0; i < len; i++) {
            trx.pending[trx.pending_n++] = q[i];
            if (trx.pending_n == 3) {
                trx_put(trx.pending[0] | (trx.pending[1] << 8) | (trx.pending[2] << 16));
                trx.pending_n = 0;
            }
        }
        break;
    case GS_PSMCT16: case GS_PSMCT16S: case GS_PSMZ16: case GS_PSMZ16S:
        for (i = 0; i + 2 <= len; i += 2)
            trx_put(q[i] | (q[i + 1] << 8));
        break;
    case GS_PSMT8: case GS_PSMT8H:
        for (i = 0; i < len; i++)
            trx_put(q[i]);
        break;
    case GS_PSMT4: case GS_PSMT4HL: case GS_PSMT4HH:
        for (i = 0; i < len; i++) {
            trx_put(q[i] & 15);
            trx_put(q[i] >> 4);
        }
        break;
    }
}

/* ---- Drawing state ------------------------------------------------------ */

/* Register fields decoded once per primitive: DrawState, in gs_priv.h. */

/* The pixel loops are compiled once per combination of these settings, so
 * the most common cases run without testing them for every pixel. */
enum { K_TME = 1, K_ABE = 2, K_ZTE = 4, K_CT32 = 8, K_BIL = 16 };
#define ALWAYS_INLINE inline __attribute__((always_inline))
#define KEY_CASES(F) \
    F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15) \
    F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31)

static DrawState ds;

static void tc_bind(void);

static void setup_draw(void)
{
    uint64_t attrs = BITS(gs.prmodecont, 0, 1) ? gs.prim : gs.prmode;
    int ctx = BITS(attrs, 9, 1);
    uint64_t test, alpha, tex0 = gs.tex0[ctx], clamp = gs.clamp[ctx];

    ds.ctx = ctx;
    ds.iip = BITS(attrs, 3, 1);
    ds.tme = BITS(attrs, 4, 1);
    ds.fge = BITS(attrs, 5, 1);
    ds.abe = BITS(attrs, 6, 1);
    ds.fst = BITS(attrs, 8, 1);

    ds.fbp = BITS(gs.frame[ctx], 0, 9) * 32;
    ds.fbw = BITS(gs.frame[ctx], 16, 6);
    ds.fpsm = BITS(gs.frame[ctx], 24, 6);
    ds.fbmsk = BITS(gs.frame[ctx], 32, 32);
    ds.fbmsk16 = ((ds.fbmsk >> 3) & 31) | (((ds.fbmsk >> 11) & 31) << 5) |
                 (((ds.fbmsk >> 19) & 31) << 10) | ((ds.fbmsk >> 31) << 15);
    ds.zbp = BITS(gs.zbuf[ctx], 0, 9) * 32;
    ds.zpsm = BITS(gs.zbuf[ctx], 24, 4) | 0x30;
    ds.zmsk = BITS(gs.zbuf[ctx], 32, 1);
    ds.scax0 = BITS(gs.scissor[ctx], 0, 11);
    ds.scax1 = BITS(gs.scissor[ctx], 16, 11);
    ds.scay0 = BITS(gs.scissor[ctx], 32, 11);
    ds.scay1 = BITS(gs.scissor[ctx], 48, 11);
    ds.tfx = BITS(tex0, 35, 2);
    ds.tcc = BITS(tex0, 34, 1);
    ds.fba = BITS(gs.fba[ctx], 0, 1);
    ds.colclamp = BITS(gs.colclamp, 0, 1);
    ds.pabe = BITS(gs.pabe, 0, 1);
    ds.fr = BITS(gs.fogcol, 0, 8);
    ds.fg = BITS(gs.fogcol, 8, 8);
    ds.fb_ = BITS(gs.fogcol, 16, 8);

    test = gs.test[ctx];
    ds.ate = BITS(test, 0, 1);
    ds.atst = BITS(test, 1, 3);
    ds.aref = BITS(test, 4, 8);
    ds.afail = BITS(test, 12, 2);
    ds.date = BITS(test, 14, 1) && ds.fpsm != GS_PSMCT24;
    ds.datm = BITS(test, 15, 1);
    ds.zte = BITS(test, 16, 1);
    ds.ztst = BITS(test, 17, 2);
    ds.zmax = (ds.zpsm == GS_PSMZ16 || ds.zpsm == GS_PSMZ16S) ? 0xffff
            : ds.zpsm == GS_PSMZ24 ? 0xffffff : 0xffffffff;

    alpha = gs.alpha[ctx];
    ds.sa = BITS(alpha, 0, 2);
    ds.sb = BITS(alpha, 2, 2);
    ds.sc = BITS(alpha, 4, 2);
    ds.sd = BITS(alpha, 6, 2);
    ds.fix = BITS(alpha, 32, 8);
    if (ds.sa == 3) ds.sa = 2; /* 3 is reserved; reads as 0 */
    if (ds.sb == 3) ds.sb = 2;
    if (ds.sd == 3) ds.sd = 2;
    ds.blend_mix = ds.sa == 0 && ds.sb == 1 && ds.sc == 0 && ds.sd == 1;

    ds.tpsm = BITS(tex0, 20, 6);
    ds.tbp = BITS(tex0, 0, 14);
    ds.tbw = BITS(tex0, 14, 6);
    ds.tw = 1 << BITS(tex0, 26, 4);
    ds.th = 1 << BITS(tex0, 30, 4);
    ds.cpsm = BITS(tex0, 51, 4);
    ds.csa = BITS(tex0, 56, 5) * 16;
    ds.bilinear = BITS(gs.tex1[ctx], 5, 1);
    ds.wms = BITS(clamp, 0, 2);
    ds.wmt = BITS(clamp, 2, 2);
    ds.minu = BITS(clamp, 4, 10);
    ds.maxu = BITS(clamp, 14, 10);
    ds.minv = BITS(clamp, 24, 10);
    ds.maxv = BITS(clamp, 34, 10);
    ds.ta0 = BITS(gs.texa, 0, 8);
    ds.aem = BITS(gs.texa, 15, 1);
    ds.ta1 = BITS(gs.texa, 32, 8);

    ds.key = (ds.tme ? K_TME : 0) | (ds.abe ? K_ABE : 0) | (ds.zte ? K_ZTE : 0) |
             (ds.fpsm == GS_PSMCT32 ? K_CT32 : 0) | (ds.tme && ds.bilinear ? K_BIL : 0);

    /* Effects that read the picture being drawn are drawn by one thread, so
     * the rows they read are always in the same state. */
    ds.feedback = 0;
    if (ds.tme) {
        uint32_t f0 = ds.fbp * 256, f1 = f0 + GS_ROW64(ds.fbw) * 64 * 4 * (ds.scay1 + 1);
        uint32_t t0 = ds.tbp * 256, t1 = t0 + GS_ROW64(ds.tbw) * 64 * 4 * ds.th;
        ds.feedback = t0 < f1 && f0 < t1;
    }
    ds.clut_hash = clut_hash;
    if (gs_gpu_on) {
        ds.tex = NULL;
        ds.clut = gs.clut;
        return;
    }
    tc_bind();
}

/* ---- Texture sampling -------------------------------------------------- */

static inline int wrap_coord(int c, int size, int mode, int minv, int maxv)
{
    switch (mode) {
    case 0: return c & (size - 1);                       /* REPEAT */
    case 1: return c < 0 ? 0 : c >= size ? size - 1 : c;  /* CLAMP */
    case 2: return c < minv ? minv : c > maxv ? maxv : c; /* REGION_CLAMP */
    default: return (c & minv) | maxv;                    /* REGION_REPEAT */
    }
}

/* A CT16 texel or CLUT entry with the TEXA alpha rules. */
static inline uint32_t tex16(const DrawState *s, uint32_t c)
{
    uint32_t rgb = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);
    int a;

    if (c & 0x8000)
        a = s->ta1;
    else
        a = (s->aem && (c & 0x7fff) == 0) ? 0 : s->ta0;
    return rgb | ((uint32_t)a << 24);
}

/* Texel at wrapped coordinates (u, v), as RGBA. */
static inline uint32_t texel_raw(const DrawState *s, int u, int v)
{
    uint32_t c, idx;

    switch (s->tpsm) {
    case GS_PSMCT32:
        return gs_rd32(gs_addr32(0, s->tbp, s->tbw, u, v));
    case GS_PSMCT24:
        c = gs_rd32(gs_addr32(0, s->tbp, s->tbw, u, v)) & 0xffffff;
        return c | ((uint32_t)((s->aem && c == 0) ? 0 : s->ta0) << 24);
    case GS_PSMCT16:
        return tex16(s, gs_rd16(gs_addr16(0, s->tbp, s->tbw, u, v)));
    case GS_PSMCT16S:
        return tex16(s, gs_rd16(gs_addr16(1, s->tbp, s->tbw, u, v)));
    case GS_PSMT8:
        idx = gs_vram[gs_addr8(s->tbp, s->tbw, u, v)];
        break;
    case GS_PSMT4: {
        uint32_t n = gs_addr4(s->tbp, s->tbw, u, v);
        idx = ((gs_vram[n >> 1] >> ((n & 1) * 4)) & 15) + s->csa;
        break;
    }
    case GS_PSMT8H:
        idx = gs_rd32(gs_addr32(0, s->tbp, s->tbw, u, v)) >> 24;
        break;
    case GS_PSMT4HL:
        idx = ((gs_rd32(gs_addr32(0, s->tbp, s->tbw, u, v)) >> 24) & 15) + s->csa;
        break;
    case GS_PSMT4HH:
        idx = (gs_rd32(gs_addr32(0, s->tbp, s->tbw, u, v)) >> 28) + s->csa;
        break;
    default:
        return gs_read_pixel_nosync(s->tpsm, s->tbp, s->tbw, u, v);
    }
    c = s->clut[idx & 511];
    return s->cpsm == 0 ? c : tex16(s, c);
}

/* Decoded textures. Each entry holds a texture as RGBA, decoded lazily by
 * blocks of 8x8 texels as primitives reach them, and stays valid until GS
 * memory under it is written (page write generations, gs_mem.h) or, for
 * indexed textures, another CLUT is loaded. Worker threads may decode the
 * same block at once: they write the same values, and the block's flag is
 * set after its texels. */
#define TC_ENTRIES 48
#define TC_MAX_TEXELS (1024 * 1024)

typedef struct TexEntry {
    int used;
    uint64_t key, texa, clut;
    uint64_t stamp; /* gs_gen when the entry was (re)started */
    uint64_t last_use;
    int w, h, bw;   /* texels, and blocks per row */
    uint32_t *texels;
    uint8_t *valid; /* per block */
    size_t cap;     /* texels allocated */
    uint64_t batch; /* the batch of primitives that last used it */
    uint32_t palette[512]; /* the CLUT it was made with */
} TexEntry;

static TexEntry tcache[TC_ENTRIES];
static uint64_t tc_clock;

static void tc_flush(void)
{
    for (int i = 0; i < TC_ENTRIES; i++)
        tcache[i].used = 0;
    ds.tex = NULL;
}

static void tc_restart(TexEntry *t)
{
    memset(t->valid, 0, (size_t)t->bw * ((t->h + 7) / 8));
    t->stamp = gs_gen;
}

/* Picks the cache entry for the texture of the primitive being set up. */
static int batch_uses(const TexEntry *t);
static int batch_touches(int psm, uint32_t bp, uint32_t bw, int x0, int y0, int x1, int y1, int reads);
static void batch_flush(void);

static void tc_bind(void)
{
    uint64_t tex0 = gs.tex0[ds.ctx], key, texa, clut;
    int indexed = ds.tpsm == GS_PSMT8 || ds.tpsm == GS_PSMT4 || ds.tpsm == GS_PSMT8H ||
                  ds.tpsm == GS_PSMT4HL || ds.tpsm == GS_PSMT4HH;
    int w = ds.tw < 8 ? 8 : ds.tw, h = ds.th < 8 ? 8 : ds.th;
    TexEntry *t = NULL, *lru = &tcache[0];

    ds.tex = NULL;
    ds.clut = gs.clut;
    /* A texture read from the picture being drawn is read as it changes. */
    if (!ds.tme || ds.feedback || (size_t)w * h > TC_MAX_TEXELS)
        return;
    /* Drawn by primitives still waiting: draw them before reading it. */
    if (batch_touches(ds.tpsm, ds.tbp, ds.tbw, 0, 0, w - 1, h - 1, 0))
        batch_flush();
    key = tex0 & ((1ull << 34) - 1);                 /* TBP, TBW, PSM, TW, TH */
    if (indexed)
        key |= tex0 & (0x3full << 51) & ~(1ull << 55); /* CPSM, CSA */
    texa = (ds.tpsm == GS_PSMCT32) ? 0 : (uint64_t)ds.ta0 | (uint64_t)ds.aem << 8 | (uint64_t)ds.ta1 << 16;
    clut = indexed ? clut_gen : 0;

    for (int i = 0; i < TC_ENTRIES; i++) {
        TexEntry *e = &tcache[i];
        if (e->used && e->key == key && e->texa == texa && e->clut == clut) {
            t = e;
            break;
        }
        if (!e->used || (lru->used && e->last_use < lru->last_use))
            lru = e;
    }
    if (t == NULL) {
        t = lru;
        if (batch_uses(t))
            batch_flush(); /* it is about to hold another texture */
        if (t->cap < (size_t)w * h) {
            free(t->texels);
            free(t->valid);
            t->texels = malloc((size_t)w * h * 4);
            t->valid = malloc((size_t)(w / 8) * (h / 8));
            t->cap = t->texels != NULL && t->valid != NULL ? (size_t)w * h : 0;
            if (t->cap == 0) {
                t->used = 0;
                return;
            }
        }
        t->used = 1;
        t->key = key;
        t->texa = texa;
        t->clut = clut;
        t->w = w;
        t->h = h;
        t->bw = w / 8;
        if (indexed)
            memcpy(t->palette, gs.clut, sizeof(t->palette));
        tc_restart(t);
    } else if (gs_pages_newer(ds.tpsm, ds.tbp, ds.tbw, 0, 0, w - 1, h - 1, t->stamp)) {
        if (batch_uses(t))
            batch_flush();
        tc_restart(t);
    }
    t->last_use = ++tc_clock;
    ds.tex = t;
    ds.clut = t->palette;
}

static void tc_decode_block(const DrawState *s, TexEntry *t, int bx, int by)
{
    for (int y = by * 8; y < by * 8 + 8; y++) {
        uint32_t *row = t->texels + (size_t)y * t->w;
        for (int x = bx * 8; x < bx * 8 + 8; x++)
            row[x] = texel_raw(s, x, y);
    }
    __atomic_store_n(&t->valid[by * t->bw + bx], 1, __ATOMIC_RELEASE);
}

void gs_decode_texture(const DrawState *s, uint32_t *dst, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[y * w + x] = texel_raw(s, x, y);
}

static inline uint32_t fetch(const DrawState *s, int u, int v)
{
    const TexEntry *t = s->tex;

    u = wrap_coord(u, s->tw, s->wms, s->minu, s->maxu) & 2047;
    v = wrap_coord(v, s->th, s->wmt, s->minv, s->maxv) & 2047;
    if (t != NULL && u < t->w && v < t->h) {
        if (!__atomic_load_n(&t->valid[(v >> 3) * t->bw + (u >> 3)], __ATOMIC_ACQUIRE))
            tc_decode_block(s, (TexEntry *)t, u >> 3, v >> 3);
        return t->texels[v * t->w + u];
    }
    return texel_raw(s, u, v);
}

static ALWAYS_INLINE uint32_t sample(const DrawState *s, int k, int u16, int v16)
{
    if (!(k & K_BIL)) /* MMAG: nearest */
        return fetch(s, u16 >> 4, v16 >> 4);

    /* Bilinear: texel centres are at +0.5. Red/blue and green/alpha are
     * weighted two at a time (the weights add up to 256). */
    int us = u16 - 8, vs = v16 - 8;
    int u0 = us >> 4, v0 = vs >> 4, fu = us & 15, fv = vs & 15;
    uint32_t c00 = fetch(s, u0, v0), c10 = fetch(s, u0 + 1, v0);
    uint32_t c01 = fetch(s, u0, v0 + 1), c11 = fetch(s, u0 + 1, v0 + 1);
    uint32_t w00 = (16 - fu) * (16 - fv), w10 = fu * (16 - fv), w01 = (16 - fu) * fv, w11 = fu * fv;
    uint32_t rb = (c00 & 0xff00ff) * w00 + (c10 & 0xff00ff) * w10 +
                  (c01 & 0xff00ff) * w01 + (c11 & 0xff00ff) * w11;
    uint32_t ga = ((c00 >> 8) & 0xff00ff) * w00 + ((c10 >> 8) & 0xff00ff) * w10 +
                  ((c01 >> 8) & 0xff00ff) * w01 + ((c11 >> 8) & 0xff00ff) * w11;
    return ((rb >> 8) & 0xff00ff) | (ga & 0xff00ff00);
}

/* ---- Pixel pipeline ---------------------------------------------------- */

static inline int compare(int method, int a, int ref)
{
    switch (method) {
    case 0: return 0;
    case 1: return 1;
    case 2: return a < ref;
    case 3: return a <= ref;
    case 4: return a == ref;
    case 5: return a >= ref;
    case 6: return a > ref;
    default: return a != ref;
    }
}

/* Frame buffer address of (x, y) and its value as RGBA. */
static inline uint32_t frame_addr(const DrawState *s, int x, int y)
{
    switch (s->fpsm) {
    case GS_PSMCT16: return gs_addr16(0, s->fbp, s->fbw, x, y);
    case GS_PSMCT16S: return gs_addr16(1, s->fbp, s->fbw, x, y);
    case GS_PSMZ16: return gs_addr16(2, s->fbp, s->fbw, x, y);
    case GS_PSMZ16S: return gs_addr16(3, s->fbp, s->fbw, x, y);
    case GS_PSMZ32: case GS_PSMZ24: return gs_addr32(1, s->fbp, s->fbw, x, y);
    default: return gs_addr32(0, s->fbp, s->fbw, x, y);
    }
}

static inline int frame_is16(const DrawState *s)
{
    return (s->fpsm & 2) != 0; /* CT16, CT16S, Z16, Z16S */
}

static inline uint32_t z_addr(const DrawState *s, int x, int y)
{
    switch (s->zpsm) {
    case GS_PSMZ16: return gs_addr16(2, s->zbp, s->fbw, x, y);
    case GS_PSMZ16S: return gs_addr16(3, s->zbp, s->fbw, x, y);
    default: return gs_addr32(1, s->zbp, s->fbw, x, y);
    }
}

/* Shades and writes one pixel inside the scissor area, with the settings
 * of key k. Colour components are 0..255; u, v are texel coordinates in
 * 1/16. */
static ALWAYS_INLINE void shade(const DrawState *s, int k, int x, int y, uint32_t z, int r, int g, int b, int a,
                                int f, int u, int v)
{
    int write_rgb = 1, write_a = 1, write_z = !s->zmsk;

    /* Depth test first: a hidden pixel needs no texture lookup (the tests
     * only decide what is written, so their order does not matter). */
    uint32_t za = 0;
    if (k & K_ZTE) {
        if (s->ztst == 0)
            return;
        if (z > s->zmax)
            z = s->zmax;
        za = z_addr(s, x, y);
        if (s->ztst >= 2) {
            uint32_t zd = (s->zpsm & 2) ? gs_rd16(za)
                        : s->zpsm == GS_PSMZ24 ? gs_rd32(za) & 0xffffff : gs_rd32(za);
            if (s->ztst == 2 ? z < zd : z <= zd)
                return;
        }
    } else {
        write_z = 0;
    }

    if (k & K_TME) {
        uint32_t t = sample(s, k, u, v);
        int tr = t & 255, tg = (t >> 8) & 255, tb = (t >> 16) & 255, ta = t >> 24;
        switch (s->tfx) {
        case 0: /* MODULATE */
            r = (tr * r) >> 7; g = (tg * g) >> 7; b = (tb * b) >> 7;
            if (s->tcc) a = (ta * a) >> 7;
            break;
        case 1: /* DECAL */
            r = tr; g = tg; b = tb;
            if (s->tcc) a = ta;
            break;
        case 2: /* HIGHLIGHT */
            r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a;
            if (s->tcc) a = ta + a;
            break;
        default: /* HIGHLIGHT2 */
            r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a;
            if (s->tcc) a = ta;
            break;
        }
        /* Every function above gives values >= 0. */
        r = r > 255 ? 255 : r; g = g > 255 ? 255 : g;
        b = b > 255 ? 255 : b; a = a > 255 ? 255 : a;
    }

    if (s->fge) {
        r = (f * r + (255 - f) * s->fr) >> 8;
        g = (f * g + (255 - f) * s->fg) >> 8;
        b = (f * b + (255 - f) * s->fb_) >> 8;
    }

    /* Alpha test */
    if (s->ate && !compare(s->atst, a, s->aref)) {
        switch (s->afail) {
        case 0: return;                            /* KEEP */
        case 1: write_z = 0; break;                /* FB_ONLY */
        case 2: write_rgb = write_a = 0; break;    /* ZB_ONLY */
        default: write_a = 0; write_z = 0; break;  /* RGB_ONLY */
        }
    }

    uint32_t fa = (k & K_CT32) ? gs_addr32(0, s->fbp, s->fbw, x, y) : frame_addr(s, x, y), raw, dst;
    if (!(k & K_CT32) && frame_is16(s)) {
        raw = gs_rd16(fa);
        dst = ((raw & 31) << 3) | (((raw >> 5) & 31) << 11) | (((raw >> 10) & 31) << 19) |
              ((raw & 0x8000) ? 0x80000000u : 0);
    } else {
        raw = gs_rd32(fa);
        dst = s->fpsm == GS_PSMCT24 || s->fpsm == GS_PSMZ24 ? raw | 0x80000000u : raw;
    }

    /* Destination alpha test */
    if (s->date && (int)((dst >> 31) & 1) != s->datm)
        return;

    if ((k & K_ABE) && !(s->pabe && a < 0x80)) {
        int dr = dst & 255, dg = (dst >> 8) & 255, db = (dst >> 16) & 255;
        if (s->blend_mix) {
            /* (Cs - Cd) * As + Cd, the usual transparency */
            r = (((r - dr) * a) >> 7) + dr;
            g = (((g - dg) * a) >> 7) + dg;
            b = (((b - db) * a) >> 7) + db;
        } else {
            int c = s->sc == 0 ? a : s->sc == 1 ? (int)(dst >> 24) : s->fix;
            int col[3][3] = { { r, g, b }, { dr, dg, db }, { 0, 0, 0 } };
            r = (((col[s->sa][0] - col[s->sb][0]) * c) >> 7) + col[s->sd][0];
            g = (((col[s->sa][1] - col[s->sb][1]) * c) >> 7) + col[s->sd][1];
            b = (((col[s->sa][2] - col[s->sb][2]) * c) >> 7) + col[s->sd][2];
        }
    }

    if (s->colclamp) {
        r = clamp255(r); g = clamp255(g); b = clamp255(b);
    } else {
        r &= 255; g &= 255; b &= 255;
    }
    a &= 255;
    if (s->fba)
        a |= 0x80;

    uint32_t src = rgba(r, g, b, a);
    if (!write_rgb)
        src = (src & 0xff000000u) | (dst & 0x00ffffffu);
    if (!write_a)
        src = (src & 0x00ffffffu) | (dst & 0xff000000u);

    if (write_rgb || write_a) {
        if (!(k & K_CT32) && frame_is16(s)) {
            uint32_t v16 = ((src >> 3) & 31) | (((src >> 11) & 31) << 5) |
                           (((src >> 19) & 31) << 10) | ((src >> 31) << 15);
            gs_wr16(fa, (v16 & ~s->fbmsk16) | (raw & s->fbmsk16));
        } else if (s->fpsm == GS_PSMCT24 || s->fpsm == GS_PSMZ24) {
            uint32_t m = s->fbmsk | 0xff000000u;
            gs_wr32(fa, (src & ~m) | (raw & m));
        } else {
            gs_wr32(fa, (src & ~s->fbmsk) | (raw & s->fbmsk));
        }
    }
    if (write_z) {
        if (s->zpsm & 2)
            gs_wr16(za, z);
        else if (s->zpsm == GS_PSMZ24)
            gs_wr32(za, (gs_rd32(za) & 0xff000000u) | (z & 0xffffff));
        else
            gs_wr32(za, z);
    }
}

/* Marks the frame and depth buffer pages that pixels x0..x1, y0..y1 of a
 * primitive may have written, for the texture cache. */
static void mark_drawn(int x0, int y0, int x1, int y1)
{
    gs_mark_pages(ds.fpsm, ds.fbp, ds.fbw, x0, y0, x1, y1);
    if (!ds.zmsk)
        gs_mark_pages(ds.zpsm, ds.zbp, ds.fbw, x0, y0, x1, y1);
}

/* One pixel anywhere (points and lines). */
static void draw_pixel(int x, int y, uint32_t z, int r, int g, int b, int a, int f, int u, int v)
{
    if (x < ds.scax0 || x > ds.scax1 || y < ds.scay0 || y > ds.scay1)
        return;
    shade(&ds, ds.key, x, y, z, r, g, b, a, f, u, v);
}

static void count_pixels(uint32_t n)
{
    __atomic_fetch_add(&gs_stats.pixels, n, __ATOMIC_RELAXED);
}

/* ---- Rasterisation ----------------------------------------------------- */

/* Texture coordinates of a vertex in 1/16 texel. */
static void tex_coords(const Vertex *v, float *u, float *w)
{
    if (ds.fst) {
        *u = (float)v->u;
        *w = (float)v->v;
    } else {
        int tw = 1 << BITS(gs.tex0[ds.ctx], 26, 4), th = 1 << BITS(gs.tex0[ds.ctx], 30, 4);
        float q = v->q != 0.0f ? v->q : 1.0f;
        *u = v->s / q * tw * 16.0f;
        *w = v->t / q * th * 16.0f;
    }
}

/* Primitives are drawn either at once (rows split between threads) or,
 * when they can wait, gathered in a batch that the threads then draw by
 * bands of the screen (see "Batches" below). Either way a job holds all
 * that drawing needs, with the settings it was made with. */
static void batch_add(int type, const void *job, int py0, int py1, int pixels);
static int batch_ready(void);
static void batch_flush(void);

typedef struct {
    const DrawState *s;
    uint32_t z;
    int r, g, b, a, f; /* sprites take these from the second vertex */
    int x0, y0, px0, px1;
    float u0, v0, du, dv;
} SpriteJob;

static ALWAYS_INLINE void sprite_span(int k, const SpriteJob *j, int py0, int py1)
{
    const DrawState *s = j->s;

    for (int py = py0; py < py1; py++) {
        int v = (int)(j->v0 + ((py << 4) - j->y0) * j->dv);
        for (int px = j->px0; px < j->px1; px++) {
            int u = (int)(j->u0 + ((px << 4) - j->x0) * j->du);
            shade(s, k, px, py, j->z, j->r, j->g, j->b, j->a, j->f, u, v);
        }
    }
}

static void sprite_rows(void *ctx, int py0, int py1)
{
    const SpriteJob *j = ctx;

    switch (j->s->key) {
#define SPRITE_CASE(k) case k: sprite_span(k, j, py0, py1); break;
    KEY_CASES(SPRITE_CASE)
#undef SPRITE_CASE
    }
    if (j->px1 > j->px0 && py1 > py0)
        count_pixels((uint32_t)(py1 - py0) * (j->px1 - j->px0));
}

static void draw_sprite(const Vertex *a, const Vertex *b)
{
    int x0 = a->x, x1 = b->x, y0 = a->y, y1 = b->y;
    float u0, v0, u1, v1;
    SpriteJob j;

    tex_coords(a, &u0, &v0);
    tex_coords(b, &u1, &v1);
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; float f = u0; u0 = u1; u1 = f; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; float f = v0; v0 = v1; v1 = f; }

    /* Pixels whose top-left corner lies in [x0, x1) x [y0, y1). */
    int px0 = (x0 + 15) >> 4, px1 = (x1 + 15) >> 4;
    int py0 = (y0 + 15) >> 4, py1 = (y1 + 15) >> 4;

    if (px0 < ds.scax0) px0 = ds.scax0;
    if (py0 < ds.scay0) py0 = ds.scay0;
    if (px1 > ds.scax1 + 1) px1 = ds.scax1 + 1;
    if (py1 > ds.scay1 + 1) py1 = ds.scay1 + 1;
    if (px1 <= px0 || py1 <= py0)
        return;

    j.s = &ds;
    j.z = b->z;
    j.r = b->r; j.g = b->g; j.b = b->b; j.a = b->a; j.f = b->f;
    j.x0 = x0;
    j.y0 = y0;
    j.px0 = px0;
    j.px1 = px1;
    j.u0 = u0;
    j.v0 = v0;
    j.du = x1 != x0 ? (u1 - u0) / (float)(x1 - x0) : 0.0f;
    j.dv = y1 != y0 ? (v1 - v0) / (float)(y1 - y0) : 0.0f;
    if (batch_ready()) {
        batch_add(6, &j, py0, py1, (px1 - px0) * (py1 - py0));
    } else {
        batch_flush();
        gs_parallel(sprite_rows, &j, py0, py1, ds.feedback ? 0 : (px1 - px0) * (py1 - py0));
    }
    mark_drawn(px0, py0, px1 - 1, py1 - 1);
}

static int64_t edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (int64_t)(bx - ax) * (py - ay) - (int64_t)(by - ay) * (px - ax);
}

/* Attributes interpolated over a triangle, as planes around its first
 * vertex: value = a0 + gx * (sx - x0) + gy * (sy - y0), in 1/16 pixel. */
enum { A_R, A_G, A_B, A_A, A_F, A_U, A_V, A_Q, A_COUNT };

typedef struct {
    const DrawState *s;
    int ex[3], ey[3]; /* vertex positions, in drawing order */
    int lr, lg, lb, la; /* flat shading: the colour of the last vertex sent */
    int px0, px1;
    int bias[3]; /* 1 for edges whose pixels are left out (right, bottom) */
    int x0, y0;
    float a0[A_COUNT], gx[A_COUNT], gy[A_COUNT];
    double z0, zgx, zgy;
} TriangleJob;

static ALWAYS_INLINE uint32_t triangle_span(int k, const TriangleJob *j, int py0, int py1)
{
    const DrawState *s = j->s;
    uint32_t drawn = 0;

    for (int py = py0; py < py1; py++) {
        int sy = py << 4, xs = j->px0, xe = j->px1;

        /* The pixels of this row inside all three edges: each edge function
         * changes by a constant step from one pixel to the next. */
        for (int e = 0; e < 3 && xs <= xe; e++) {
            int a = (e + 1) % 3, b = (e + 2) % 3;
            int64_t w = edge(j->ex[a], j->ey[a], j->ex[b], j->ey[b], j->px0 << 4, sy) - j->bias[e];
            int64_t dw = -(int64_t)(j->ey[b] - j->ey[a]) * 16;
            if (dw == 0) {
                if (w < 0)
                    xe = xs - 1;
            } else if (dw > 0) {
                if (w < 0) {
                    int64_t kmin = (-w + dw - 1) / dw;
                    if (j->px0 + kmin > xs)
                        xs = (int)(j->px0 + (kmin < 100000 ? kmin : 100000));
                }
            } else {
                if (w < 0) {
                    xe = xs - 1;
                } else {
                    int64_t kmax = w / -dw;
                    if (j->px0 + kmax < xe)
                        xe = (int)(j->px0 + kmax);
                }
            }
        }
        if (xs > xe)
            continue;

        float dx = (float)((xs << 4) - j->x0), dy = (float)(sy - j->y0);
        float at[A_COUNT], step[A_COUNT];
        for (int i = 0; i < A_COUNT; i++) {
            at[i] = j->a0[i] + j->gx[i] * dx + j->gy[i] * dy;
            step[i] = j->gx[i] * 16.0f;
        }
        double z = j->z0 + j->zgx * ((xs << 4) - j->x0) + j->zgy * (sy - j->y0) + 0.5;
        double zstep = j->zgx * 16.0;

        for (int px = xs; px <= xe; px++) {
            int r, g, b, a;
            /* Rounding errors must not take a value below what all three
             * vertices hold: colours get a small bias, and depth (up to 32
             * bits, compared exactly) is interpolated in double. */
            if (s->iip) {
                r = (int)(at[A_R] + 0.01f); g = (int)(at[A_G] + 0.01f);
                b = (int)(at[A_B] + 0.01f); a = (int)(at[A_A] + 0.01f);
            } else {
                r = j->lr; g = j->lg; b = j->lb; a = j->la;
            }
            int u = 0, tv = 0;
            if (k & K_TME) {
                float qq = at[A_Q] != 0.0f ? at[A_Q] : 1.0f;
                u = (int)(at[A_U] / qq);
                tv = (int)(at[A_V] / qq);
            }
            shade(s, k, px, py, z <= 0.0 ? 0u : z >= 4294967295.0 ? 0xffffffffu : (uint32_t)z,
                  r, g, b, a, (int)(at[A_F] + 0.01f), u, tv);
            for (int i = 0; i < A_COUNT; i++)
                at[i] += step[i];
            z += zstep;
        }
        drawn += (uint32_t)(xe - xs + 1);
    }
    return drawn;
}

static void triangle_rows(void *ctx, int py0, int py1)
{
    const TriangleJob *j = ctx;
    uint32_t drawn = 0;

    switch (j->s->key) {
#define TRIANGLE_CASE(k) case k: drawn = triangle_span(k, j, py0, py1); break;
    KEY_CASES(TRIANGLE_CASE)
#undef TRIANGLE_CASE
    }
    count_pixels(drawn);
}

static void draw_triangle(const Vertex *v0, const Vertex *v1, const Vertex *v2)
{
    TriangleJob j;
    const Vertex *v[3] = { v0, v1, v2 };
    int64_t area = edge(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y);

    if (area == 0)
        return;
    if (area < 0) {
        const Vertex *t = v[1]; v[1] = v[2]; v[2] = t;
        area = -area;
    }

    /* Top-left rule, as on the GS: a pixel exactly on an edge belongs to the
     * triangle only on its top and left edges. Without it, a polygon whose
     * edges fall on pixels gets one more column and row, sampled past its
     * texture area (a sliver of the next letter beside each font glyph), and
     * triangles sharing an edge draw it twice. With the vertices in this
     * order, edges going up are left edges and edges going right are top. */
    for (int i = 0; i < 3; i++) {
        const Vertex *a = v[(i + 1) % 3], *b = v[(i + 2) % 3];
        int top_left = b->y < a->y || (b->y == a->y && b->x > a->x);
        j.bias[i] = top_left ? 0 : 1;
        j.ex[i] = v[i]->x;
        j.ey[i] = v[i]->y;
    }

    int minx = v[0]->x, maxx = v[0]->x, miny = v[0]->y, maxy = v[0]->y;
    for (int i = 1; i < 3; i++) {
        if (v[i]->x < minx) minx = v[i]->x;
        if (v[i]->x > maxx) maxx = v[i]->x;
        if (v[i]->y < miny) miny = v[i]->y;
        if (v[i]->y > maxy) maxy = v[i]->y;
    }
    int px0 = (minx + 15) >> 4, px1 = maxx >> 4, py0 = (miny + 15) >> 4, py1 = maxy >> 4;
    if (px0 < ds.scax0) px0 = ds.scax0;
    if (py0 < ds.scay0) py0 = ds.scay0;
    if (px1 > ds.scax1) px1 = ds.scax1;
    if (py1 > ds.scay1) py1 = ds.scay1;
    if (px1 < px0 || py1 < py0)
        return;
    j.px0 = px0;
    j.px1 = px1;

    /* Per-vertex attributes; texture coordinates are interpolated as s/q, t/q
     * and 1/q for perspective correction (STQ), or linearly (UV). */
    float att[3][A_COUNT];
    for (int i = 0; i < 3; i++) {
        const Vertex *p = v[i];
        float q = (ds.fst || p->q == 0.0f) ? 1.0f : p->q;
        att[i][A_R] = (float)p->r; att[i][A_G] = (float)p->g; att[i][A_B] = (float)p->b;
        att[i][A_A] = (float)p->a; att[i][A_F] = (float)p->f;
        if (ds.fst) {
            att[i][A_U] = (float)p->u;
            att[i][A_V] = (float)p->v;
        } else {
            att[i][A_U] = p->s * ds.tw * 16.0f;
            att[i][A_V] = p->t * ds.th * 16.0f;
        }
        att[i][A_Q] = q;
    }
    /* Planes from the differences to the first vertex: an attribute equal at
     * all three vertices is exactly that value everywhere. */
    {
        double dx1 = v[1]->x - v[0]->x, dy1 = v[1]->y - v[0]->y;
        double dx2 = v[2]->x - v[0]->x, dy2 = v[2]->y - v[0]->y;
        double det = (double)area;
        for (int i = 0; i < A_COUNT; i++) {
            double d1 = (double)att[1][i] - att[0][i], d2 = (double)att[2][i] - att[0][i];
            j.a0[i] = att[0][i];
            j.gx[i] = (float)((d1 * dy2 - d2 * dy1) / det);
            j.gy[i] = (float)((d2 * dx1 - d1 * dx2) / det);
        }
        double z1 = (double)v[1]->z - v[0]->z, z2 = (double)v[2]->z - v[0]->z;
        j.z0 = (double)v[0]->z;
        j.zgx = (z1 * dy2 - z2 * dy1) / det;
        j.zgy = (z2 * dx1 - z1 * dx2) / det;
    }
    j.x0 = v[0]->x;
    j.y0 = v[0]->y;
    /* Flat shading uses the colour of the last vertex sent. */
    j.lr = v2->r; j.lg = v2->g; j.lb = v2->b; j.la = v2->a;
    j.s = &ds;
    int work = (px1 - px0 + 1) * (py1 - py0 + 1) / 2;
    if (batch_ready()) {
        batch_add(3, &j, py0, py1 + 1, work);
    } else {
        batch_flush();
        gs_parallel(triangle_rows, &j, py0, py1 + 1, ds.feedback ? 0 : work);
    }
    mark_drawn(px0, py0, px1, py1);
}

/* ---- Batches -------------------------------------------------------------
 *
 * Small primitives are not worth splitting between threads one at a time,
 * so they are gathered and drawn together: each thread takes bands of rows
 * and draws, in order, the part of every primitive in its band. Pixels in
 * different rows never share memory, so the result is the same as drawing
 * the primitives one by one.
 *
 * A batch is drawn (flushed) before anything that must see its pixels or
 * could change what it reads: a transfer into GS memory a primitive reads
 * (a texture) or draws to, a texture or CLUT read from what it draws, a
 * change of frame or depth buffer, a primitive drawn at once, and any read
 * of GS memory from outside (the display, gs_read_pixel). Its textures are
 * decoded entries of the cache (with their own copy of the CLUT), kept
 * until it is drawn.
 */
#define BAND_ROWS 8
#define BATCH_MAX 32768

typedef struct {
    int type, state;
    int y0, y1; /* rows [y0, y1) */
    union {
        TriangleJob tri;
        SpriteJob spr;
    } j;
} BatchCmd;

static struct {
    int enabled; /* -1: not decided yet */
    BatchCmd *cmds;
    int n, cap;
    DrawState *states;
    int ns, scap;
    uint64_t id;
    int64_t work;
    int rows;
    uint64_t reads[GS_PAGES / 64], writes[GS_PAGES / 64];
    uint32_t fbp, fbw, zbp;
    int fpsm, zpsm;
} batch = { .enabled = -1, .id = 1 };

static int batch_uses(const TexEntry *t)
{
    return batch.n > 0 && t->used && t->batch == batch.id;
}

static int masks_meet(const uint64_t *a, const uint64_t *b)
{
    for (int i = 0; i < GS_PAGES / 64; i++)
        if (a[i] & b[i])
            return 1;
    return 0;
}

/* 1 if the pages of that rectangle are drawn to (writes) or also read
 * (reads) by the batch waiting to be drawn. */
static int batch_touches(int psm, uint32_t bp, uint32_t bw, int x0, int y0, int x1, int y1, int reads)
{
    uint64_t m[GS_PAGES / 64] = { 0 };

    if (batch.n == 0)
        return 0;
    gs_page_mask(psm, bp, bw, x0, y0, x1, y1, m);
    return masks_meet(m, batch.writes) || (reads && masks_meet(m, batch.reads));
}

static void batch_bands(void *ctx, int b0, int b1)
{
    (void)ctx;
    for (int band = b0; band < b1; band++) {
        int r0 = band * BAND_ROWS, r1 = r0 + BAND_ROWS;
        for (int i = 0; i < batch.n; i++) {
            BatchCmd *c = &batch.cmds[i];
            int y0 = c->y0 > r0 ? c->y0 : r0, y1 = c->y1 < r1 ? c->y1 : r1;
            if (y0 >= y1)
                continue;
            if (c->type == 3)
                triangle_rows(&c->j.tri, y0, y1);
            else
                sprite_rows(&c->j.spr, y0, y1);
        }
    }
}

static void batch_flush(void)
{
    if (batch.n == 0)
        return;
    for (int i = 0; i < batch.n; i++) {
        BatchCmd *c = &batch.cmds[i];
        if (c->type == 3)
            c->j.tri.s = &batch.states[c->state];
        else
            c->j.spr.s = &batch.states[c->state];
    }
    gs_parallel(batch_bands, NULL, 0, (batch.rows + BAND_ROWS - 1) / BAND_ROWS,
                batch.work > 0x7fffffff ? 0x7fffffff : (int)batch.work);
    batch.n = 0;
    batch.ns = 0;
    batch.work = 0;
    batch.rows = 0;
    memset(batch.reads, 0, sizeof(batch.reads));
    memset(batch.writes, 0, sizeof(batch.writes));
    batch.id++;
}

/* 1 if the primitive being set up can wait in the batch. */
static int batch_ready(void)
{
    if (batch.enabled < 0)
        batch.enabled = gs_thread_count() > 1 && getenv("CVX_GS_TRACE") == NULL &&
                        getenv("CVX_GS_NO_BATCH") == NULL;
    /* Textures read from GS memory as they draw (from the picture being
     * drawn, or too large for the cache) are drawn at once. */
    return batch.enabled && !ds.feedback && (!ds.tme || ds.tex != NULL);
}

static void batch_add(int type, const void *job, int py0, int py1, int pixels)
{
    BatchCmd *c;

    gs_mem_sync = batch_flush; /* reads of GS memory from outside see it drawn */
    /* One frame and depth buffer per batch. */
    if (batch.n > 0 && (batch.fbp != ds.fbp || batch.fbw != ds.fbw || batch.fpsm != ds.fpsm ||
                        batch.zbp != ds.zbp || batch.zpsm != ds.zpsm))
        batch_flush();
    if (batch.n == BATCH_MAX)
        batch_flush();
    if (batch.n == batch.cap) {
        int cap = batch.cap ? batch.cap * 2 : 1024;
        BatchCmd *p = realloc(batch.cmds, (size_t)cap * sizeof(*p));
        if (p == NULL) {
            batch_flush();
            return; /* nothing to draw with: the primitive is lost */
        }
        batch.cmds = p;
        batch.cap = cap;
    }
    if (batch.ns == 0 || memcmp(&batch.states[batch.ns - 1], &ds, sizeof(ds)) != 0) {
        if (batch.ns == batch.scap) {
            int cap = batch.scap ? batch.scap * 2 : 256;
            DrawState *p = realloc(batch.states, (size_t)cap * sizeof(*p));
            if (p == NULL) {
                batch_flush();
                return;
            }
            batch.states = p;
            batch.scap = cap;
        }
        batch.states[batch.ns++] = ds;
        /* What the primitives with these settings draw to and read. */
        gs_page_mask(ds.fpsm, ds.fbp, ds.fbw, ds.scax0, ds.scay0, ds.scax1, ds.scay1, batch.writes);
        if (!ds.zmsk || ds.zte)
            gs_page_mask(ds.zpsm, ds.zbp, ds.fbw, ds.scax0, ds.scay0, ds.scax1, ds.scay1, batch.writes);
        if (ds.tme)
            gs_page_mask(ds.tpsm, ds.tbp, ds.tbw, 0, 0, ds.tex->w - 1, ds.tex->h - 1, batch.reads);
    }
    if (batch.n == 0) {
        batch.fbp = ds.fbp;
        batch.fbw = ds.fbw;
        batch.fpsm = ds.fpsm;
        batch.zbp = ds.zbp;
        batch.zpsm = ds.zpsm;
    }
    if (ds.tex != NULL)
        ds.tex->batch = batch.id;

    c = &batch.cmds[batch.n++];
    c->type = type;
    c->state = batch.ns - 1;
    c->y0 = py0;
    c->y1 = py1;
    if (type == 3)
        c->j.tri = *(const TriangleJob *)job;
    else
        c->j.spr = *(const SpriteJob *)job;
    if (py1 > batch.rows)
        batch.rows = py1;
    batch.work += pixels;
}

static void draw_line(const Vertex *a, const Vertex *b)
{
    int x0 = a->x >> 4, y0 = a->y >> 4, x1 = b->x >> 4, y1 = b->y >> 4;
    int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    float u0, v0, u1, v1;

    tex_coords(a, &u0, &v0);
    tex_coords(b, &u1, &v1);
    for (int i = 0; i <= steps; i++) {
        float t = steps ? (float)i / steps : 0.0f;
        const Vertex *c = ds.iip ? NULL : b;
        draw_pixel(x0 + (int)((x1 - x0) * t), y0 + (int)((y1 - y0) * t),
                   (uint32_t)(a->z + (b->z - (float)a->z) * t),
                   c ? c->r : (int)(a->r + (b->r - a->r) * t),
                   c ? c->g : (int)(a->g + (b->g - a->g) * t),
                   c ? c->b : (int)(a->b + (b->b - a->b) * t),
                   c ? c->a : (int)(a->a + (b->a - a->a) * t),
                   b->f, (int)(u0 + (u1 - u0) * t), (int)(v0 + (v1 - v0) * t));
    }
    count_pixels(steps + 1);
    mark_drawn(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0);
}

/* ---- Vertex queue ------------------------------------------------------ */

static void vertex_kick(uint64_t xyz, int has_f, int draw)
{
    int ctx = BITS(gs.prim, 9, 1);
    int type = BITS(gs.prim, 0, 3);
    Vertex v;

    v.x = (int)BITS(xyz, 0, 16) - (int)BITS(gs.xyoffset[ctx], 0, 16);
    v.y = (int)BITS(xyz, 16, 16) - (int)BITS(gs.xyoffset[ctx], 32, 16);
    if (has_f) {
        v.z = BITS(xyz, 32, 24);
        v.f = BITS(xyz, 56, 8);
    } else {
        v.z = BITS(xyz, 32, 32);
        v.f = BITS(gs.fog, 56, 8);
    }
    v.r = BITS(gs.rgbaq, 0, 8);
    v.g = BITS(gs.rgbaq, 8, 8);
    v.b = BITS(gs.rgbaq, 16, 8);
    v.a = BITS(gs.rgbaq, 24, 8);
    {
        uint32_t qb = BITS(gs.rgbaq, 32, 32), sb = BITS(gs.st, 0, 32), tb = BITS(gs.st, 32, 32);
        memcpy(&v.q, &qb, 4);
        memcpy(&v.s, &sb, 4);
        memcpy(&v.t, &tb, 4);
    }
    v.u = BITS(gs.uv, 0, 14);
    v.v = BITS(gs.uv, 16, 14);

    int needed;
    switch (type) {
    case 0: needed = 1; break;              /* point */
    case 1: case 2: case 6: needed = 2; break; /* line, line strip, sprite */
    default: needed = 3; break;             /* triangle, strip, fan */
    }

    gs.queue[gs.queued++] = v;
    if (gs.queued < needed)
        return;

    if (draw) {
        static int trace = -1;
        if (trace < 0)
            trace = getenv("CVX_GS_TRACE") != NULL;
        setup_draw();
        if (trace) {
            /* One line per primitive: its registers, then its vertices. */
            int c = ds.ctx;
            fprintf(stderr, "prim %u type %d ctx %d prim %03x frame %016llx tex0 %016llx tex1 %016llx "
                    "clamp %016llx alpha %016llx test %016llx zbuf %016llx texa %016llx fba %d pabe %d dthe %d\n",
                    gs_stats.prims, type, c, (unsigned)BITS(gs.prim, 0, 11),
                    (unsigned long long)gs.frame[c], (unsigned long long)gs.tex0[c],
                    (unsigned long long)gs.tex1[c], (unsigned long long)gs.clamp[c],
                    (unsigned long long)gs.alpha[c], (unsigned long long)gs.test[c], (unsigned long long)gs.zbuf[c],
                    (unsigned long long)gs.texa, (int)BITS(gs.fba[c], 0, 1), (int)BITS(gs.pabe, 0, 1),
                    (int)BITS(gs.dthe, 0, 1));
            for (int i = 0; i < gs.queued; i++) {
                const Vertex *q = &gs.queue[i];
                fprintf(stderr, "  v%d xy %.4f %.4f z %u rgba %d %d %d %d uv %.4f %.4f stq %g %g %g\n", i,
                        q->x / 16.0, q->y / 16.0, q->z, q->r, q->g, q->b, q->a, q->u / 16.0, q->v / 16.0,
                        q->s, q->t, q->q);
            }
        }
        gs_stats.prims++;
        changes++;
        if (gs_gpu_on)
            gs_gpu_prim(&ds, type, gs.queue);
        else switch (type) {
        case 0:
            batch_flush();
            draw_pixel(v.x >> 4, v.y >> 4, v.z, v.r, v.g, v.b, v.a, v.f, v.u, v.v);
            count_pixels(1);
            mark_drawn(v.x >> 4, v.y >> 4, v.x >> 4, v.y >> 4);
            break;
        case 1: case 2: batch_flush(); draw_line(&gs.queue[0], &gs.queue[1]); break;
        case 3: case 4: case 5: draw_triangle(&gs.queue[0], &gs.queue[1], &gs.queue[2]); break;
        case 6: draw_sprite(&gs.queue[0], &gs.queue[1]); break;
        }
        if (trace) {
            /* CVX_GS_PIXEL=x,y: that pixel of the frame buffer and depth
             * buffer after the primitive. */
            const char *px = getenv("CVX_GS_PIXEL");
            int x, y;
            if (px != NULL && sscanf(px, "%d,%d", &x, &y) == 2)
                fprintf(stderr, "  pixel %d,%d = %08x z %08x\n", x, y,
                        gs_read_pixel_nosync(ds.fpsm, ds.fbp, ds.fbw, x, y),
                        gs_read_pixel_nosync(ds.zpsm, ds.zbp, ds.fbw, x, y));
        }
    }

    switch (type) {
    case 2: /* line strip */
        gs.queue[0] = gs.queue[1];
        gs.queued = 1;
        break;
    case 4: /* triangle strip */
        gs.queue[0] = gs.queue[1];
        gs.queue[1] = gs.queue[2];
        gs.queued = 2;
        break;
    case 5: /* triangle fan: the first vertex stays */
        gs.queue[1] = gs.queue[2];
        gs.queued = 2;
        break;
    default:
        gs.queued = 0;
        break;
    }
}

/* ---- Registers ---------------------------------------------------------- */

void gs_write_reg(int addr, uint64_t value)
{
    switch (addr) {
    case 0x00: gs.prim = value; gs.queued = 0; break;
    case 0x01: gs.rgbaq = value; break;
    case 0x02: gs.st = value; break;
    case 0x03: gs.uv = value; break;
    case 0x04: vertex_kick(value, 1, 1); break;   /* XYZF2 */
    case 0x05: vertex_kick(value, 0, 1); break;   /* XYZ2 */
    case 0x06: case 0x07:
        gs.tex0[addr - 6] = value;
        load_clut(value);
        break;
    case 0x08: case 0x09: gs.clamp[addr - 8] = value; break;
    case 0x0a: gs.fog = value; break;
    case 0x0c: vertex_kick(value, 1, 0); break;   /* XYZF3 */
    case 0x0d: vertex_kick(value, 0, 0); break;   /* XYZ3 */
    case 0x14: case 0x15: gs.tex1[addr - 0x14] = value; break;
    case 0x16: case 0x17: {
        /* TEX2: the CLUT part of TEX0 */
        int c = addr - 0x16;
        uint64_t mask = 0xffffffe003f00000ull; /* PSM, CBP, CPSM, CSM, CSA, CLD */
        gs.tex0[c] = (gs.tex0[c] & ~mask) | (value & mask);
        load_clut(gs.tex0[c]);
        break;
    }
    case 0x18: case 0x19: gs.xyoffset[addr - 0x18] = value; break;
    case 0x1a: gs.prmodecont = value; break;
    case 0x1b: gs.prmode = value; break;
    case 0x1c: gs.texclut = value; break;
    case 0x3b: gs.texa = value; break;
    case 0x3d: gs.fogcol = value; break;
    case 0x40: case 0x41: gs.scissor[addr - 0x40] = value; break;
    case 0x42: case 0x43: gs.alpha[addr - 0x42] = value; break;
    case 0x44: gs.dimx = value; break;
    case 0x45: gs.dthe = value; break;
    case 0x46: gs.colclamp = value; break;
    case 0x47: case 0x48: gs.test[addr - 0x47] = value; break;
    case 0x49: gs.pabe = value; break;
    case 0x4a: case 0x4b: gs.fba[addr - 0x4a] = value; break;
    case 0x4c: case 0x4d: gs.frame[addr - 0x4c] = value; break;
    case 0x4e: case 0x4f: gs.zbuf[addr - 0x4e] = value; break;
    case 0x50: gs.bitbltbuf = value; break;
    case 0x51: gs.trxpos = value; break;
    case 0x52: gs.trxreg = value; break;
    case 0x53: gs.trxdir = value; trx_start(); break;
    case 0x54: { /* HWREG: 8 bytes of image data */
        uint8_t q[8];
        memcpy(q, &value, 8);
        trx_bytes(q, 8);
        break;
    }
    default:
        break; /* TEXFLUSH, SIGNAL, FINISH, LABEL, mip registers... */
    }
}

/* ---- GIF ---------------------------------------------------------------- */

static void gif_packed(int reg, uint64_t lo, uint64_t hi)
{
    switch (reg) {
    case 0x0: gs_write_reg(0x00, lo); break;
    case 0x1: /* RGBAQ: R, G, B, A in the four words, Q from the last ST */
        gs.rgbaq = BITS(lo, 0, 8) | ((uint64_t)BITS(lo, 32, 8) << 8) |
                   ((uint64_t)BITS(hi, 0, 8) << 16) | ((uint64_t)BITS(hi, 32, 8) << 24);
        {
            uint32_t qb;
            memcpy(&qb, &gs.q, 4);
            gs.rgbaq |= (uint64_t)qb << 32;
        }
        break;
    case 0x2: { /* ST, and Q for the next RGBAQ */
        uint32_t qb = BITS(hi, 0, 32);
        gs.st = lo;
        memcpy(&gs.q, &qb, 4);
        break;
    }
    case 0x3: gs.uv = BITS(lo, 0, 14) | ((uint64_t)BITS(lo, 32, 14) << 16); break;
    case 0x4: { /* XYZF2 */
        uint64_t v = BITS(lo, 0, 16) | ((uint64_t)BITS(lo, 32, 16) << 16) |
                     ((uint64_t)BITS(hi, 4, 24) << 32) | ((uint64_t)BITS(hi, 36, 8) << 56);
        vertex_kick(v, 1, !BITS(hi, 47, 1));
        break;
    }
    case 0x5: { /* XYZ2 */
        uint64_t v = BITS(lo, 0, 16) | ((uint64_t)BITS(lo, 32, 16) << 16) |
                     ((uint64_t)BITS(hi, 0, 32) << 32);
        vertex_kick(v, 0, !BITS(hi, 47, 1));
        break;
    }
    case 0x6: case 0x7: case 0x8: case 0x9: gs_write_reg(reg, lo); break;
    case 0xa: gs.fog = (uint64_t)BITS(hi, 36, 8) << 56; break;
    case 0xc: case 0xd: gs_write_reg(reg, lo); break;
    case 0xe: gs_write_reg(BITS(hi, 0, 8), lo); break; /* A+D */
    default: break;
    }
}

static void gif_qword(uint64_t lo, uint64_t hi)
{
    if (!gif.in_tag) {
        gif.tag_lo = lo;
        gif.tag_hi = hi;
        gif.nloop = BITS(lo, 0, 15);
        gif.flg = BITS(lo, 58, 2);
        gif.nreg = BITS(lo, 60, 4);
        if (gif.nreg == 0)
            gif.nreg = 16;
        gif.reg = 0;
        gs.q = 1.0f;
        if (BITS(lo, 46, 1) && gif.flg == 0)
            gs_write_reg(0x00, BITS(lo, 47, 11));
        gif.in_tag = gif.nloop != 0;
        return;
    }

    switch (gif.flg) {
    case 0: /* PACKED */
        gif_packed(BITS(gif.tag_hi, gif.reg * 4, 4), lo, hi);
        if (++gif.reg >= gif.nreg) {
            gif.reg = 0;
            gif.nloop--;
        }
        break;
    case 1: { /* REGLIST: two registers per quadword */
        uint64_t data[2] = { lo, hi };
        for (int i = 0; i < 2 && gif.nloop; i++) {
            int r = BITS(gif.tag_hi, gif.reg * 4, 4);
            if (r != 0xe && r != 0xf) /* descriptors 0x0-0xd are register addresses */
                gs_write_reg(r, data[i]);
            if (++gif.reg >= gif.nreg) {
                gif.reg = 0;
                gif.nloop--;
            }
        }
        break;
    }
    default: { /* IMAGE */
        uint8_t q[16];
        memcpy(q, &lo, 8);
        memcpy(q + 8, &hi, 8);
        trx_bytes(q, 16);
        gif.nloop--;
        break;
    }
    }
    if (gif.nloop == 0)
        gif.in_tag = 0;
}

/* ---- Frame dumps ------------------------------------------------------- */

static const char dump_magic[8] = "CVXGSD1";
enum { DUMP_END, DUMP_GIF, DUMP_DISPLAY };
static FILE *dump_file;
static char dump_path[260];

void gs_dump_frame(const char *path)
{
    if (dump_file == NULL && dump_path[0] == 0)
        snprintf(dump_path, sizeof(dump_path), "%s", path);
}

static void dump_u32(uint32_t v)
{
    fwrite(&v, 4, 1, dump_file);
}

void gs_dump_vblank(void)
{
    /* Two V-blanks: the game may take two of them to draw a frame. */
    static int vblanks;
    batch_flush();
    if (dump_file != NULL && ++vblanks < 2)
        return;
    vblanks = 0;
    if (dump_file != NULL) {
        dump_u32(DUMP_END);
        fclose(dump_file);
        dump_file = NULL;
        fprintf(stderr, "gs: saved %s\n", dump_path);
        dump_path[0] = 0;
    } else if (dump_path[0] != 0) {
        gs_gpu_sync_all(); /* what the GPU drew, into the memory dumped */
        dump_file = fopen(dump_path, "wb");
        if (dump_file == NULL) {
            fprintf(stderr, "gs: cannot write %s\n", dump_path);
            dump_path[0] = 0;
            return;
        }
        fwrite(dump_magic, 8, 1, dump_file);
        dump_u32(sizeof(gs));
        dump_u32(sizeof(trx));
        dump_u32(sizeof(gif));
        fwrite(&gs, sizeof(gs), 1, dump_file);
        fwrite(&trx, sizeof(trx), 1, dump_file);
        fwrite(&gif, sizeof(gif), 1, dump_file);
        fwrite(gs_vram, GS_MEM_SIZE, 1, dump_file);
    }
}

static void gif_write(const uint64_t *qwords, uint32_t count);

int gs_replay(const char *path)
{
    FILE *f;
    char magic[8];
    uint32_t sizes[3], type, count;

    batch_flush();
    f = fopen(path, "rb");
    uint64_t *buf = NULL;
    int ok = 0;

    if (f == NULL)
        return -1;
    if (fread(magic, 8, 1, f) == 1 && memcmp(magic, dump_magic, 8) == 0 &&
        fread(sizes, 4, 3, f) == 3 && sizes[0] == sizeof(gs) && sizes[1] == sizeof(trx) &&
        sizes[2] == sizeof(gif) && fread(&gs, sizeof(gs), 1, f) == 1 &&
        fread(&trx, sizeof(trx), 1, f) == 1 && fread(&gif, sizeof(gif), 1, f) == 1 &&
        fread(gs_vram, GS_MEM_SIZE, 1, f) == 1) {
        changes++;
        tc_flush();
        gs_mark_all();
        gs_gpu_reset();
        while (fread(&type, 4, 1, f) == 1) {
            if (type == DUMP_GIF) {
                if (fread(&count, 4, 1, f) != 1)
                    break;
                buf = realloc(buf, (size_t)count * 16 + 16);
                if (buf == NULL || fread(buf, 16, count, f) != count)
                    break;
                gif_write(buf, count);
            } else if (type == DUMP_DISPLAY) {
                uint64_t d[2];
                if (fread(d, 8, 2, f) != 2)
                    break;
                gs_set_display(d[0], d[1]);
            } else {
                ok = type == DUMP_END;
                break;
            }
        }
    }
    free(buf);
    fclose(f);
    return ok ? 0 : -1;
}

static void gif_write(const uint64_t *qwords, uint32_t count)
{
    if (dump_file != NULL) {
        dump_u32(DUMP_GIF);
        dump_u32(count);
        fwrite(qwords, 16, count, dump_file);
    }
    for (uint32_t i = 0; i < count; i++)
        gif_qword(qwords[i * 2], qwords[i * 2 + 1]);
}

static int64_t busy_start(void)
{
    return gs_clock_ns != NULL ? gs_clock_ns() : 0;
}

static void busy_end(int64_t start)
{
    if (gs_clock_ns != NULL)
        gs_stats.busy_ns += gs_clock_ns() - start;
}

void gs_gif_write(const uint64_t *qwords, uint32_t count)
{
    int64_t t = busy_start();
    gif_write(qwords, count);
    busy_end(t);
}

/* ---- DMA ---------------------------------------------------------------- */

extern unsigned char port_scratchpad[0x4000];

void *gs_dma_pointer(uint32_t addr)
{
    if ((addr & 0x80000000u) || (addr & 0x70000000u) == 0x70000000u)
        return port_scratchpad + (addr & 0x3ff0);
    return (void *)(uintptr_t)(addr & 0x7ffffff0u);
}

void gs_dma_gif_normal(uint32_t madr, uint32_t qwc)
{
    gs_gif_write((const uint64_t *)gs_dma_pointer(madr), qwc);
}

static void dma_chain(uint32_t tadr);

void gs_dma_gif_chain(uint32_t tadr)
{
    int64_t t = busy_start();
    dma_chain(tadr);
    busy_end(t);
}

static void dma_chain(uint32_t tadr)
{
    uint32_t stack[2];
    int sp = 0;

    gs_stats.chains++;
    for (int guard = 0; guard < 1 << 20; guard++) {
        const uint64_t *tag = (const uint64_t *)gs_dma_pointer(tadr);
        uint64_t t = tag[0];
        uint32_t qwc = BITS(t, 0, 16);
        int id = BITS(t, 28, 3);
        uint32_t addr = BITS(t, 32, 31) | (BITS(t, 63, 1) << 31);
        uint32_t next = tadr + 16;

        switch (id) {
        case 0: /* refe: data at ADDR, then stop */
            gif_write((const uint64_t *)gs_dma_pointer(addr), qwc);
            return;
        case 1: /* cnt: data follows the tag */
            gif_write(tag + 2, qwc);
            next = tadr + 16 + qwc * 16;
            break;
        case 2: /* next: data follows, next tag at ADDR */
            gif_write(tag + 2, qwc);
            next = addr;
            break;
        case 3: case 4: /* ref, refs */
            gif_write((const uint64_t *)gs_dma_pointer(addr), qwc);
            break;
        case 5: /* call */
            gif_write(tag + 2, qwc);
            if (sp < 2)
                stack[sp++] = tadr + 16 + qwc * 16;
            next = addr;
            break;
        case 6: /* ret */
            gif_write(tag + 2, qwc);
            if (sp == 0)
                return;
            next = stack[--sp];
            break;
        default: /* end */
            gif_write(tag + 2, qwc);
            return;
        }
        tadr = next;
    }
    fprintf(stderr, "gs: DMA chain too long, stopped\n");
}

/* ---- Display ------------------------------------------------------------ */

void gs_set_display(uint64_t dispfb, uint64_t display)
{
    if (dump_file != NULL) {
        uint64_t d[2] = { dispfb, display };
        dump_u32(DUMP_DISPLAY);
        fwrite(d, 8, 2, dump_file);
    }
    gs.dispfb = dispfb;
    gs.display = display;
    changes++;
}

void gs_display_area(uint32_t *fbp, uint32_t *fbw, int *psm, int *w, int *h)
{
    int magh = BITS(gs.display, 23, 4) + 1;
    int width = (BITS(gs.display, 32, 12) + 1) / magh;
    int height = BITS(gs.display, 44, 11) + 1;

    *fbp = BITS(gs.dispfb, 0, 9) * 32;
    *fbw = BITS(gs.dispfb, 9, 6);
    *psm = BITS(gs.dispfb, 15, 5);
    if (*fbw == 0)
        *fbw = 10;
    if (width <= 1 || width > GS_DISPLAY_MAX_W)
        width = 640;
    if (height <= 1 || height > GS_DISPLAY_MAX_H)
        height = 480;
    *w = width;
    *h = height;
}

int gs_read_display(uint32_t *dst, int *w, int *h)
{
    uint32_t fbp, fbw;
    int psm, width, height;

    batch_flush();
    gs_display_area(&fbp, &fbw, &psm, &width, &height);
    *w = width;
    *h = height;
    if (gs_gpu_on) /* only for screenshots: always converted */
        gs_gpu_sync(psm, fbp, fbw, 0, 0, width - 1, height - 1);
    else if (changes == shown_changes)
        return 0;
    shown_changes = changes;
    if (psm == GS_PSMCT32 || psm == GS_PSMCT24) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                dst[y * width + x] = gs_rd32(gs_addr32(0, fbp, fbw, x, y)) | 0xff000000u;
        return 1;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t c = gs_read_pixel_nosync(psm, fbp, fbw, x, y);
            if (psm == GS_PSMCT16 || psm == GS_PSMCT16S)
                c = expand16(c, 0);
            dst[y * width + x] = c | 0xff000000u;
        }
    }
    return 1;
}

void gs_reset(void)
{
    batch_flush();
    memset(gs_vram, 0, sizeof(gs_vram));
    memset(&gs, 0, sizeof(gs));
    memset(&trx, 0, sizeof(trx));
    memset(&gif, 0, sizeof(gif));
    changes++;
    tc_flush();
    gs_mark_all();
    gs_gpu_reset();
    gs.q = 1.0f;
    gs.prmodecont = 1;
    gs.colclamp = 1;
}

void gs_debug_status(char *buf, int size)
{
    uint32_t fbp = BITS(gs.dispfb, 0, 9);
    uint32_t fbw = BITS(gs.dispfb, 9, 6);
    int psm = BITS(gs.dispfb, 15, 5), lit = 0;
    GsStats st = gs_stats;

    memset(&gs_stats, 0, sizeof(gs_stats));
    for (int y = 0; y < 480 && !gs_gpu_on; y += 4)
        for (int x = 0; x < 640; x += 4)
            lit += (gs_read_pixel_nosync(psm, fbp * 32, fbw ? fbw : 10, x, y) & 0xffffff) != 0;
    if (gs_gpu_on) /* GS memory is not up to date: the pictures are on the GPU */
        snprintf(buf, size, "gs: %u ms, %u chains, %u uploads, %u prims (gpu) | display page %u, width %u | "
                 "draw page %u", (unsigned)(st.busy_ns / 1000000), st.chains, st.uploads, st.prims, fbp, fbw * 64,
                 BITS(gs.frame[0], 0, 9));
    else
        snprintf(buf, size, "gs: %u ms, %u chains, %u uploads, %u prims, %u pixels | display page %u, "
                 "width %u, %d%% non-black | draw page %u",
                 (unsigned)(st.busy_ns / 1000000), st.chains, st.uploads, st.prims, st.pixels, fbp,
                 fbw * 64, lit * 100 / (160 * 120),
                 BITS(gs.frame[0], 0, 9));
}
