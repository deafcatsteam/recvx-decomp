/*
 * sub1_compat.c -- port-side overrides for sub1.c functions whose decomp
 * bodies are still scePrintf("...UNIMPLEMENTED!\n") stubs upstream.
 *
 * Pattern (matches game_texture_stubs.c, ps2_texture.c #ifdef hooks):
 *   sub1.c declares the function with the upstream-correct signature and
 *   placeholder body. We add a tiny `#ifdef RECVX_PC_PORT` block at the
 *   top of each that forwards to a `recvx_*` symbol defined here. When
 *   upstream eventually decompiles the real body, we delete the shim.
 *
 * Why this file is in recvx_game (not recvx_port_stubs):
 *   We need NJS_POINT2 / NJS_COLOR / S_WORK from KATANA + game headers,
 *   which are PRIVATE to recvx_game. Same reasoning as game_texture_stubs.c.
 */

#include "ninja.h"
#include "sub1.h"   /* S_WORK */

#include <stdint.h>

extern void recvx_log(const char* tag, const char* fmt, ...);

typedef struct recvx_gfx_vtx {
    float    x, y, z;
    uint32_t color;
} recvx_gfx_vtx;
extern void recvx_gfx_draw_polygon(const recvx_gfx_vtx* v, int n, int trans);

/* ----- DrawPoly2D ---------------------------------------------------------
 * Untextured 4-vertex 2D polygon with per-vertex color. The single live call
 * site at sub1.c:7428 is MultiWindowBack, which draws the inventory
 * background as a 352x184 dark-blue (0xFF000030 BGRA) rectangle.
 *
 * Decomp call: DrawPoly2D(p, col, uv, -31.0f, 0x60, 0)
 *   p[4]   - screen-space positions (NJS_POINT2 = {float x,y})
 *   col[4] - per-vertex BGRA (NJS_COLOR.color = uint32 BGRA byte order)
 *   uv[4]  - UV pairs as int16 in NJS_COLOR.tex; UNINITIALIZED when tex=0
 *   pri    - z priority (negative = closer on PS2; passed to z-sort queue)
 *   atr    - PS2 GS attribute flags (0x60 = alpha-blend on; treat all as
 *            blended for now since UI overlays universally need it)
 *   texnum - texture index in current texlist; 0 = untextured (no UV)
 *
 * For texnum != 0 we'd bind a texture from the inventory texlist and route
 * to recvx_gfx_draw_quad — currently unreached, will add when a textured
 * call site appears.
 */
void recvx_DrawPoly2D(NJS_POINT2* pos, NJS_COLOR* col, NJS_COLOR* uv,
                      float pri, unsigned int atr, int texnum)
{
    (void)uv; (void)atr;
    if (!pos || !col) return;

    if (texnum != 0) {
        static int warned = 0;
        if (!warned) {
            recvx_log("game", "recvx_DrawPoly2D: textured path (texnum=%d) not impl yet", texnum);
            warned = 1;
        }
        return;
    }

    recvx_gfx_vtx v[4];
    for (int i = 0; i < 4; ++i) {
        v[i].x = pos[i].x;
        v[i].y = pos[i].y;
        v[i].z = pri;
        v[i].color = col[i].color;
    }

    /* One-time diagnostic so we can confirm the shim chain is actually
     * being hit AND see what geometry sub1.c is asking for. Remove once
     * the inventory chrome renders correctly. */
    static int logged = 0;
    if (!logged) {
        recvx_log("game",
                  "recvx_DrawPoly2D first call: p=[(%.1f,%.1f)..(%.1f,%.1f)] "
                  "col=0x%08x pri=%.1f atr=0x%x",
                  pos[0].x, pos[0].y, pos[2].x, pos[2].y,
                  col[0].color, pri, atr);
        logged = 1;
    }

    recvx_gfx_draw_polygon(v, 4, 1);
}

/* ----- SpriteH / SpriteUV / SpriteOnOff / PulseInit -----------------------
 * State-update helpers for the sprite/parts arrays. They don't draw
 * directly; they populate S_WORK fields that other code reads. Until those
 * consumers exist, no-ops are correct AND silence the per-frame
 * "UNIMPLEMENTED!" log noise. */
void recvx_SpriteH(S_WORK* st)     { (void)st; }
void recvx_SpriteUV(S_WORK* st)    { (void)st; }
void recvx_SpriteOnOff(S_WORK* st) { (void)st; }
void recvx_PulseInit(void)         { }
