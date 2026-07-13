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

/* Per-frame projection-center offset latched by njSetScreen (stub_game.c).
 * Applied to every 2D draw rect so logical positions land relative to the
 * intended cx/cy instead of PS2's default 320,240. Inventory uses
 * (235, 224) -> offset (-85, -16); most other modes keep cx=320, cy=240
 * which gives offset (0, 0) and passes through unchanged. */
extern void recvx_get_screen_offset(float* dx, float* dy);

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

/* PS2 globals declared as `extern` in headers we use, but defined for
 * real only in ps2_dummy.c / ps2_NaTextureFunction.c which aren't in
 * RECVX_GAME_SOURCES yet. sub1.c reads/writes Ps2_current_texbreak
 * after each OT pass; itemview.c picks branches off ViewType;
 * ps2_texture.c reads Ps2_tex_info as the head of the texmem pool list. */
unsigned int     Ps2_current_texbreak;
int              ViewType;
NJS_TEXMEMLIST*  Ps2_tex_info;

/* Stubs for ps2_NaTextureFunction.c helpers that ps2_texture.c calls
 * from bhCopyTexmem2Mainmem / bhCopyMainmem2Texmem (paths we don't run
 * since we don't actually push/pop texmem on PC — the GL backend keeps
 * everything resident). bhSetMemPvpTexture, the only ps2_texture.c
 * function that runs in our flow, doesn't reach these. */
void Ps2MemCopy4(void* dst, void* src, int lNum) {
    if (!dst || !src || lNum <= 0) return;
    memcpy(dst, src, (size_t)lNum * 4);
}
int  Ps2TextureMalloc(NJS_TEXMEMLIST* p) { (void)p; return 0; }
int  SearchNumber(unsigned int gidx, unsigned int bank) {
    (void)gidx; (void)bank; return -1;
}
int  SearchNullNumber(void) { return 0; }

/* ------------------------------------------------------------------ */
/* Low-4GiB TEXMEMLIST pool                                           */
/* ------------------------------------------------------------------ */

/* Must stay in sync with RX_GFX_TEX_SLOTS in backend_gl.c. 64 was enough
 * for menus/FMV, but a real room load (bhSetRoom + player/enemy models)
 * pushes well past it — the pool is never recycled per room yet. */
#define TEX_POOL_SLOTS 512

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

/* Real impl (moved from the stub_ninja.c no-op): must actually set
 * Ps2_tex_info, or bhCopyMainmem2Texmem (ps2_texture.c:493, called on
 * every movie-to-room texture handoff) dereferences a NULL pool head
 * and segfaults the first time a movie plays over a room with textures.
 *
 * Deliberately does NOT point Ps2_tex_info at the caller's `addr` (main.c
 * passes the plain static `tbuf`, a normal high-address global): ps2_texture.c
 * truncates NJS_TEXMEMLIST* to a Uint32 texaddr and back (same x64 gotcha
 * documented above the g_tex_pool block), so the backing buffer must live in
 * the low 4 GiB like g_tex_pool does. We give Ps2_tex_info its own
 * low4g allocation rather than sharing g_tex_pool, since SearchNullNumber()
 * is stubbed to always return slot 0 — sharing would let bhCopyMainmem2Texmem
 * stomp a slot njLoadTexture has already bound for real rendering. */
