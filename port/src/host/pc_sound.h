/*
 * The PC sound engine: voices mixed on the audio thread (see pc_sound.c).
 *
 * A voice plays either a stream (16-bit samples written by the game thread,
 * like the ADX music and speech) or a source (a function the mixer calls for
 * each sample, like the sound driver's ADPCM voices). Each voice has a gain,
 * a stereo pan and, optionally, a position relative to the listener; the
 * position is what a 3D renderer (HRTF headphones, 5.1/7.1) will use.
 *
 * All the functions below are called from the game thread.
 */
#ifndef PC_SOUND_H
#define PC_SOUND_H

#include <stdint.h>

#define PC_SOUND_VOICES 64

/* Produces the next sample frame of a source (left, right) at the voice's
 * rate. Returns 1, 0 when no data is ready yet (silence, retried later), or
 * -1 when the sound has ended. Runs on the audio thread with the audio lock
 * held, so it may read what the game thread changes under that lock. */
typedef int (*PcSoundFetch)(void *user, int16_t lr[2]);

/* Opens a stream voice holding up to capacity frames; returns -1 if none is
 * free. It starts playing as soon as samples are written. */
int pc_sound_open_stream(int channels, int rate, int capacity);

/* Opens a source voice; returns -1 if none is free. */
int pc_sound_open_source(PcSoundFetch fetch, void *user, int rate);

/* Stops a voice at once and frees it. */
void pc_sound_close(int v);

/* Streams: frames that can be written, writing, and marking the end (the
 * voice then finishes once everything written has been played). */
int pc_sound_space(int v);
void pc_sound_write(int v, const int16_t *samples, int frames);
void pc_sound_end(int v);

/* Set once the voice has played everything (a stream after its end, a
 * source after it returned -1). */
int pc_sound_finished(int v);

/* Input frames played so far (at the voice's own rate). */
uint64_t pc_sound_played(int v);

void pc_sound_set_rate(int v, int rate);
void pc_sound_set_gain(int v, float gain); /* linear, 1 = unchanged */
void pc_sound_set_pan(int v, float pan);   /* -1 left .. 0 centre .. 1 right */
void pc_sound_set_pause(int v, int pause);

/* Position relative to the listener (x right, y up, z forward), in any
 * unit; on = 0 goes back to the plain pan. While a position is set, it
 * decides the direction instead of the pan. */
void pc_sound_set_position(int v, int on, float x, float y, float z);

/* Volume of everything (linear). */
void pc_sound_set_master(float gain);

/* Mono output (the game's sound option): both sides get the average. */
void pc_sound_set_mono(int mono);

/* Called once per V-blank by the game thread: runs the updaters (stream
 * refills) and, when no audio device is open, plays the voices silently so
 * that they still advance in time. */
void pc_sound_frame(void);

/* Adds a mixer: a function that adds its own frames at PC_AUDIO_RATE into
 * lr (16-bit sample units), on the audio thread with the lock held. */
void pc_sound_add_mixer(void (*fn)(float *lr, int frames));

/* Adds a function for pc_sound_frame() to call. */
void pc_sound_add_updater(void (*fn)(void));

#endif
