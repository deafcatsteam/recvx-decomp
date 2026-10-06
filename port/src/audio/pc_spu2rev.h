/*
 * The SPU2's reverb, in software: the game sets one per SPU2 core (mode and
 * depth, SdrSetRev), from each room's settings, and the effect banks say
 * which sounds are sent to it.
 *
 * Every call must be made with the audio lock held (pc_audio_lock).
 */
#ifndef PC_SPU2REV_H
#define PC_SPU2REV_H

#include <stdint.h>

#define SPU2REV_CORES 2

/* mode: 0 off, 5 hall (the only one the game uses); depth 0-0x7fff. */
void spu2rev_set(int core, int mode, int depth);

/* Adds the reverb of in (n stereo frames at 48 kHz, what the voices send
 * to the core's effect) to out. */
void spu2rev_mix(int core, const int32_t *in, int32_t *out, int n);

#endif