void njInitTexture(NJS_TEXMEMLIST* addr, Uint32 n) {
    static NJS_TEXMEMLIST* low4g_buf;
    (void)addr;
    if (!low4g_buf) {
        low4g_buf = (NJS_TEXMEMLIST*)recvx_alloc_low4g((size_t)n * sizeof(NJS_TEXMEMLIST));
        if (low4g_buf) memset(low4g_buf, 0, (size_t)n * sizeof(NJS_TEXMEMLIST));
    }
    Ps2_tex_info = low4g_buf;
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

/* CSM1 pixel-index swizzle. Applies to both 8-bit (256-entry) and 4-bit
 * (16-entry-in-32-entry-block) paletted textures.
 *
 * Sony PS2 TIM2 spec (ClutType bit 7):
 *   bit 7 = 0  → CSM1  (swizzled / GS-native VRAM layout)
 *   bit 7 = 1  → CSM2  (linear / device-independent layout)
 *
 * PS2 GS stores CLUTs in a PSMCT32-block pattern regardless of logical
 * entry count: 8 entries per half-block with a bit-3↔4 address remap
 * between logical and physical offsets. The same swap works for both
 * 8-bit (256 entries spanning 16 half-blocks) and 4-bit (16 entries
 * spanning 2 half-blocks, padded to 32 physical slots) because it's
 * really a block-addressing swizzle, not a palette-size quirk.
 *
 *   logical idx bits: i7 i6 i5 i4 i3 i2 i1 i0
 *   physical offset:  i7 i6 i5 i3 i4 i2 i1 i0   ← bits 3 and 4 swap
 *
 * For 4-bit (nibble 0..15), only bit 3 matters: logical 0..7 stay put,
 * logical 8..15 map to physical 16..23 (the upper half-block). Any TIM2
 * 4-bit CLUT padded to 128 B (32 entries × 4 B) is in this layout. */
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
    /* Bit 7 of ClutType is the CSM indicator:
     *   0 → CSM1 (swizzled, PS2 GS native layout, needs bits-3-4 index swap)
     *   1 → CSM2 (linear, already in logical order, no swap)
     * Earlier revisions had this inverted, which collapsed 8-bit paletted
     * textures to outlines-only (the ADX/Capcom splashes and menu bg fills
     * all read wrong palette entries for roughly half the index range). */
    int csm1_swizzled = (clut_type & 0x80) == 0;
    int clut_fmt      = clut_type & 0x07;     /* 1=A1BGR5 2=XBGR24 3=ABGR32 */

    RX_LOG("tim2", "decode %dx%d imgT=%d clutT=0x%02x (%s) hdr=%u img=%u clut=%u",
           w, h, image_type, (unsigned)ph->ClutType,
           csm1_swizzled ? "CSM1" : "CSM2",
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
            if (csm1_swizzled) idx = idx_csm1_swap(idx);
            const uint8_t* pal = clut + idx * 4;
            dst[i*4+0] = pal[0];
            dst[i*4+1] = pal[1];
            dst[i*4+2] = pal[2];
            dst[i*4+3] = alpha_ps2_to_pc(pal[3]);
        }
    } else if (image_type == 4) {
        /* 4-bit paletted. Two pixels per byte, PS2 packs low nibble first
         * (pixel 0 = low nibble, pixel 1 = high nibble).
         *
         * CLUT layout: CSM1 16-entry CLUTs are stored in a 32-entry
         * (128-byte @ ABGR32) PSMCT32 block, with the 16 "real" entries
         * placed at physical positions 0..7 and 16..23. Positions 8..15
         * and 24..31 are don't-care padding (zero-filled on this bank).
         * The same bit-3↔4 swap we use for 8-bit applies: logical nibble
         * N >= 8 reads from physical entry N+8. Skip the swap for CSM2
         * (linear) or for CLUTs sized 64 B (16 entries, densely packed).
         *
         * Concrete failure this catches: the ADX CRI logo (ADV.AFS[0],
         * 2nd TIM2 @ +0x82540). Outline pixels use nibbles 0..7 and
         * render correctly regardless, but the solid white fills use
         * nibbles 8..15 — without the swap those read zero-padding
         * entries and render as transparent black, giving an empty
         * outlined logo instead of a solid-white one. */
        if (clut_fmt != 3) {
            RX_LOG("tim2", "unsupported clut_fmt=%d for 4bpp (want ABGR32)",
                   clut_fmt);
            return 0;
        }
        int swizzle_4bpp = csm1_swizzled && (ph->ClutSize >= 128);
        for (int i = 0; i < w * h; ++i) {
            uint8_t byte   = img[i >> 1];
            uint8_t nibble = (i & 1) ? (uint8_t)(byte >> 4)
                                     : (uint8_t)(byte & 0x0F);
            if (swizzle_4bpp) nibble = idx_csm1_swap(nibble);
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
static Uint32       g_current_texnum;

Sint32 njSetTexture(NJS_TEXLIST* tl) { g_active_tl = tl; return 0; }
Sint32 njSetTextureNum(Uint32 n)     { g_current_texnum = n; return 0; }

/* Resolve a CNK material's texture id (NJD_CT_TID, ninja_cnk.c) against
 * the texlist njSetTexture last latched (bhAllDrawModel calls
 * njSetTexture(rom->mdl.texP) right before drawing the room). Returns
 * a gfx pool slot, or -1 if unresolvable (draws white/untextured). */
int recvx_cnk_resolve_texture_slot(unsigned int tex_id) {
    if (!g_active_tl || tex_id >= g_active_tl->nbTexture) return -1;
    NJS_TEXMEMLIST* ml = (NJS_TEXMEMLIST*)(uintptr_t)g_active_tl->textures[tex_id].texaddr;
    return pool_slot_of(ml);
}

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
    /* Trace: log non-BG textured quads, deduped on (slot, UV rect) so a
     * quad animated only via color (e.g. DisplayPressStartPlate's FadeRate
     * pulse) logs once instead of 900 times. Mode 4 logo strips have fixed
     * UVs too so each only logs once — leaves room for Mode 6/8/9 plates. */
    if (g_current_slot != 0) {
        /* Dedup key: (slot, UV rect, alpha >> 4). 16 alpha buckets per UV so
         * a FadeRate pulse logs ~16 times instead of 900 (visible animation)
         * or 1 (stuck at one alpha). Lets us distinguish "never animates"
         * from "we just dedupe too aggressively". */
        struct rec { int slot; float u1,v1,u2,v2; uint8_t abkt; };
        static struct rec seen[128];
        static int nseen = 0;
        float u1=q->u1, v1=q->v1, u2=q->u2, v2=q->v2;
        uint8_t abkt = (uint8_t)(((uint32_t)g_current_color >> 28) & 0xF);
        int hit = 0;
        for (int i = 0; i < nseen; ++i) {
            if (seen[i].slot == g_current_slot &&
                seen[i].u1 == u1 && seen[i].v1 == v1 &&
                seen[i].u2 == u2 && seen[i].v2 == v2 &&
                seen[i].abkt == abkt) {
                hit = 1; break;
            }
        }
        if (!hit && nseen < 128) {
            seen[nseen].slot = g_current_slot;
            seen[nseen].u1 = u1; seen[nseen].v1 = v1;
            seen[nseen].u2 = u2; seen[nseen].v2 = v2;
            seen[nseen].abkt = abkt;
            nseen++;
            RX_LOG("draw", "NEW slot=%d screen=(%.0f,%.0f-%.0f,%.0f) "
                   "uv=(%.3f,%.3f-%.3f,%.3f) z=%.3f col=0x%08x trans=%d",
                   g_current_slot, q->x1, q->y1, q->x2, q->y2,
                   u1, v1, u2, v2,
                   z, (unsigned)g_current_color, g_current_trans);
        }
    }
    float dx, dy; recvx_get_screen_offset(&dx, &dy);
    recvx_gfx_draw_quad(g_current_slot,
                        q->x1 + dx, q->y1 + dy, q->x2 + dx, q->y2 + dy,
                        q->u1, q->v1, q->u2, q->v2,
                        z, g_current_color, g_current_trans);
}

/* sub1.c:DrawPoly2D and effsub*.c bind a NJS_POINT2COL (positions + colors
 * + optional UVs) and ask Ninja to draw it as a 2D polygon. PS2 path
 * pushes a GIF packet; we lower it to either:
 *   - recvx_gfx_draw_polygon for the untextured case (atr & 0x80000000 == 0)
 *   - recvx_gfx_draw_quad for the textured n=4 case (binds the texture
 *     latched by the most recent njSetTexture+njSetTextureNum)
 * Other (textured non-quad) call sites currently no-op with a warning.
 * `attr` semantics:
 *   bit 31 (0x80000000) - "use UVs from p2c->tex" (textured)
 *   bit 5  (0x20)       - alpha blend on
 *   bit 6  (0x40)       - blend math variant (we treat any 0x60 as "blend")
 */
void njDrawPolygon2D(NJS_POINT2COL* p2c, Sint32 n, Float pri, Uint32 attr) {
    if (!p2c || !p2c->p || !p2c->col || n < 3) return;

    int trans = (attr & 0x60) ? 1 : 0;

    float dx, dy; recvx_get_screen_offset(&dx, &dy);

    if (!(attr & 0x80000000)) {
        /* Untextured. Per-vertex color, single z.
         *
         * recvx_gfx_draw_polygon's backend submits as GL_TRIANGLE_STRIP
         * (matches the GS PRIM=4 convention used by njDrawPolygon /
         * AdvDrawFadePolygon, which feed verts in zigzag TL,BL,TR,BR
         * order). njDrawPolygon2D callers (sub1.c MultiWindowBack,
         * effect.c bhDrawPARAM2D, effsub1.c) instead emit ROTATIONAL
         * order (CW or CCW around the quad). For n=4, swapping indices
         * 2 and 3 converts either rotational direction into zigzag, so
         * the strip primitive paints the full quad instead of a bow-tie.
         * n=3 (effsub1b triangles) needs no reorder. */
        if (n > 64) return;
        static const int reorder4[4] = { 0, 1, 3, 2 };
        recvx_gfx_vtx v[64];
        for (int i = 0; i < n; ++i) {
            int src = (n == 4) ? reorder4[i] : i;
            v[i].x = p2c->p[src].x + dx;
            v[i].y = p2c->p[src].y + dy;
            v[i].z = pri;
            v[i].color = p2c->col[src].color;
        }
        static int logged_poly = 0;
        if (!logged_poly) {
            RX_LOG("poly", "njDrawPolygon2D first untextured n=%d "
                   "p[0]=(%.0f,%.0f) p[2]=(%.0f,%.0f) col=0x%08x z=%.2f",
                   n, p2c->p[0].x, p2c->p[0].y, p2c->p[2].x, p2c->p[2].y,
                   p2c->col[0].color, pri);
            logged_poly = 1;
        }
        recvx_gfx_draw_polygon(v, n, trans);
        return;
    }

    /* Textured path: only quads handled for now (every sub1.c call passes
     * n==4). NJS_POINT2COL.tex stores UVs as int16 in NJS_TEX{u,v} — PS2
     * GS uses 4096 = 1.0 UV scale, but the inventory's TIM2 uploads land
     * in our pool as RGBA with full-texture coverage so we normalize off
     * a per-quad min/max. */
    if (n != 4 || !p2c->tex) {
        static int warned = 0;
        if (!warned) {
            RX_LOG("nj", "njDrawPolygon2D: textured non-quad (n=%d) not impl", n);
            warned = 1;
        }
        return;
    }

    /* Resolve current texture slot + actual pixel dimensions via the same
     * chain njSetQuadTexture uses. nWidth/nHeight come from the
     * NJS_TEXMEMLIST so we don't have to guess the atlas size — KazariAnim
     * for example draws a 32x2 strip out of inventory texture #2 which is
     * 512x512, not 256, and a hardcoded /256 scale was tiling the strip
     * across the whole panel. */
    int slot = -1;
    int tex_w = 0, tex_h = 0;
    if (g_active_tl && g_current_texnum < g_active_tl->nbTexture) {
        NJS_TEXMEMLIST* ml =
            (NJS_TEXMEMLIST*)(uintptr_t)g_active_tl->textures[g_current_texnum].texaddr;
        if (ml) {
            slot  = pool_slot_of(ml);
            tex_w = (int)ml->texinfo.texsurface.nWidth;
            tex_h = (int)ml->texinfo.texsurface.nHeight;
        }
    }
    if (slot < 0 || tex_w <= 0 || tex_h <= 0) return;

    /* Compute axis-aligned screen + UV rect from the 4 verts. NJS_COLOR.tex
     * gives us int16 pixel coordinates that we normalize against the
     * texture's actual size. dx/dy already computed above for the
     * untextured branch. */
    float x1 = p2c->p[0].x + dx, y1 = p2c->p[0].y + dy;
    float x2 = x1, y2 = y1;
    int   u1 = p2c->tex[0].tex.u, v1 = p2c->tex[0].tex.v;
    int   u2 = u1, v2 = v1;
    for (int i = 1; i < 4; ++i) {
        float px = p2c->p[i].x + dx, py = p2c->p[i].y + dy;
        if (px < x1) x1 = px; else if (px > x2) x2 = px;
        if (py < y1) y1 = py; else if (py > y2) y2 = py;
        if (p2c->tex[i].tex.u < u1) u1 = p2c->tex[i].tex.u; else if (p2c->tex[i].tex.u > u2) u2 = p2c->tex[i].tex.u;
        if (p2c->tex[i].tex.v < v1) v1 = p2c->tex[i].tex.v; else if (p2c->tex[i].tex.v > v2) v2 = p2c->tex[i].tex.v;
    }
    float fu1 = (float)u1 / (float)tex_w, fv1 = (float)v1 / (float)tex_h;
    float fu2 = (float)u2 / (float)tex_w, fv2 = (float)v2 / (float)tex_h;
    uint32_t color = p2c->col[0].color;

    /* Dedup: log first call per (slot, UV-bucket) so we can see which
     * texture pages the font path is sampling vs the inventory atlas
     * path. Useful for diagnosing why glyphs render as garbage. */
    {
        struct rec { int slot; int u1, v1; };
        static struct rec seen[64];
        static int nseen = 0;
        int hit = 0;
        for (int i = 0; i < nseen; ++i) {
            if (seen[i].slot == slot && seen[i].u1 == u1 && seen[i].v1 == v1) {
                hit = 1; break;
            }
        }
        if (!hit && nseen < 64) {
            seen[nseen].slot = slot;
            seen[nseen].u1 = u1;
            seen[nseen].v1 = v1;
            nseen++;
            RX_LOG("tex2d", "njDrawPolygon2D textured slot=%d tex=%dx%d "
                   "uv_px=(%d,%d-%d,%d) -> uv=(%.4f,%.4f-%.4f,%.4f) "
                   "screen=(%.0f,%.0f-%.0f,%.0f) attr=0x%x",
                   slot, tex_w, tex_h, u1, v1, u2, v2,
                   fu1, fv1, fu2, fv2, x1, y1, x2, y2, attr);
        }
    }

    recvx_gfx_draw_quad(slot, x1, y1, x2, y2, fu1, fv1, fu2, fv2, pri, color, trans);
}

/* njDrawPolygon2DM — same primitive with model-view matrix transform.
 * sub1.c doesn't use it, but bup_00.c / effsub1b.c do. Forward to the
 * untransformed variant so non-inventory call sites at least don't
 * unresolved-link. The lack of matrix is incorrect for those, but they
 * aren't in our render path until 3D effects are wired. */
void njDrawPolygon2DM(NJS_POINT2COL* p2c, Sint32 n, Float pri, Uint32 attr) {
    njDrawPolygon2D(p2c, n, pri, attr);
}

/* sub1.c calls njDrawSprite2D ~50 times per frame to render every part
 * of the inventory UI: tabs, cursor, item icons, count digits, equipment
 * plates. PS2 path goes through ps2_NaSprite.c which builds a draw
 * packet for the GS GIF; we instead resolve the sprite's chosen tanim
 * frame to a screen-space + UV rect and forward to recvx_gfx_draw_quad,
 * piggybacking on the same z-sorted 2D queue adv.c uses for menu plates.
 *
 *   sp->tlist       — texture list (NJS_TEXLIST*)
 *   sp->tanim[n]    — animation frame: sx/sy size + cx/cy center
 *                     + u1/v1/u2/v2 UV pixels + texid (index into tlist)
 *   sp->p           — screen-space anchor
 *   sp->sx / sy     — extra scale (sub1 always uses 1.0)
 *   sp->ang         — rotation 0..0xFFFF mapping 0..360° (sub1 mostly 0)
 *   pri             — render priority (used as z)
 *   attr            — bit 1 = transparent, bit 5 = drawing flag (we
 *                     treat both transparent + non-transparent as
 *                     blend-enabled for now, since menu parts use a
 *                     mix of opaque tabs + alpha-blended icons)
 */
/* True if `p` is NULL, the PS2 bin-relocator -1 sentinel, or any other
 * "looks like garbage" address. On Windows x64 user-mode any pointer with
 * the top 17 bits set is necessarily kernel/sentinel. Filters out -1
 * (0xFFFFFFFFFFFFFFFF), x86-32 sign-extended -1 (0xFFFFFFFF80000000+),
 * and accidental zero-extensions like 0x00000000FFFFFFFF that some bin
 * realizers leave when a 32-bit -1 is widened. */
static int ptr_looks_invalid(const void* p) {
    if (p == NULL) return 1;
    uintptr_t u = (uintptr_t)p;
    if (u == ~(uintptr_t)0) return 1;            /* exact -1 */
    if (u == 0xFFFFFFFFu) return 1;              /* 32-bit -1 zero-ext */
    if ((u >> 47) != 0 && (u >> 47) != ((uintptr_t)1 << 17) - 1) {
        /* High 17 bits not all 0 (user) or all 1 (canonical kernel) — i.e.
         * some non-canonical garbage. Best to skip. */
        return 1;
    }
    return 0;
}

void njDrawSprite2D(NJS_SPRITE* sp, Sint32 n, Float pri, Uint32 attr) {
    if (ptr_looks_invalid(sp))         return;
    if (ptr_looks_invalid(sp->tlist))  return;
    if (ptr_looks_invalid(sp->tanim))  return;

    NJS_TEXANIM* ta = &sp->tanim[n];
    NJS_TEXLIST* tl = sp->tlist;
    if (ptr_looks_invalid(ta))         return;
    if (ptr_looks_invalid(tl))         return;
    if (n < 0 || n > 4096)             return;  /* tanim animation index sanity */

    /* nbTexture / textures pointer sanity — tl might be a stack/static
     * struct that's been zero-initialized but not loaded, so nbTexture==0
     * and textures==NULL is a normal "skip" rather than a bug. */
    if (tl->nbTexture == 0 || ptr_looks_invalid(tl->textures)) return;
    if (ta->texid < 0 || (Uint32)ta->texid >= tl->nbTexture) return;

    /* Resolve the tex id to our pool slot the same way njSetQuadTexture
     * does. The slot is what the backend uses to bind a real GL texture. */
    NJS_TEXMEMLIST* ml =
        (NJS_TEXMEMLIST*)(uintptr_t)tl->textures[ta->texid].texaddr;
    if (ptr_looks_invalid(ml))         return;
    int slot = pool_slot_of(ml);
    if (slot < 0) return;

    int tex_w = (int)ml->texinfo.texsurface.nWidth;
    int tex_h = (int)ml->texinfo.texsurface.nHeight;
    if (tex_w <= 0 || tex_h <= 0) return;

    /* Screen rect: anchor at sp->p with center offset (cx,cy), scaled
     * by sp->sx / sp->sy, sized by the tanim's sx/sy. Plus the latched
     * njSetScreen offset so inventory sprites (cx=235, cy=224) shift
     * left/up by (-85, -16) and land in their intended viewport. */
    float sx = sp->sx, sy = sp->sy;
    float dx, dy; recvx_get_screen_offset(&dx, &dy);
    /* One-shot log so we can verify the offset is actually applied (not
     * just the njSetScreen call site reached). Triggers on first non-zero
     * offset seen, which means the inventory hook ran. */
    static int logged_offset = 0;
    if (!logged_offset && (dx != 0.0f || dy != 0.0f)) {
        RX_LOG("spr", "screen offset live: dx=%.1f dy=%.1f (first sprite this frame)", dx, dy);
        logged_offset = 1;
    }
    float x1 = sp->p.x - (float)ta->cx * sx + dx;
    float y1 = sp->p.y - (float)ta->cy * sy + dy;
    float x2 = x1 + (float)ta->sx * sx;
    float y2 = y1 + (float)ta->sy * sy;

    /* UV normalization assumes the source atlas was 256x256 (PS2 game's
     * internal "logical" texture base). Our extracted TIM2 atlases are
     * 512x512 (or 1024x512), and PARTS NJS_TEXANIM data has UV pixel
     * coords in the 0..256 range. Normalizing by tex_w would only
     * sample the upper-left 1/(tex_w/256) of each texture, leaving the
     * rest of the atlas content unreferenced -- which is why inventory
     * chrome rendered as "fragments of multiple characters" with the
     * wooden background sliver missing. Same root cause as the font
     * 14->28 cell fix in message.c, applied uniformly to all sprite UV
     * coords. Fall back to actual tex_w if it's <=256 (no scaling
     * needed for true-256 atlases). */
    int uv_base_w = tex_w > 256 ? 256 : tex_w;
    int uv_base_h = tex_h > 256 ? 256 : tex_h;
    float u1 = (float)ta->u1 / (float)uv_base_w;
    float v1 = (float)ta->v1 / (float)uv_base_h;
    float u2 = (float)ta->u2 / (float)uv_base_w;
    float v2 = (float)ta->v2 / (float)uv_base_h;

    /* attr bit layout in sub1.c's SpriteSet2D:
     *   bit 0=0x01 hidden     bit 1=0x02 inv x  bit 2=0x04 transparent
     *   bit 3=0x08 inv y      bit 4=0x10 ?      bit 5=0x20 drawing
     * Treat 0x04 as "alpha blended" and skip the rest for now. */
    int trans = (attr & 0x04) ? 1 : 0;

    /* Per-sprite tint from the most recent njSetConstantMaterial call.
     * sub1.c:3217 packs spr_t.col into a NJS_ARGB and sets it before
     * each SpriteSet2D, giving items in the inventory grid distinct
     * tints (selected vs. unselected vs. damaged). Defaults to
     * 0xFFFFFFFF (opaque white = no tint) on cold start. */
    extern unsigned int recvx_get_constant_material_argb(void);
    uint32_t tint = recvx_get_constant_material_argb();

    /* Dedup: log first time we see each (slot, anim-id) pair so we can
     * see what's drawing post-hook without flooding the log. */
    {
        struct rec { int slot; int n; };
        static struct rec seen[64];
        static int nseen = 0;
        int hit = 0;
        for (int i = 0; i < nseen; ++i) {
            if (seen[i].slot == slot && seen[i].n == n) { hit = 1; break; }
        }
        if (!hit && nseen < 64) {
            seen[nseen].slot = slot;
            seen[nseen].n    = n;
            nseen++;
            RX_LOG("spr", "njDrawSprite2D slot=%d n=%d screen=(%.0f,%.0f-%.0f,%.0f) "
                   "uv=(%.3f,%.3f-%.3f,%.3f) z=%.3f attr=0x%x",
                   slot, n, x1, y1, x2, y2, u1, v1, u2, v2, pri, attr);
        }
    }

    recvx_gfx_draw_quad(slot, x1, y1, x2, y2, u1, v1, u2, v2,
                        pri, tint, trans);
}

/* adv.c:447/687 njDrawPolygon — vertex-colored TRIANGLE_STRIP (usually 4
 * verts in TL-BL-TR-BR zigzag order). The PS2 GS PRIM register encodes
 * prim=4 (TRIANGLESTRIP) in ps2_NaDraw.c; the backend renders accordingly. */
void njDrawPolygon(NJS_POLYGON_VTX* p, Sint32 count, Sint32 trans) {
    if (!p || count <= 0) return;
    recvx_gfx_vtx vb[32];
    if (count > 32) count = 32;
    float dx, dy; recvx_get_screen_offset(&dx, &dy);
    for (Sint32 i = 0; i < count; ++i) {
        vb[i].x     = p[i].x + dx;
        vb[i].y     = p[i].y + dy;
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
        {
            extern SYS_WORK* sys;
            sys->pad_on = sys->pad_oncpy = sys->pad_ps = sys->pad_old = 0;
        }
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

    /* Mirror sys->pad_* fields the same way pad.c:bhSetPad would.
     * sub1.c CursorMove + most menu nav code reads sys->pad_ps (newly
     * pressed bits this frame), not Pad[0]. Without this update,
     * pad_ps stays 0 and inputs never reach the inventory cursor.
     * Edge-detection from pad.c:275-278: ps = curr AND-NOT prev_on. */
    extern SYS_WORK* sys;
    unsigned int pad = p->on;
    sys->pad_old   = sys->pad_on;
    sys->pad_ps    = pad & ~sys->pad_oncpy;
    sys->pad_rs    = pad ^ (sys->pad_oncpy | sys->pad_ps);
    sys->pad_on    = sys->pad_oncpy = pad;
    sys->pad_al    = p->l;
    sys->pad_ar    = p->r;
    sys->pad_ax    = -p->x1;
    sys->pad_ay    = -p->y1;

    /* Context remap: when the inventory is dispatching, arrow keys feel
     * unintuitive because RECVX wires the d-pad to Cancel (sub1.c:4133
     * checks pad_ps & 0x5000 = UP|DOWN -> StatusCancel) while real
     * navigation goes through L1/R1 (tab nav) and L2/R2 (item-grid
     * nav). Strip the d-pad bits and OR in shoulder-button bits so
     * arrow keys drive nav directly. Title-menu and gameplay paths
     * outside the inventory keep the original d-pad mapping.
     *
     * Bit mapping (scePad-shifted layout from input.c):
     *   UP    (0x1000) -> L2   (0x0001) [item grid up]
     *   DOWN  (0x4000) -> R2   (0x0002) [item grid down]
     *   LEFT  (0x8000) -> L1   (0x0004) [tab left]
     *   RIGHT (0x2000) -> R1   (0x0008) [tab right] */
    extern S_WORK swork;
    if (swork.subscreenmode != 0) {
        unsigned int dpad = Pad[0].on    & 0xF000;
        unsigned int dpsp = Pad[0].press & 0xF000;
        unsigned int dpre = Pad[0].Rept  & 0xF000;
        unsigned int shoulder_on = 0, shoulder_ps = 0, shoulder_re = 0;
        if (dpad & 0x1000) shoulder_on |= 0x1;
        if (dpad & 0x4000) shoulder_on |= 0x2;
        if (dpad & 0x8000) shoulder_on |= 0x4;
        if (dpad & 0x2000) shoulder_on |= 0x8;
        if (dpsp & 0x1000) shoulder_ps |= 0x1;
        if (dpsp & 0x4000) shoulder_ps |= 0x2;
        if (dpsp & 0x8000) shoulder_ps |= 0x4;
        if (dpsp & 0x2000) shoulder_ps |= 0x8;
        if (dpre & 0x1000) shoulder_re |= 0x1;
        if (dpre & 0x4000) shoulder_re |= 0x2;
        if (dpre & 0x8000) shoulder_re |= 0x4;
        if (dpre & 0x2000) shoulder_re |= 0x8;
        /* Strip d-pad, OR in shoulder remap. Mirror to sys->pad_* too. */
        Pad[0].on    = (Pad[0].on    & ~0xF000u) | shoulder_on;
        Pad[0].press = (Pad[0].press & ~0xF000u) | shoulder_ps;
        Pad[0].Rept  = (Pad[0].Rept  & ~0xF000u) | shoulder_re;
        sys->pad_on    = sys->pad_oncpy = Pad[0].on;
        sys->pad_ps    = Pad[0].press;
    }
}

/* Port-only inventory toggle: press Triangle (S key) during gameplay to
 * open the inventory. Real PS2 game has this wired up via bhSysCallGame
 * button checks that we haven't traced. Mirrors the effect of the
 * normal "open inventory" transition: clears ts_flg bit 9 (lets
 * Itemselect dispatch) AND sets subscreenmode bit 0x40 (triggers
 * ItemTaskCheck's full-init branch, which calls CallSystemSe, sets the
 * screen viewport, and eventually StatusInit). Only fires when Itemselect
 * was suspended — pressing it while inventory is already open is ignored.
 *
 * Called from main_pc.c per-frame, after recvx_pump_pad. */
extern S_WORK swork;

/* bhSetFontTexture now real (effect.c, Task 4.2) — uses the actual
 * ef_info[] table instead of this file's prior hand-rolled hardcoded-4
 * substitute. */

/* Load ITEM1.AFS file 145 — the inventory texture pack — into sys->subtxp
 * and run SbsTextureInit so swork.subtx_list gets populated. bup_00.c does
 * this in the real game (line 292: sys->subtxp = sys->memp; then
 * SbsTextureInit fires from a later state) but bup_00 isn't compiled.
 * One-shot: only does the load if sys->subtxp is still NULL. */
extern void SbsTextureInit(void);
extern int  RequestReadInsideFile(unsigned int, unsigned int, void*);
extern int  GetInsideFileSize(unsigned int, unsigned int);
extern void* bhGetFreeMemory(unsigned int, int);
extern void  recvx_log(const char* tag, const char* fmt, ...);

static void port_load_inventory_textures(void) {
    extern SYS_WORK* sys;
    if (sys->subtxp) return;  /* already loaded */
    int sz = GetInsideFileSize(sys->itm_partid, 145);
    if (sz <= 0) {
        recvx_log("game",
            "port inv-tex: ITEM1.AFS/145 missing (sz=%d, partid=%d)",
            sz, sys->itm_partid);
        return;
    }
    void* buf = bhGetFreeMemory((unsigned)sz, 32);
    if (!buf) {
        recvx_log("game", "port inv-tex: bhGetFreeMemory(%d) FAILED", sz);
        return;
    }
    if (RequestReadInsideFile(sys->itm_partid, 145, buf) != 0) {
        recvx_log("game", "port inv-tex: AFS read failed for partid=%d id=145",
                  sys->itm_partid);
        return;
    }
    sys->subtxp = (unsigned char*)buf;
    SbsTextureInit();
    recvx_log("game",
        "port inv-tex: loaded ITEM1.AFS/145 (%d B) + SbsTextureInit done", sz);
}

void recvx_port_check_inventory_toggle(void) {
    extern SYS_WORK* sys;
    /* Press, not held — edge-triggered. */
    if (!(Pad[0].press & 0x10)) return;

    /* Always re-arm on press. Earlier check `if (!(ts_flg & 0x200)) return`
     * meant Triangle worked only once: after the in-game close path, our
     * compiled subset doesn't always restore ts_flg | 0x200 (the suspend
     * code at sub1.c:3145 lives inside a `statusflg & 0x80000 && taskloop ==
     * 4 && mn_md0 == 0` branch that requires player state we don't run).
     * Unconditionally clearing ts_flg and arming subscreenmode lets a
     * second press re-trigger ItemTaskCheck's init even when Itemselect
     * is technically still dispatching with no inventory visible. */
    /* Idempotent: gate on subscreenmode == 0 (inventory fully closed).
     * Each re-fire of ItemTaskCheck's init branch re-arms SpriteH's
     * slide-in (statusflg |= 0x2000) without resetting cen_pos to the
     * start position, so re-pressing while open animates an extra 8
     * frames past the target — that's the 2x displacement that puts
     * the scrollbar at game-X=240 instead of 456. Skip if already
     * armed/open/animating. */
    if (swork.subscreenmode != 0) {
        recvx_log("game",
            "port inventory toggle: ignored (subscreenmode=0x%x already active)",
            (unsigned)swork.subscreenmode);
        return;
    }
    port_load_inventory_textures();
    sys->ts_flg &= ~0x200u;
    swork.subscreenmode |= 0x40;
    recvx_log("game",
        "port inventory toggle: opening (ts_flg bit 0x200 cleared, "
        "subscreenmode 0x40 armed)");
}

/* Per-frame diagnostic dump for inventory state. Logs cen_pos[5] every
 * 30 frames while Itemselect is dispatching, so we can see whether the
 * slide-in animation overshoots or some other code modifies the
 * position after settling. Also logs Pad[0].press and swork.maincsr /
 * subscreenmode transitions so we can trace why down-nav from the top
 * tabs into the item grid doesn't work. Throttled to readable volume. */
extern float cen_pos[12][6];

void recvx_port_diag_inventory(void) {
    extern SYS_WORK* sys;
    static int last_subscreenmode = -1;
    static unsigned int last_press = 0;
    static int last_maincsr = -1;
    static int frame_ctr = 0;
    static int last_logged_cen_frame = -1000;

    /* Port-side close cleanup: when SpriteH finishes the outbound slide
     * animation it sets statusflg & 0x80000 ("outbound complete") but
     * the PS2 cleanup that would reset subscreenmode and re-suspend
     * Itemselect lives at sub1.c:3113 inside a `statusflg & 0x80000 &&
     * taskloop == 4 && mn_md0 == 0` branch we don't satisfy. So
     * subscreenmode sticks at 0x2 forever and our toggle's idempotent
     * gate refuses re-opens. Detect the stuck state and finish the
     * cleanup ourselves so the next Triangle press reopens cleanly. */
    if ((swork.statusflg & 0x80000) && swork.subscreenmode != 0) {
        recvx_log("inv",
            "port close cleanup: statusflg & 0x80000 set, subscreenmode=0x%x "
            "-> resetting to 0, suspending Itemselect",
            (unsigned)swork.subscreenmode);
        swork.subscreenmode = 0;
        swork.statusflg = 0;
        swork.flgtest = 0;
        sys->ts_flg |= 0x200u;
        /* Reset some other state that StatusInit will re-arm on next open. */
        swork.mode = 0;
        swork.testmode = 0;
        last_subscreenmode = 0;
        frame_ctr++;
        return;
    }

    if (sys->ts_flg & 0x200) {
        if (last_subscreenmode != 0 && last_subscreenmode != -1) {
            recvx_log("inv",
                "inventory closed (ts_flg & 0x200 set, was subscreenmode=0x%x)",
                last_subscreenmode);
        }
        last_subscreenmode = 0;
        frame_ctr++;
        return;
    }

    if (frame_ctr - last_logged_cen_frame > 30) {
        recvx_log("inv",
            "cen_pos[5]=(%.0f,%.0f -> %.0f,%.0f vel=%.0f,%.0f) "
            "subscreenmode=0x%x statusflg=0x%x",
            cen_pos[5][0], cen_pos[5][1], cen_pos[5][2], cen_pos[5][3],
            cen_pos[5][4], cen_pos[5][5],
            (unsigned)swork.subscreenmode, (unsigned)swork.statusflg);
        last_logged_cen_frame = frame_ctr;
    }

    /* Force Monitor task mode bytes to 0 ONLY when stuck in the specific
     * 0x0B04 (mn_md0=4 mn_md1=0xB) pattern. Original hook cleared
     * mn_md0/mn_stack[0] unconditionally every frame — that fixed the
     * item-action submenu gate at sub1.c:1978 but ALSO nuked legitimate
     * transient states like mn_md0=6 mn_md1=1 (sub-binary load header
     * parse, the EXAMINE flow). After case 0 triggered the AFS read and
     * advanced mn_md1=1, the next frame the hook would zero it before
     * case 1 could parse the header — leaving sys->sb_ppp NULL forever.
     *
     * Narrowed: only clear when md == 0x0B04 (the stuck pattern). Other
     * legitimate states (6 sub-bin load, 1 init, etc.) are left alone so
     * their state machines can advance normally. */
    {
        unsigned int md_packed = *(unsigned int*)&sys->mn_md0;
        unsigned int stk0      = (unsigned int)sys->mn_stack[0];
        const unsigned int STUCK_PATTERN = 0x00000B04u;

        if (md_packed == STUCK_PATTERN) {
            *(unsigned int*)&sys->mn_md0 = 0;
        }
        if (stk0 == STUCK_PATTERN) {
            sys->mn_stack[0] = 0;
        }
    }

    if ((int)swork.subscreenmode != last_subscreenmode) {
        recvx_log("inv",
            "subscreenmode transition: 0x%x -> 0x%x (flgtest=%d statusflg=0x%x)",
            last_subscreenmode, (unsigned)swork.subscreenmode,
            (int)swork.flgtest, (unsigned)swork.statusflg);
        last_subscreenmode = (int)swork.subscreenmode;
    }

    if (Pad[0].press && Pad[0].press != last_press) {
        recvx_log("inv",
            "Pad[0].press=0x%x sys->pad_ps=0x%x maincsr=%d listcsr_0=%d "
            "subscreenmode=0x%x",
            Pad[0].press, sys->pad_ps,
            (int)swork.maincsr, (int)swork.listcsr_0,
            (unsigned)swork.subscreenmode);
        last_press = Pad[0].press;
    }
    if (!Pad[0].press) last_press = 0;

    if ((int)swork.maincsr != last_maincsr) {
        recvx_log("inv",
            "maincsr changed: %d -> %d (mode=%d testmode=%d)",
            last_maincsr, (int)swork.maincsr,
            (int)swork.mode, (int)swork.testmode);
        last_maincsr = (int)swork.maincsr;
    }

    /* Item-action submenu gate diagnostic. When user is in item-grid
     * cursor mode (mode=1, maincsr=3) and presses Start, sub1.c:1976
     * gates the transition to ItemCommand (mode=4) on three things:
     *   - !(statusflg & 0x20)
     *   - mn_md0 == 0
     *   - itemid != 0  ← most likely culprit (cursor on empty slot)
     * Log all three when Start is pressed in this state so we can see
     * exactly which gate fails. */
    if ((Pad[0].press & 0x800) && swork.mode == 1 && swork.maincsr == 3) {
        unsigned int mn_md0 = *(unsigned int*)&sys->mn_md0;
        recvx_log("inv",
            "ITEM-action gate check: itemid=%u statusflg&0x20=%u mn_md0=%u "
            "listcsr_0=%u  -> %s",
            (unsigned)swork.itemid,
            (unsigned)(swork.statusflg & 0x20),
            mn_md0, (unsigned)swork.listcsr_0,
            (!(swork.statusflg & 0x20) && mn_md0 == 0 && swork.itemid != 0)
                ? "PASS (should enter ItemCommand)" : "BLOCKED");
    }

    /* Per-frame itemid trace while in item-grid cursor — so we can
     * tell at a glance whether the slot the user lands on actually
     * has an item. Throttled by listcsr_0 change. */
    static int last_listcsr = -1;
    if (swork.mode == 1 && swork.maincsr == 3 &&
        (int)swork.listcsr_0 != last_listcsr) {
        recvx_log("inv",
            "item cursor moved: listcsr_0=%u itemid=%u",
            (unsigned)swork.listcsr_0, (unsigned)swork.itemid);
        last_listcsr = (int)swork.listcsr_0;
    }
    if (swork.mode != 1) last_listcsr = -1;

    frame_ctr++;
}

/* ------------------------------------------------------------------ */
/* Typewriter task skip — jump straight from NEW GAME to Movie task.  */
/* ------------------------------------------------------------------ */

/* SYS_WORK lives in main.c; we need mvi_no/mvi_md/tk_flg visible through
 * the types.h struct so member access survives the x64 pointer-growth
 * that shifts 32-bit offsets past `void* typ_exp @ 0x50`. */
extern SYS_WORK* sys;

/* ControlTypewriter now real (bup_00.c). */
