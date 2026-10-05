/*
 * Software GS: GIF packet parsing, register state, image transfers and the
 * pixel pipeline (see gs.h). References: GS User's Manual (registers,
 * drawing and texture functions) and the EE User's Manual (GIF, DMA).
 */
#include "gs.h"
#include "gs_mem.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GsStats gs_stats;

/* Extracts bits [lo, lo+n) of v. */
#define BITS(v, lo, n) ((uint32_t)(((v) >> (lo)) & ((1ull << (n)) - 1)))

/* ---- Register state ---------------------------------------------------- */

typedef struct {
    int x, y;       /* window coordinates, 12.4 fixed point (offset applied) */
    uint32_t z;
    int f;          /* fog coefficient */
    int r, g, b, a;
    float s, t, q;
    int u, v;       /* 10.4 fixed point texel coordinates */
} Vertex;

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

static uint32_t expand24(uint32_t c)
{
    int a = (BITS(gs.texa, 15, 1) && (c & 0xffffff) == 0) ? 0 : BITS(gs.texa, 0, 8);
    return (c & 0xffffff) | ((uint32_t)a << 24);
}

/* ---- CLUT -------------------------------------------------------------- */

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
            gs.clut[i] = gs_read_pixel(cpsm == 0 ? GS_PSMCT32 : cpsm, cbp, 1, x, y);
        }
    } else {
        /* CSM2: a plain row at TEXCLUT's offset. */
        uint32_t cbw = BITS(gs.texclut, 0, 6), cou = BITS(gs.texclut, 6, 6) * 16;
        uint32_t cov = BITS(gs.texclut, 12, 10);
        for (int i = 0; i < entries; i++)
            gs.clut[i] = gs_read_pixel(cpsm == 0 ? GS_PSMCT32 : cpsm, cbp, cbw, cou + i, cov);
    }
}

/* ---- Image transfers ---------------------------------------------------- */

static void trx_start(void)
{
    int dir = BITS(gs.trxdir, 0, 2);

    trx.active = 0;
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
    } else if (dir == 2) {
        /* Local -> local copy, through a temporary buffer. */
        int spsm = BITS(gs.bitbltbuf, 24, 6), dpsm = BITS(gs.bitbltbuf, 56, 6);
        uint32_t sbp = BITS(gs.bitbltbuf, 0, 14), sbw = BITS(gs.bitbltbuf, 16, 6);
        uint32_t dbp = BITS(gs.bitbltbuf, 32, 14), dbw = BITS(gs.bitbltbuf, 48, 6);
        int sx = BITS(gs.trxpos, 0, 11), sy = BITS(gs.trxpos, 16, 11);
        int dx = BITS(gs.trxpos, 32, 11), dy = BITS(gs.trxpos, 48, 11);
        int w = BITS(gs.trxreg, 0, 12), h = BITS(gs.trxreg, 32, 12);
        static uint32_t tmp[2048 * 64];

        for (int y0 = 0; y0 < h; y0 += 64) {
            int rows = h - y0 < 64 ? h - y0 : 64;
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < w && x < 2048; x++)
                    tmp[y * 2048 + x] = gs_read_pixel(spsm, sbp, sbw, (sx + x) & 2047, (sy + y0 + y) & 2047);
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < w && x < 2048; x++)
                    gs_write_pixel(dpsm, dbp, dbw, (dx + x) & 2047, (dy + y0 + y) & 2047, tmp[y * 2048 + x]);
        }
    }
}

