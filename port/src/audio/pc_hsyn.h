/*
 * Software stand-in for the PS2's hardware synthesizer (Sony's modhsyn on
 * the SPU2): plays Sony HD/BD sound banks through MIDI-style messages.
 *
 * Ports 0-9 carry the sequence (music) banks, ports 10-15 the effect banks,
 * as in the game's sound driver. Every call must be made with the audio
 * lock held (pc_audio_lock); the audio thread mixes under it.
 *
 * Adapted from recvx-vita (platform/iop/hsyn.h, MIT licence,
 * Copyright (c) 2024-2026 AshfordFamily).
 */
#ifndef PC_HSYN_H
#define PC_HSYN_H

#include <stdint.h>

#define HSYN_PORTS 16
#define HSYN_RATE 48000

/* Loads a bank on a port: hd/bd are malloc'd and owned by the synth from
 * here on. NULL unloads. */
void hsyn_bank_set(int port, uint8_t *hd, uint32_t hdsize, uint8_t *bd, uint32_t bdsize);
/* The same, sharing the caller's copy (kept alive while loaded). */
void hsyn_bank_ref(int port, const uint8_t *hd, uint32_t hdsize, const uint8_t *bd, uint32_t bdsize);
const uint8_t *hsyn_bank_hd(int port);
uint32_t hsyn_bank_bd_size(int port);

void hsyn_program(int port, int ch, int prog);
void hsyn_note_on(int port, int ch, int note, int vel);
void hsyn_note_off(int port, int ch, int note);
void hsyn_sound_off(int port, int ch);           /* CC 120: silence at once */
void hsyn_volume(int port, int ch, int vol);      /* CC 7, 0-127 */
void hsyn_pan(int port, int ch, int pan);         /* CC 10, 0-127 */
void hsyn_bend(int port, int ch, int bend);       /* 14 bits, 0x2000 centre */
void hsyn_expression(int port, int ch, int expr); /* CC 11, 0-127 */
void hsyn_note_release_all(int port);
void hsyn_reset_channels(int port);
void hsyn_port_master(int port, int vol); /* 0-0x3fff */
void hsyn_port_volume(int port, int vol); /* 0-127 */
void hsyn_port_pan(int port, int pan);    /* 0-127, centre 64 */
void hsyn_port_bend(int port, int bend);  /* 14 bits, 0x2000 centre */
void hsyn_master_volume(int vol);         /* 0-0x3fff */

/* Set while a voice of this channel still sounds. */
int hsyn_channel_active(int port, int ch);

/* Adds n stereo frames at HSYN_RATE. */
void hsyn_mix(int32_t *out, int n);

/* HD helpers */
typedef struct {
    uint32_t bd_offset, bd_size;
    uint16_t rate;
    uint8_t loop, base_note;
} HsynSampleInfo;

/* Checks that a header looks like a Sony HD bank ("IECSsreV"). */
int hsyn_hd_valid(const uint8_t *hd, uint32_t size);
int hsyn_hd_max_program(const uint8_t *hd);
int hsyn_hd_program_sample(const uint8_t *hd, uint32_t bdsize, int prog, HsynSampleInfo *out);

#endif
