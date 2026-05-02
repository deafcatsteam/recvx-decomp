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
 * History: started with shims for DrawPoly2D + SpriteH + SpriteUV +
 * SpriteOnOff + PulseInit. The first four got real upstream
 * implementations (commits 73fdef6a, 9ea09ce8, 047c3dbc, 88add94a) so
 * their shims were retired. PulseInit remains the only stub.
 */

#include "ninja.h"
#include "sub1.h"   /* S_WORK */

#include <stdint.h>

extern void recvx_log(const char* tag, const char* fmt, ...);

/* PulseInit populates parts_22b's `pulse_work` panel-position state for
 * the inventory's "selected item highlight" pulse animation. Until
 * upstream decompiles the body, no-op suppresses the per-frame
 * "PulseInit - UNIMPLEMENTED!" log spam without affecting visuals (the
 * pulse highlight just won't animate). */
void recvx_PulseInit(void) { }
