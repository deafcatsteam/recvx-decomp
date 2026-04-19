/*
 * game_texture_stubs.c — texture-state nj* stubs that need to see the REAL
 * NJS_TEXMEMLIST / NJS_TEXLIST layout as defined by ninjastr.h.
 *
 * Why not in port/src/stubs/stub_game.c?
 *   Stubs in recvx_port_stubs intentionally do NOT pull KATANA headers
 *   (CMakeLists.txt comment at add_library(recvx_port_stubs ...)). The
 *   texture pipeline needs to READ/WRITE NJS_TEXMEMLIST fields whose offsets
 *   come from ninjastr.h, so this file is compiled into recvx_game where
 *   the KATANA include path + prelude are active.
 *
 * Crash this fixes (Warning mode 17):
 *   SetQuadUv2Ex@adv.c:744 does:
 *     temp = (NJS_TEXMEMLIST*)AdvTexList[ListNo].textures[TexNo].texaddr;
 *     TexX = temp->texinfo.texsurface.nWidth;   // AV: temp == NULL
 *   Our previous njLoadTexture was a no-op, so `.texaddr` stayed zeroed
 *   and the first draw after the fade started dereffed NULL.
 *
 * x64 pointer-width gotcha:
 *   NJS_TEXNAME.texaddr is Uint32 (4 bytes). On PS2 this holds a full
 *   32-bit VRAM pointer; on x64 it can't hold an 8-byte heap pointer.
 *   We allocate the TEXMEMLIST pool in the low 4 GiB of VA via
 *   VirtualAlloc2 + MEM_ADDRESS_REQUIREMENTS so pool addresses fit in
 *   Uint32 without truncation.
 */

#include "ninja.h"
#include "types.h"          /* TIM2_PICTUREHEADER / QUAD */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* recvx_port.h is on the port-stubs include path but NOT on recvx_game's
 * include path (game target is PRIVATE). Forward-declare the log entry
 * point + gfx API we call into so we can still hit them without exposing
 * the port header (which would drag in port-side SDL / stdbool typedefs
 * that conflict with KATANA's own shadows). */
extern void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)

typedef struct recvx_gfx_vtx {
    float    x, y, z;
    uint32_t color;
} recvx_gfx_vtx;
extern void recvx_gfx_tex_upload(int slot, const void* rgba, int w, int h);
extern void recvx_gfx_draw_quad(int slot,
                                float x1, float y1, float x2, float y2,
                                float u1, float v1, float u2, float v2,
                                float z, uint32_t color, int trans);
extern void recvx_gfx_draw_polygon(const recvx_gfx_vtx* v, int n, int trans);
extern void recvx_gfx_set_filter(int mode);

/* Ps2_current_texmemlist is typed in ps2_NaTextureFunction.h as
 * `extern NJS_TEXMEMLIST*`. adv.c:695 dereferences it on the TransFlag==1
 * path: casts texsurface.pSurface to TIM2_PICTUREHEADER_EX* and ORs
 * ClutChange with 0x8000. Needs to point at a real pool slot, not NULL. */
NJS_TEXMEMLIST* Ps2_current_texmemlist;

/* ------------------------------------------------------------------ */
/* Low-4GiB TEXMEMLIST pool                                           */
/* ------------------------------------------------------------------ */

#define TEX_POOL_SLOTS 64

static NJS_TEXMEMLIST* g_tex_pool;
static int             g_tex_pool_used;

/* The Win32 VirtualAlloc2 allocator lives in port/src/tex_pool_alloc.c so
 * <windows.h> doesn't collide with types.h (both define a POINT struct).
 * We just consume the raw bytes here. */
extern void* recvx_alloc_low4g(size_t bytes);

static void ensure_pool(void) {
    if (g_tex_pool) return;
    size_t bytes = TEX_POOL_SLOTS * sizeof(NJS_TEXMEMLIST);
    g_tex_pool = (NJS_TEXMEMLIST*)recvx_alloc_low4g(bytes);
    if (g_tex_pool) {
        memset(g_tex_pool, 0, bytes);
        RX_LOG("tex", "pool base=%p size=%zu (must be <4GiB for Uint32 texaddr fit)",
               g_tex_pool, bytes);
    } else {
        RX_LOG("tex", "POOL ALLOC FAILED");
    }
}

