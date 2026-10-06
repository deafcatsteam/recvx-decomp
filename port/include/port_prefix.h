/*
 * port_prefix.h - force-included before every translation unit of the PC port.
 *
 * The original game is built with Metrowerks CodeWarrior for the PS2 EE, where
 * `long` is 8 bytes and 128-bit integers exist. The PC port is built as a
 * 32-bit program (pointers stay 4 bytes so the game's data structures and the
 * pointers stored inside its data files keep their layout), so the wide types
 * are remapped here before any SDK or game header is seen.
 */
#ifndef PORT_PREFIX_H
#define PORT_PREFIX_H

#define PLATFORM_PC 1

/*
 * CodeWarrior's prefix file made the C library visible everywhere; several
 * game files call memcpy/atan2f/... without including anything. Without a
 * prototype, a float-returning function would be read back as an int.
 */
/* glibc's <sys/types.h> defines a 4-byte u_long; the EE's is 8 bytes. */
#define u_long port_libc_u_long
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#undef u_long

#ifdef _WIN32
/* index() is POSIX; the game only names it in a debug message. */
static inline char *port_index(const char *s, int c) { return strchr(s, c); }
#define index port_index
#endif

/*
 * `long` is 8 bytes for CodeWarrior on the EE but 4 bytes on 32-bit x86. The
 * game relies on the wide one (GS packets are built from 64-bit longs, struct
 * layouts follow it), and its 32-bit types already use `int`. So `long` is
 * widened for everything compiled after this point. System headers must all
 * be included above; port code spells 64-bit integers as int64_t/uint64_t.
 */
#define long long long

/* 128-bit "quadword": only ever used to move 16 bytes at once. */
typedef struct port_u128 {
    unsigned int w[4];
} __attribute__((aligned(16))) port_u128;

/* `__int128` is not available on 32-bit x86. */
#define __int128 port_u128

/* CRI's cri_xpts.h uses mode(TI) and `long` for 64-bit; predefine them. */
#define _TYPEDEF_Uint64
typedef uint64_t Uint64;
#define _TYPEDEF_Sint64
typedef int64_t Sint64;
#define _TYPEDEF_Uint128
typedef port_u128 Uint128;
#define _TYPEDEF_Sint128
typedef port_u128 Sint128;

/* PC platform hooks called from game code under #ifdef PLATFORM_PC. */
void pc_wait_vblank(void);

/* 16:9 (pc_widescreen.c): the 3D is drawn narrower by pc_wide_x (1 in 4:3),
   set for each frame by pc_widescreen_frame through pc_set_wide
   (ps2_NaView.c); pc_wide_text narrows the text the same way, around the
   middle of the screen, from n x/y pairs. */
extern float pc_wide_x;
void pc_widescreen_frame(void);
void pc_set_wide(float k);
void pc_wide_text(float *xy, int n);

/* 3D sound (pc_sound3d.c): pc_snd_at gives the world position (x, y, z) of
   the sound the game is about to start or move this frame; pc_snd_take
   hands it over, seen from the camera, and forgets it. pc_sd_take_dir
   (ps2_sg_sd.c) gives it to a sound handle at a request, and the handle's
   requests carry it to the sound driver (pc_snddrv_dir). */
void pc_snd_at(const float *pos);
int pc_snd_take(float dir[3]);
void pc_sd_take_dir(void *handle, int midi, int mode);
void pc_snddrv_dir(int midi, int port, int ch, const float *dir);

/* 60 images a second (pc_interp.c): pc_interp_begin starts a frame (after
   the buffers are swapped) and pc_interp_frame ends it (before the wait for
   the V-blanks); pc_interp_model tells which model the 3D strips drawn next
   belong to (mod: its shadow), to find them again in the next frame. */
void pc_interp_begin(void);
void pc_interp_frame(void);
void pc_interp_model(const void *model, int mod);
void pc_interp_vertex(const unsigned int *words);

/* CodeWarrior / EE-gcc specific keywords. */
#define __inline__ inline

#endif /* PORT_PREFIX_H */
