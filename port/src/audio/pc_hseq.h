/*
 * Sony SQ sequence player (Sony's modmidi stand-in) driving the synth's
 * ports 0-9. Every call must be made with the audio lock held.
 *
 * Adapted from recvx-vita (platform/iop/hseq.h, MIT licence,
 * Copyright (c) 2024-2026 AshfordFamily).
 */
#ifndef PC_HSEQ_H
#define PC_HSEQ_H

#include <stdint.h>

#define HSEQ_PORTS 10

/* Plays MIDI block `block` of an SQ file (Vers/Sequ/Midi chunks); keeps a
 * copy. Returns 0 when the block is not there. */
int hseq_start(int port, const uint8_t *sq, uint32_t size, int block);
void hseq_stop(int port); /* releases the sounding notes */
void hseq_pause(int port, int paused);
int hseq_playing(int port); /* until the end of a block that does not loop */
void hseq_tick(void);       /* 240 Hz */

#endif