static NJS_TEXMEMLIST* pool_get(void) {
    ensure_pool();
    if (!g_tex_pool || g_tex_pool_used >= TEX_POOL_SLOTS) return NULL;
    return &g_tex_pool[g_tex_pool_used++];
}

/* Map a pool NJS_TEXMEMLIST back to its 0..63 slot index — inverse of
 * pool_get's ++g_tex_pool_used. Used by njSetQuadTexture to tell the
 * backend which GL texture to bind. Returns -1 if ml is outside pool. */
static int pool_slot_of(const NJS_TEXMEMLIST* ml) {
    if (!ml || !g_tex_pool) return -1;
    ptrdiff_t diff = ml - g_tex_pool;
    if (diff < 0 || diff >= TEX_POOL_SLOTS) return -1;
    return (int)diff;
}

/* ------------------------------------------------------------------ */
/* TIM2 decoder — decode a raw Sofdec/CRI TIM2 blob into RGBA8.       */
/*                                                                    */
/* Layout (see SetPvrInfo @ adv.c:576):                               */
/*   pp[0..0x7F]   : TIM2_PICTUREHEADER_EX  (game-extended FILE hdr)  */
/*   pp[0x80..]    : TIM2_PICTUREHEADER     (real picture header)     */
/*   pp[0x80+Hdr]  : image data                                       */
/*   pp[.. + Img]  : CLUT data                                        */
/*                                                                    */
/* PS2 alpha is 0..128 (128 = full). Multiply by 2 and saturate so    */
/* semi-opaque logos don't come out at 50% on screen.                 */
/* ------------------------------------------------------------------ */

#define TIM2_MAX_W   1024
#define TIM2_MAX_H   1024

static uint8_t g_decode_scratch[TIM2_MAX_W * TIM2_MAX_H * 4];

/* Implemented in port/src/tex_dump.c (recvx_port_stubs target — that file
 * has the real MSVC stdio without KATANA's SH-series shadow blocking FILE).
 * Gated by RECVX_DUMP_TEX=1 env var; writes port/debug/tex/tex_NN_WxH.tga. */
extern void recvx_dump_rgba_tga(int slot, int w, int h, const void* rgba);

static inline uint8_t alpha_ps2_to_pc(uint8_t a) {
    int x = (int)a * 2;
    return (uint8_t)(x > 255 ? 255 : x);
}

/* CSM2 -> CSM1 index remap for 8-bit paletted textures. PS2 GS reads the
 * 256-entry palette in 8x2 blocks striped across 16x16; if the CLUT is
 * stored CSM1 (linear), the pixel INDEX needs bits 3 and 4 swapped before
 * lookup. Detected via ClutType bit 0x80.
 *   i = iiiBAiii ->  out = iiiABiii */
static inline uint8_t idx_csm1_swap(uint8_t i) {
    return (uint8_t)((i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1));
}

/* Returns 1 on success with w/h filled and g_decode_scratch populated.
 * Returns 0 for any unsupported format (caller uploads a 1x1 placeholder). */