static void trx_put(uint32_t v)
{
    if (trx.y >= trx.h)
        return;
    gs_write_pixel(trx.psm, trx.bp, trx.bw, (trx.x0 + trx.x) & 2047, (trx.y0 + trx.y) & 2047, v);
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

/* ---- Texture sampling -------------------------------------------------- */

static int wrap_coord(int c, int size, int mode, int minv, int maxv)
{
    switch (mode) {
    case 0: return c & (size - 1);                       /* REPEAT */
    case 1: return c < 0 ? 0 : c >= size ? size - 1 : c;  /* CLAMP */
    case 2: return c < minv ? minv : c > maxv ? maxv : c; /* REGION_CLAMP */
    default: return (c & minv) | maxv;                    /* REGION_REPEAT */
    }
}

static uint32_t texel(int ctx, int u, int v)
{
    uint64_t tex0 = gs.tex0[ctx], clamp = gs.clamp[ctx];
    int psm = BITS(tex0, 20, 6);
    uint32_t tbp = BITS(tex0, 0, 14), tbw = BITS(tex0, 14, 6);
    int tw = 1 << BITS(tex0, 26, 4), th = 1 << BITS(tex0, 30, 4);
    int cpsm = BITS(tex0, 51, 4);
    uint32_t c;

    u = wrap_coord(u, tw, BITS(clamp, 0, 2), BITS(clamp, 4, 10), BITS(clamp, 14, 10));
    v = wrap_coord(v, th, BITS(clamp, 2, 2), BITS(clamp, 24, 10), BITS(clamp, 34, 10));

    switch (psm) {
    case GS_PSMCT32:
        return gs_read_pixel(psm, tbp, tbw, u & 2047, v & 2047);
    case GS_PSMCT24:
        return expand24(gs_read_pixel(psm, tbp, tbw, u & 2047, v & 2047));
    case GS_PSMCT16: case GS_PSMCT16S:
        return expand16(gs_read_pixel(psm, tbp, tbw, u & 2047, v & 2047), 1);
    case GS_PSMT8: case GS_PSMT8H: case GS_PSMT4: case GS_PSMT4HL: case GS_PSMT4HH: {
        uint32_t idx = gs_read_pixel(psm, tbp, tbw, u & 2047, v & 2047);
        if (psm == GS_PSMT4 || psm == GS_PSMT4HL || psm == GS_PSMT4HH)
            idx += BITS(tex0, 56, 5) * 16; /* CSA */
        c = gs.clut[idx & 511];
        return cpsm == 0 ? c : expand16(c, 1);
    }
    }
    return 0;
}

static uint32_t sample(int ctx, int u16, int v16)
{
    uint64_t tex1 = gs.tex1[ctx];

    if (!BITS(tex1, 5, 1)) /* MMAG: nearest */
        return texel(ctx, u16 >> 4, v16 >> 4);

    /* Bilinear: texel centres are at +0.5. */
    int us = u16 - 8, vs = v16 - 8;
    int u0 = us >> 4, v0 = vs >> 4, fu = us & 15, fv = vs & 15;
    uint32_t c00 = texel(ctx, u0, v0), c10 = texel(ctx, u0 + 1, v0);
    uint32_t c01 = texel(ctx, u0, v0 + 1), c11 = texel(ctx, u0 + 1, v0 + 1);
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int a = (c00 >> sh) & 255, b = (c10 >> sh) & 255, c = (c01 >> sh) & 255, d = (c11 >> sh) & 255;
        int top = a * (16 - fu) + b * fu, bot = c * (16 - fu) + d * fu;
        out |= (uint32_t)((top * (16 - fv) + bot * fv) >> 8) << sh;
    }
    return out;
}

/* ---- Pixel pipeline ---------------------------------------------------- */

typedef struct {
    int ctx;
    int tme, fge, abe, fst, iip;
    int fpsm, zpsm;
    uint32_t fbp, fbw, zbp, fbmsk;
    int zmsk;
    uint64_t test, alpha;
    int scax0, scax1, scay0, scay1;
    int tfx, tcc;
    int fba, colclamp, pabe;
    int fr, fg, fb_; /* fog colour */
} DrawState;

static DrawState ds;

