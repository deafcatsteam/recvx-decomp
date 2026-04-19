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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* recvx_port.h is on the port-stubs include path but NOT on recvx_game's
 * include path (game target is PRIVATE). Forward-declare the log entry
 * point so we can still use RX_LOG-style output. */
extern void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Low-4GiB TEXMEMLIST pool                                           */
/* ------------------------------------------------------------------ */

#define TEX_POOL_SLOTS 64

static NJS_TEXMEMLIST* g_tex_pool;
static int             g_tex_pool_used;

/* Reserve a single low-4GiB page on first use. 64 * sizeof(NJS_TEXMEMLIST)
 * is well under 4 KiB so one page covers the whole pool. */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <memoryapi.h>
static NJS_TEXMEMLIST* tex_pool_alloc_low4g(size_t bytes) {
    MEM_ADDRESS_REQUIREMENTS req = {0};
    req.LowestStartingAddress = (PVOID)(uintptr_t)0x10000;
    req.HighestEndingAddress  = (PVOID)(uintptr_t)0xFFFFFFFEULL;
    req.Alignment             = 0;
    MEM_EXTENDED_PARAMETER param = {0};
    param.Type    = MemExtendedParameterAddressRequirements;
    param.Pointer = &req;
    void* p = VirtualAlloc2(GetCurrentProcess(), NULL, bytes,
                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                            &param, 1);
    if (!p) {
        RX_LOG("tex", "VirtualAlloc2 low-4g failed (err=%lu), falling back",
               GetLastError());
        p = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    return (NJS_TEXMEMLIST*)p;
}
#else
static NJS_TEXMEMLIST* tex_pool_alloc_low4g(size_t bytes) {
    (void)bytes;
    return NULL;  /* non-Windows not supported in this port yet */
}
#endif

static void ensure_pool(void) {
    if (g_tex_pool) return;
    size_t bytes = TEX_POOL_SLOTS * sizeof(NJS_TEXMEMLIST);
    g_tex_pool = tex_pool_alloc_low4g(bytes);
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
 * into a pool TEXMEMLIST, and stamp that pool address into .texaddr so
 * SetQuadUv2Ex's cast resolves to valid memory. */
void njLoadTexture(NJS_TEXLIST* tl) {
    if (!tl) { RX_LOG("nj", "njLoadTexture tl=NULL"); return; }
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
            ml->texinfo.texsurface.nWidth  = ti->texsurface.nWidth;
            ml->texinfo.texsurface.nHeight = ti->texsurface.nHeight;
            ml->texinfo.texsurface.Type    = ti->texsurface.Type;
            ml->texinfo.texaddr            = ti->texaddr;  /* raw TIM2 ptr */
        } else {
            /* Fall back to a safe non-zero size so division in
             * SetQuadUv2Ex doesn't produce inf/nan. */
            ml->texinfo.texsurface.nWidth  = 1024;
            ml->texinfo.texsurface.nHeight = 512;
        }
        uintptr_t addr = (uintptr_t)ml;
        if (addr > 0xFFFFFFFFULL) {
            RX_LOG("nj", "  slot %u: pool ptr %p EXCEEDS 4GiB — truncation",
                   (unsigned)i, (void*)ml);
        }
        tn->texaddr = (Uint32)addr;
        RX_LOG("nj", "  slot %u: ml=%p w=%u h=%u -> texaddr=0x%08x",
               (unsigned)i, (void*)ml,
               (unsigned)ml->texinfo.texsurface.nWidth,
               (unsigned)ml->texinfo.texsurface.nHeight,
               (unsigned)tn->texaddr);
    }
}

/* Remaining nj* texture helpers — adv.c calls these from the draw path
 * but we don't actually render yet. Keep them quiet no-ops (no log spam
 * once per frame). */
void njSetTexture(NJS_TEXLIST* tl)   { (void)tl; }
void njSetTextureNum(Sint32 n)       { (void)n; }