static int tim2_decode(const void* blob, int* out_w, int* out_h,
                       uint8_t** out_pixels) {
    if (!blob) return 0;
    const uint8_t* pp = (const uint8_t*)blob;

    /* Magic check on the EX header. Game always prepends 128B incl. "TIM2". */
    if (pp[0] != 'T' || pp[1] != 'I' || pp[2] != 'M' || pp[3] != '2') {
        RX_LOG("tim2", "decode: bad magic %02x%02x%02x%02x",
               pp[0], pp[1], pp[2], pp[3]);
        return 0;
    }

    const TIM2_PICTUREHEADER* ph = (const TIM2_PICTUREHEADER*)(pp + 128);
    int w = (int)ph->ImageWidth;
    int h = (int)ph->ImageHeight;
    if (w <= 0 || h <= 0 || w > TIM2_MAX_W || h > TIM2_MAX_H) {
        RX_LOG("tim2", "decode: bogus size %dx%d", w, h);
        return 0;
    }
    const uint8_t* img  = pp + 128 + ph->HeaderSize;
    const uint8_t* clut = img + ph->ImageSize;
    uint8_t* dst = g_decode_scratch;

    int image_type = ph->ImageType;
    int clut_type  = ph->ClutType;
    int csm1       = (clut_type & 0x80) != 0;  /* CLUT in linear order? */
    int clut_fmt   = clut_type & 0x07;         /* 1=A1BGR5 2=XBGR24 3=ABGR32 */

    RX_LOG("tim2", "decode %dx%d imgT=%d clutT=0x%02x hdr=%u img=%u clut=%u",
           w, h, image_type, (unsigned)ph->ClutType,
           (unsigned)ph->HeaderSize, (unsigned)ph->ImageSize,
           (unsigned)ph->ClutSize);

    if (image_type == 3) {
        /* ABGR32 direct. Memory order R,G,B,A — matches GL_RGBA. */
        for (int i = 0; i < w * h; ++i) {
            dst[i*4+0] = img[i*4+0];
            dst[i*4+1] = img[i*4+1];
            dst[i*4+2] = img[i*4+2];
            dst[i*4+3] = alpha_ps2_to_pc(img[i*4+3]);
        }
    } else if (image_type == 5) {
        /* 8-bit paletted. Palette expected to be 256 entries. */
        if (clut_fmt != 3) {
            RX_LOG("tim2", "unsupported clut_fmt=%d (want ABGR32)", clut_fmt);
            return 0;
        }
        for (int i = 0; i < w * h; ++i) {
            uint8_t idx = img[i];
            if (csm1) idx = idx_csm1_swap(idx);
            const uint8_t* pal = clut + idx * 4;
            dst[i*4+0] = pal[0];
            dst[i*4+1] = pal[1];
            dst[i*4+2] = pal[2];
            dst[i*4+3] = alpha_ps2_to_pc(pal[3]);
        }
    } else if (image_type == 4) {
        /* 4-bit paletted. Two pixels per byte, PS2 packs low nibble first
         * (pixel 0 = low nibble, pixel 1 = high nibble). Palette is
         * nominally 16 entries; TIM2 files sometimes pad ClutSize to 32
         * (128 bytes @ ABGR32) — we only index 0..15 so padding is inert.
         * No CSM bit-swap: 16-entry palettes are read linearly by the GS. */
        if (clut_fmt != 3) {
            RX_LOG("tim2", "unsupported clut_fmt=%d for 4bpp (want ABGR32)",
                   clut_fmt);
            return 0;
        }
        for (int i = 0; i < w * h; ++i) {
            uint8_t byte   = img[i >> 1];
            uint8_t nibble = (i & 1) ? (uint8_t)(byte >> 4)
                                     : (uint8_t)(byte & 0x0F);
            const uint8_t* pal = clut + nibble * 4;
            dst[i*4+0] = pal[0];
            dst[i*4+1] = pal[1];
            dst[i*4+2] = pal[2];
            dst[i*4+3] = alpha_ps2_to_pc(pal[3]);
        }
    } else {
        RX_LOG("tim2", "unsupported image_type=%d", image_type);
        return 0;
    }

    *out_w      = w;
    *out_h      = h;
    *out_pixels = dst;
    return 1;
}

/* ------------------------------------------------------------------ */
/* nj* texture entry points                                           */
/* ------------------------------------------------------------------ */

/* adv.c:589 SetPvrInfo passes (ip, pp, fmt, w, h). The real Ninja writes
 * every field of NJS_TEXINFO; for our black-box stub only nWidth/nHeight
 * matter because that's what SetQuadUv2Ex reads back. */
void njSetTextureInfo(NJS_TEXINFO* ti, Uint16* data, Sint32 Type,
                      Sint32 w, Sint32 h) {
    RX_LOG("nj", "njSetTextureInfo ti=%p data=%p type=%d w=%d h=%d",
           (void*)ti, (void*)data, (int)Type, (int)w, (int)h);
    if (!ti) return;
    ti->texaddr                = data;
    ti->texsurface.Type        = (Uint32)Type;
    ti->texsurface.nWidth      = (Uint32)w;
    ti->texsurface.nHeight     = (Uint32)h;
    ti->texsurface.pSurface    = (Uint32*)data;  /* raw TIM2 payload */
}

/* adv.c:590 njSetTextureName(np, ip, gIdx, attr). Real Ninja stashes `addr`
 * (an NJS_TEXINFO*) in the NJS_TEXNAME and marks it as "not yet uploaded"
 * by zeroing texaddr. njLoadTexture later walks the array and populates
 * texaddr from each filename-stashed NJS_TEXINFO. */