static void setup_draw(void)
{
    uint64_t attrs = BITS(gs.prmodecont, 0, 1) ? gs.prim : gs.prmode;
    int ctx = BITS(attrs, 9, 1);

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
    ds.zbp = BITS(gs.zbuf[ctx], 0, 9) * 32;
    ds.zpsm = BITS(gs.zbuf[ctx], 24, 4) | 0x30;
    ds.zmsk = BITS(gs.zbuf[ctx], 32, 1);
    ds.test = gs.test[ctx];
    ds.alpha = gs.alpha[ctx];
    ds.scax0 = BITS(gs.scissor[ctx], 0, 11);
    ds.scax1 = BITS(gs.scissor[ctx], 16, 11);
    ds.scay0 = BITS(gs.scissor[ctx], 32, 11);
    ds.scay1 = BITS(gs.scissor[ctx], 48, 11);
    ds.tfx = BITS(gs.tex0[ctx], 35, 2);
    ds.tcc = BITS(gs.tex0[ctx], 34, 1);
    ds.fba = BITS(gs.fba[ctx], 0, 1);
    ds.colclamp = BITS(gs.colclamp, 0, 1);
    ds.pabe = BITS(gs.pabe, 0, 1);
    ds.fr = BITS(gs.fogcol, 0, 8);
    ds.fg = BITS(gs.fogcol, 8, 8);
    ds.fb_ = BITS(gs.fogcol, 16, 8);
}

static int compare(int method, int a, int ref)
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

static uint32_t read_frame(int x, int y)
{
    uint32_t c = gs_read_pixel(ds.fpsm, ds.fbp, ds.fbw, x, y);

    if (ds.fpsm == GS_PSMCT16 || ds.fpsm == GS_PSMCT16S)
        return expand16(c, 0);
    if (ds.fpsm == GS_PSMCT24)
        return c | 0x80000000u;
    return c;
}

/* Shades and writes one pixel. Colour components are 0..255; u, v are
 * texel coordinates in 1/16. */
static void draw_pixel(int x, int y, uint32_t z, int r, int g, int b, int a, int f, int u, int v)
{
    int write_rgb = 1, write_a = 1, write_z = !ds.zmsk;

    if (x < ds.scax0 || x > ds.scax1 || y < ds.scay0 || y > ds.scay1)
        return;

    if (ds.tme) {
        uint32_t t = sample(ds.ctx, u, v);
        int tr = t & 255, tg = (t >> 8) & 255, tb = (t >> 16) & 255, ta = t >> 24;
        switch (ds.tfx) {
        case 0: /* MODULATE */
            r = (tr * r) >> 7; g = (tg * g) >> 7; b = (tb * b) >> 7;
            if (ds.tcc) a = (ta * a) >> 7;
            break;
        case 1: /* DECAL */
            r = tr; g = tg; b = tb;
            if (ds.tcc) a = ta;
            break;
        case 2: /* HIGHLIGHT */
            r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a;
            if (ds.tcc) a = ta + a;
            break;
        default: /* HIGHLIGHT2 */
            r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a;
            if (ds.tcc) a = ta;
            break;
        }
        r = clamp255(r); g = clamp255(g); b = clamp255(b); a = clamp255(a);
    }

    if (ds.fge) {
        r = (f * r + (255 - f) * ds.fr) >> 8;
        g = (f * g + (255 - f) * ds.fg) >> 8;
        b = (f * b + (255 - f) * ds.fb_) >> 8;
    }

    /* Alpha test */
    if (BITS(ds.test, 0, 1) && !compare(BITS(ds.test, 1, 3), a, BITS(ds.test, 4, 8))) {
        switch (BITS(ds.test, 12, 2)) {
        case 0: return;                            /* KEEP */
        case 1: write_z = 0; break;                /* FB_ONLY */
        case 2: write_rgb = write_a = 0; break;    /* ZB_ONLY */
        default: write_a = 0; write_z = 0; break;  /* RGB_ONLY */
        }
    }

    /* Depth test */
    if (BITS(ds.test, 16, 1)) {
        int ztst = BITS(ds.test, 17, 2);
        if (ztst == 0)
            return;
        if (ztst >= 2) {
            uint32_t zmax = (ds.zpsm == GS_PSMZ16 || ds.zpsm == GS_PSMZ16S) ? 0xffff
                          : ds.zpsm == GS_PSMZ24 ? 0xffffff : 0xffffffff;
            uint32_t zd = gs_read_pixel(ds.zpsm, ds.zbp, ds.fbw, x, y);
            if (z > zmax)
                z = zmax;
            if (ztst == 2 ? z < zd : z <= zd)
                return;
        }
    } else {
        write_z = 0;
    }

    uint32_t dst = read_frame(x, y);

    /* Destination alpha test */
    if (BITS(ds.test, 14, 1) && ds.fpsm != GS_PSMCT24) {
        int dbit = (dst >> 31) & 1;
        if (dbit != (int)BITS(ds.test, 15, 1))
            return;
    }

    if (ds.abe && !(ds.pabe && a < 0x80)) {
        int cd[3] = { dst & 255, (dst >> 8) & 255, (dst >> 16) & 255 };
        int cs[3] = { r, g, b };
        int ad = dst >> 24;
        int sa = BITS(ds.alpha, 0, 2), sb = BITS(ds.alpha, 2, 2);
        int sc = BITS(ds.alpha, 4, 2), sd = BITS(ds.alpha, 6, 2);
        int fix = BITS(ds.alpha, 32, 8);
        int c = sc == 0 ? a : sc == 1 ? ad : fix;
        int out[3];
        for (int i = 0; i < 3; i++) {
            int va = sa == 0 ? cs[i] : sa == 1 ? cd[i] : 0;
            int vb = sb == 0 ? cs[i] : sb == 1 ? cd[i] : 0;
            int vd = sd == 0 ? cs[i] : sd == 1 ? cd[i] : 0;
            out[i] = (((va - vb) * c) >> 7) + vd;
        }
        r = out[0]; g = out[1]; b = out[2];
    }

    if (ds.colclamp) {
        r = clamp255(r); g = clamp255(g); b = clamp255(b);
    } else {
        r &= 255; g &= 255; b &= 255;
    }
    a &= 255;
    if (ds.fba)
        a |= 0x80;

    uint32_t src = rgba(r, g, b, a);
    if (!write_rgb)
        src = (src & 0xff000000u) | (dst & 0x00ffffffu);
    if (!write_a)
        src = (src & 0x00ffffffu) | (dst & 0xff000000u);
    src = (src & ~ds.fbmsk) | (dst & ds.fbmsk);

    if (write_rgb || write_a) {
        switch (ds.fpsm) {
        case GS_PSMCT32:
            gs_write_pixel(GS_PSMCT32, ds.fbp, ds.fbw, x, y, src);
            break;
        case GS_PSMCT24:
            gs_write_pixel(GS_PSMCT24, ds.fbp, ds.fbw, x, y, src & 0xffffff);
            break;
        default:
            gs_write_pixel(ds.fpsm, ds.fbp, ds.fbw, x, y,
                           ((src >> 3) & 31) | (((src >> 11) & 31) << 5) |
                           (((src >> 19) & 31) << 10) | ((src >> 31) << 15));
            break;
        }
    }
    if (write_z)
        gs_write_pixel(ds.zpsm, ds.zbp, ds.fbw, x, y, z);
    gs_stats.pixels++;
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

static void draw_sprite(const Vertex *a, const Vertex *b)
{
    int x0 = a->x, x1 = b->x, y0 = a->y, y1 = b->y;
    float u0, v0, u1, v1;

    tex_coords(a, &u0, &v0);
    tex_coords(b, &u1, &v1);
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; float f = u0; u0 = u1; u1 = f; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; float f = v0; v0 = v1; v1 = f; }

    /* Pixels whose top-left corner lies in [x0, x1) x [y0, y1). */
    int px0 = (x0 + 15) >> 4, px1 = (x1 + 15) >> 4;
    int py0 = (y0 + 15) >> 4, py1 = (y1 + 15) >> 4;
    float du = x1 != x0 ? (u1 - u0) / (float)(x1 - x0) : 0.0f;
    float dv = y1 != y0 ? (v1 - v0) / (float)(y1 - y0) : 0.0f;

    if (px0 < ds.scax0) px0 = ds.scax0;
    if (py0 < ds.scay0) py0 = ds.scay0;
    if (px1 > ds.scax1 + 1) px1 = ds.scax1 + 1;
    if (py1 > ds.scay1 + 1) py1 = ds.scay1 + 1;

    /* Sprites take their colour, z and fog from the second vertex. */
    for (int py = py0; py < py1; py++) {
        float v = v0 + ((py << 4) - y0) * dv;
        for (int px = px0; px < px1; px++) {
            float u = u0 + ((px << 4) - x0) * du;
            draw_pixel(px, py, b->z, b->r, b->g, b->b, b->a, b->f, (int)u, (int)v);
        }
    }
}