void njSetTextureName(NJS_TEXNAME* tn, void* addr,
                      Uint32 gIdx, Uint32 attr) {
    RX_LOG("nj", "njSetTextureName tn=%p addr=%p gIdx=%u attr=0x%08x",
           (void*)tn, addr, (unsigned)gIdx, (unsigned)attr);
    if (!tn) return;
    tn->filename = addr;   /* NJS_TEXINFO* masquerading as filename */
    tn->attr     = attr;
    tn->texaddr  = 0;      /* cleared — filled by njLoadTexture */
}

/* adv.c:847 njLoadTexture(&AdvTexList[0]). Walk every NJS_TEXNAME in the
 * list, pull the NJS_TEXINFO we stashed into filename, copy nWidth/nHeight
 * and the raw TIM2 blob ptr into a pool TEXMEMLIST, stamp that pool
 * address into .texaddr so SetQuadUv2Ex's cast resolves, then decode the
 * TIM2 to RGBA8 and upload it to the backend texture for this slot. */
Sint32 njLoadTexture(NJS_TEXLIST* tl) {
    if (!tl) { RX_LOG("nj", "njLoadTexture tl=NULL"); return -1; }
    RX_LOG("nj", "njLoadTexture tl=%p nbTex=%u", (void*)tl,
           (unsigned)tl->nbTexture);
    for (Uint32 i = 0; i < tl->nbTexture; ++i) {
        NJS_TEXNAME* tn = &tl->textures[i];
        NJS_TEXINFO* ti = (NJS_TEXINFO*)tn->filename;
        NJS_TEXMEMLIST* ml = pool_get();
        if (!ml) {
            RX_LOG("nj", "  slot %u: pool exhausted", (unsigned)i);
            continue;
        }
        ml->globalIndex = 0;
        ml->bank        = 0;
        ml->count       = 1;
        if (ti) {
            ml->texinfo.texsurface.nWidth   = ti->texsurface.nWidth;
            ml->texinfo.texsurface.nHeight  = ti->texsurface.nHeight;
            ml->texinfo.texsurface.Type     = ti->texsurface.Type;
            ml->texinfo.texaddr             = ti->texaddr;   /* raw TIM2 ptr */
            ml->texinfo.texsurface.pSurface = (Uint32*)ti->texaddr;
        } else {
            ml->texinfo.texsurface.nWidth  = 1024;
            ml->texinfo.texsurface.nHeight = 512;
        }
        uintptr_t addr = (uintptr_t)ml;
        if (addr > 0xFFFFFFFFULL) {
            RX_LOG("nj", "  slot %u: pool ptr %p EXCEEDS 4GiB — truncation",
                   (unsigned)i, (void*)ml);
        }
        tn->texaddr = (Uint32)addr;

        int slot = pool_slot_of(ml);
        int dw = 0, dh = 0;
        uint8_t* dpix = NULL;
        if (ti && ti->texaddr && tim2_decode(ti->texaddr, &dw, &dh, &dpix)) {
            recvx_gfx_tex_upload(slot, dpix, dw, dh);
            recvx_dump_rgba_tga(slot, dw, dh, dpix);
            RX_LOG("nj", "  slot %d: decoded %dx%d from %p uploaded",
                   slot, dw, dh, ti->texaddr);
        } else {
            /* Placeholder — solid color so unhandled TIM2 formats or
             * missing payloads still show a visible quad. Per-slot tint
             * so adjacent textures in the same list aren't confused. */
            static uint8_t ph[4*4];
            uint8_t tint_r = (uint8_t)(60 + (slot * 53) % 196);
            uint8_t tint_g = (uint8_t)(80 + (slot * 97) % 176);
            uint8_t tint_b = (uint8_t)(60 + (slot * 131)% 196);
            for (int p = 0; p < 4; ++p) {
                ph[p*4+0] = tint_r;
                ph[p*4+1] = tint_g;
                ph[p*4+2] = tint_b;
                ph[p*4+3] = 255;
            }
            recvx_gfx_tex_upload(slot, ph, 2, 2);
            RX_LOG("nj", "  slot %d: PLACEHOLDER tint=(%u,%u,%u)",
                   slot, tint_r, tint_g, tint_b);
        }
    }
    return (Sint32)tl->nbTexture;
}

/* Latched by njSetTexture so njSetQuadTexture can index the active list.
 * Ps2_current_texmemlist is pointed at the selected slot so adv.c's
 * ClutChange write at adv.c:695 targets real pool memory. */