static int64_t edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (int64_t)(bx - ax) * (py - ay) - (int64_t)(by - ay) * (px - ax);
}

static void draw_triangle(const Vertex *v0, const Vertex *v1, const Vertex *v2)
{
    const Vertex *v[3] = { v0, v1, v2 };
    int64_t area = edge(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y);

    if (area == 0)
        return;
    if (area < 0) {
        const Vertex *t = v[1]; v[1] = v[2]; v[2] = t;
        area = -area;
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

    /* Per-vertex attributes; texture coordinates are interpolated as s/q, t/q
     * and 1/q for perspective correction (STQ), or linearly (UV). */
    float att[3][9];
    for (int i = 0; i < 3; i++) {
        const Vertex *p = v[i];
        float q = (ds.fst || p->q == 0.0f) ? 1.0f : p->q;
        float u, w;
        if (ds.fst) {
            u = (float)p->u;
            w = (float)p->v;
        } else {
            int tw = 1 << BITS(gs.tex0[ds.ctx], 26, 4), th = 1 << BITS(gs.tex0[ds.ctx], 30, 4);
            u = p->s * tw * 16.0f;
            w = p->t * th * 16.0f;
        }
        att[i][0] = (float)p->r; att[i][1] = (float)p->g; att[i][2] = (float)p->b;
        att[i][3] = (float)p->a; att[i][4] = (float)p->z; att[i][5] = (float)p->f;
        att[i][6] = u; att[i][7] = w; att[i][8] = ds.fst ? 1.0f : q;
    }
    /* Flat shading uses the colour of the last vertex sent. */
    const Vertex *last = v2;

    for (int py = py0; py <= py1; py++) {
        int sy = py << 4;
        for (int px = px0; px <= px1; px++) {
            int sx = px << 4;
            int64_t w0 = edge(v[1]->x, v[1]->y, v[2]->x, v[2]->y, sx, sy);
            int64_t w1 = edge(v[2]->x, v[2]->y, v[0]->x, v[0]->y, sx, sy);
            int64_t w2 = edge(v[0]->x, v[0]->y, v[1]->x, v[1]->y, sx, sy);
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            float b0 = (float)w0 / area, b1 = (float)w1 / area, b2 = (float)w2 / area;
            float at[9];
            for (int k = 0; k < 9; k++)
                at[k] = att[0][k] * b0 + att[1][k] * b1 + att[2][k] * b2;
            float qq = at[8] != 0.0f ? at[8] : 1.0f;
            int r, g, b, a;
            if (ds.iip) {
                r = (int)at[0]; g = (int)at[1]; b = (int)at[2]; a = (int)at[3];
            } else {
                r = last->r; g = last->g; b = last->b; a = last->a;
            }
            draw_pixel(px, py, (uint32_t)at[4], r, g, b, a, (int)at[5],
                       (int)(at[6] / qq), (int)(at[7] / qq));
        }
    }
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
        setup_draw();
        gs_stats.prims++;
        switch (type) {
        case 0: draw_pixel(v.x >> 4, v.y >> 4, v.z, v.r, v.g, v.b, v.a, v.f, v.u, v.v); break;
        case 1: case 2: draw_line(&gs.queue[0], &gs.queue[1]); break;
        case 3: case 4: case 5: draw_triangle(&gs.queue[0], &gs.queue[1], &gs.queue[2]); break;
        case 6: draw_sprite(&gs.queue[0], &gs.queue[1]); break;
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

void gs_gif_write(const uint64_t *qwords, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        gif_qword(qwords[i * 2], qwords[i * 2 + 1]);
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

void gs_dma_gif_chain(uint32_t tadr)
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
            gs_gif_write((const uint64_t *)gs_dma_pointer(addr), qwc);
            return;
        case 1: /* cnt: data follows the tag */
            gs_gif_write(tag + 2, qwc);
            next = tadr + 16 + qwc * 16;
            break;
        case 2: /* next: data follows, next tag at ADDR */
            gs_gif_write(tag + 2, qwc);
            next = addr;
            break;
        case 3: case 4: /* ref, refs */
            gs_gif_write((const uint64_t *)gs_dma_pointer(addr), qwc);
            break;
        case 5: /* call */
            gs_gif_write(tag + 2, qwc);
            if (sp < 2)
                stack[sp++] = tadr + 16 + qwc * 16;
            next = addr;
            break;
        case 6: /* ret */
            gs_gif_write(tag + 2, qwc);
            if (sp == 0)
                return;
            next = stack[--sp];
            break;
        default: /* end */
            gs_gif_write(tag + 2, qwc);
            return;
        }
        tadr = next;
    }
    fprintf(stderr, "gs: DMA chain too long, stopped\n");
}

/* ---- Display ------------------------------------------------------------ */

void gs_set_display(uint64_t dispfb, uint64_t display)
{
    gs.dispfb = dispfb;
    gs.display = display;
}

void gs_read_display(uint32_t *dst, int *w, int *h)
{
    uint32_t fbp = BITS(gs.dispfb, 0, 9) * 32;
    uint32_t fbw = BITS(gs.dispfb, 9, 6);
    int psm = BITS(gs.dispfb, 15, 5);
    int magh = BITS(gs.display, 23, 4) + 1;
    int width = (BITS(gs.display, 32, 12) + 1) / magh;
    int height = BITS(gs.display, 44, 11) + 1;

    if (fbw == 0)
        fbw = 10;
    if (width <= 1 || width > GS_DISPLAY_MAX_W)
        width = 640;
    if (height <= 1 || height > GS_DISPLAY_MAX_H)
        height = 480;
    *w = width;
    *h = height;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t c = gs_read_pixel(psm, fbp, fbw, x, y);
            if (psm == GS_PSMCT16 || psm == GS_PSMCT16S)
                c = expand16(c, 0);
            dst[y * width + x] = c | 0xff000000u;
        }
    }
}

void gs_reset(void)
{
    memset(gs_vram, 0, sizeof(gs_vram));
    memset(&gs, 0, sizeof(gs));
    memset(&trx, 0, sizeof(trx));
    memset(&gif, 0, sizeof(gif));
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
    for (int y = 0; y < 480; y += 4)
        for (int x = 0; x < 640; x += 4)
            lit += (gs_read_pixel(psm, fbp * 32, fbw ? fbw : 10, x, y) & 0xffffff) != 0;
    snprintf(buf, size, "gs: %u chains, %u uploads, %u prims, %u pixels | display page %u, "
             "width %u, %d%% non-black | draw page %u",
             st.chains, st.uploads, st.prims, st.pixels, fbp, fbw * 64, lit * 100 / (160 * 120),
             BITS(gs.frame[0], 0, 9));
}