static NJS_TEXLIST* g_active_tl;
static uint32_t     g_current_color;
static int          g_current_slot;
static int          g_current_trans;

Sint32 njSetTexture(NJS_TEXLIST* tl) { g_active_tl = tl; return 0; }
Sint32 njSetTextureNum(Uint32 n)     { (void)n;  return 0; }

/* ------------------------------------------------------------------ */
/* Draw primitives — forward to the backend gfx API                   */
/* ------------------------------------------------------------------ */

void njQuadTextureStart(int trans) {
    g_current_trans = trans;
}

void njQuadTextureEnd(void) {
    /* Nothing to tear down: recvx_gfx_draw_quad is self-contained. */
}

/* adv.c:700 njSetQuadTexture(TexNo, BaseColor). TexNo indexes the
 * currently-active texlist (set by njSetTexture above). */
void njSetQuadTexture(int tex_id, Uint32 base_color) {
    g_current_color = base_color;
    g_current_slot  = -1;
    if (!g_active_tl || (Uint32)tex_id >= g_active_tl->nbTexture) {
        Ps2_current_texmemlist = NULL;
        return;
    }
    NJS_TEXMEMLIST* ml =
        (NJS_TEXMEMLIST*)(uintptr_t)g_active_tl->textures[tex_id].texaddr;
    Ps2_current_texmemlist = ml;
    g_current_slot = pool_slot_of(ml);
}

/* adv.c:702 njDrawQuadTexture(qp, PosZ). QUAD has x1/y1/x2/y2 in PS2
 * screen-space and u1/v1/u2/v2 already normalized by SetQuadUv2Ex. */
void njDrawQuadTexture(QUAD* q, float z) {
    if (!q) return;
    recvx_gfx_draw_quad(g_current_slot,
                        q->x1, q->y1, q->x2, q->y2,
                        q->u1, q->v1, q->u2, q->v2,
                        z, g_current_color, g_current_trans);
}

/* adv.c:447/687 njDrawPolygon — vertex-colored fan (usually 4 verts). */
void njDrawPolygon(NJS_POLYGON_VTX* p, Sint32 count, Sint32 trans) {
    if (!p || count <= 0) return;
    recvx_gfx_vtx vb[32];
    if (count > 32) count = 32;
    for (Sint32 i = 0; i < count; ++i) {
        vb[i].x     = p[i].x;
        vb[i].y     = p[i].y;
        vb[i].z     = p[i].z;
        vb[i].color = p[i].col;
    }
    recvx_gfx_draw_polygon(vb, (int)count, (int)trans);
}

void njTextureFilterMode(Sint32 mode) {
    recvx_gfx_set_filter((int)mode);
}

/* ------------------------------------------------------------------ */
/* Pad[] bridge                                                       */
/* ------------------------------------------------------------------ */

/* On PS2 the Pad[] array is populated by Ps2_Read_Key (ps2_sg_pad.c) which
 * we don't compile. Without it Pad[0].press stays 0 and every menu gate
 * (CheckStartButton, AdvGetOkButton) misses. We simulate the same copy
 * path: read our ninja peripheral each frame, mirror .on/.press into
 * Pad[0]. Called from main_pc.c after recvx_input_new_frame().
 *
 * The ninja peripheral already carries scePad-shifted bits (see
 * input.c header), so this is just a struct copy — no remapping.
 */
#include "padman.h"  /* Pad[4] */
/* njGetPeripheral is already declared in KATANA/Include/ninjapad.h pulled
 * via ninja.h above. Its return type is `const NJS_PERIPHERAL*` which is
 * typedef'd from PDS_PERIPHERAL — same layout as our port-side mirror. */

void recvx_pump_pad(void) {
    const NJS_PERIPHERAL* p = njGetPeripheral(0);
    if (!p) {
        Pad[0].on = 0;
        Pad[0].press = 0;
        Pad[0].Rept = 0;
        Pad[0].l = 0;
        Pad[0].r = 0;
        Pad[0].x1 = Pad[0].y1 = 0;
        return;
    }
    Pad[0].on      = p->on;
    Pad[0].press   = p->press;
    Pad[0].Rept    = recvx_input_rept();
    Pad[0].l       = p->l;
    Pad[0].r       = p->r;
    Pad[0].x1      = p->x1;
    Pad[0].y1      = p->y1;
    Pad[0].x2      = p->x2;
    Pad[0].y2      = p->y2;
}
