/*
 * The PC sound engine (see pc_sound.h).
 *
 * The audio device's thread calls mix() through pc_audio_mix_hook with the
 * audio lock held; the game thread takes the same lock to change voices.
 * Each voice is resampled to PC_AUDIO_RATE with 4-point Hermite
 * interpolation, and its gain changes are ramped over each mixed block so
 * that volume and pan moves do not click.
 *
 * Direction: spatialize() turns a voice's pan, or its position when it has
 * one, into the left and right gains. It is the single place to replace for
 * modern 3D sound (HRTF for headphones, or more output channels for 5.1/7.1):
 * every voice that knows where it is already reaches it with a position.
 */
#include "pc_sound.h"
#include "pc_host.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { KIND_FREE, KIND_STREAM, KIND_SOURCE };

typedef struct {
    int kind;
    int channels;
    int rate;

    /* Stream: ring of interleaved samples, positions in frames. */
    int16_t *ring;
    uint32_t cap, rd, wr;
    int ended;

    /* Source */
    PcSoundFetch fetch;
    void *user;

    int finished, paused;
    int tail; /* frames of silence still to shift in after the end */
    uint64_t played;

    /* Resampling: hist[1] and hist[2] surround the output position frac. */
    float hist[4][2];
    double frac;

    float gain, pan;
    int has_pos;
    float pos[3];
    float cur[2]; /* gains applied at the end of the last block */
    int started;
} Voice;

static Voice voices[PC_SOUND_VOICES];
static float master = 1.0f;
static int mono_out;

static void (*updaters[8])(void);
static int updater_count;
static void (*mixers[4])(float *lr, int frames);
static int mixer_count;

/* ---- Spatialization ---------------------------------------------------- */

static void spatialize(const Voice *v, float g[2])
{
    float pan = v->pan;
    float att = 1.0f;
    float a;

    if (v->has_pos) {
        float d = sqrtf(v->pos[0] * v->pos[0] + v->pos[2] * v->pos[2]);

        pan = d > 1e-6f ? v->pos[0] / d : 0.0f; /* sine of the azimuth */
        if (v->pos[2] < 0.0f)
            att = 0.85f; /* behind: a little quieter */
    }
    if (pan < -1.0f)
        pan = -1.0f;
    if (pan > 1.0f)
        pan = 1.0f;
    /* Constant power, full level on both sides at the centre. */
    a = (pan + 1.0f) * 0.78539816f;
    g[0] = cosf(a) * 1.41421356f;
    g[1] = sinf(a) * 1.41421356f;
    if (g[0] > 1.0f)
        g[0] = 1.0f;
    if (g[1] > 1.0f)
        g[1] = 1.0f;
    g[0] *= v->gain * att * master;
    g[1] *= v->gain * att * master;
}

/* ---- Mixing (audio thread, lock held) ---------------------------------- */

/* Next input frame of a voice: 1, 0 (none yet) or -1 (ended). */
static int next_frame(Voice *v, float out[2])
{
    int16_t lr[2];
    int r;

    if (v->kind == KIND_STREAM) {
        const int16_t *s;

        if (v->rd == v->wr)
            return v->ended ? -1 : 0;
        s = v->ring + (size_t)(v->rd % v->cap) * v->channels;
        out[0] = s[0];
        out[1] = v->channels > 1 ? s[1] : s[0];
        v->rd++;
        return 1;
    }
    r = v->fetch(v->user, lr);
    if (r > 0) {
        out[0] = lr[0];
        out[1] = lr[1];
    }
    return r;
}

static float hermite(float y0, float y1, float y2, float y3, float t)
{
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * t + c2) * t + c1) * t + y1;
}

static void mix_voice(Voice *v, float *lr, int frames)
{
    double step = (double)v->rate / PC_AUDIO_RATE;
    float target[2], g[2], dg[2];
    int i, c;

    spatialize(v, target);
    if (!v->started) {
        v->cur[0] = target[0];
        v->cur[1] = target[1];
        v->started = 1;
    }
    g[0] = v->cur[0];
    g[1] = v->cur[1];
    dg[0] = (target[0] - g[0]) / frames;
    dg[1] = (target[1] - g[1]) / frames;

    for (i = 0; i < frames; i++) {
        while (v->frac >= 1.0) {
            float in[2];
            int r = v->tail > 0 ? -1 : next_frame(v, in);

            if (r == 0)
                goto underrun;
            if (r < 0) {
                if (v->tail == 0)
                    v->tail = 3;
                if (--v->tail == 0) {
                    v->finished = 1;
                    v->cur[0] = target[0];
                    v->cur[1] = target[1];
                    return;
                }
                in[0] = in[1] = 0.0f;
            } else {
                v->played++;
            }
            memmove(v->hist[0], v->hist[1], sizeof(v->hist[0]) * 3);
            v->hist[3][0] = in[0];
            v->hist[3][1] = in[1];
            v->frac -= 1.0;
        }
        for (c = 0; c < 2; c++) {
            float s = hermite(v->hist[0][c], v->hist[1][c], v->hist[2][c], v->hist[3][c],
                              (float)v->frac);
            lr[i * 2 + c] += s * (g[c] + dg[c] * i);
        }
        v->frac += step;
    }
underrun:
    v->cur[0] = target[0];
    v->cur[1] = target[1];
}

static void mix(float *lr, int frames)
{
    int i;

    for (i = 0; i < PC_SOUND_VOICES; i++) {
        Voice *v = &voices[i];

        if (v->kind != KIND_FREE && !v->finished && !v->paused)
            mix_voice(v, lr, frames);
    }
    if (mixer_count > 0) {
        float own[512 * 2];
        int done = 0;

        while (done < frames) {
            int n = frames - done > 512 ? 512 : frames - done;

            memset(own, 0, (size_t)n * 2 * sizeof(float));
            for (i = 0; i < mixer_count; i++)
                mixers[i](own, n);
            for (i = 0; i < n * 2; i++)
                lr[done * 2 + i] += own[i] * master;
            done += n;
        }
    }
    if (mono_out) {
        for (i = 0; i < frames; i++)
            lr[i * 2] = lr[i * 2 + 1] = (lr[i * 2] + lr[i * 2 + 1]) * 0.5f;
    }
}

/* ---- Voices (game thread) ---------------------------------------------- */

static int open_voice(int kind, int channels, int rate)
{
    int i;

    pc_audio_mix_hook = mix;
    for (i = 0; i < PC_SOUND_VOICES; i++) {
        if (voices[i].kind == KIND_FREE) {
            Voice *v = &voices[i];

            memset(v, 0, sizeof(*v));
            v->channels = channels;
            v->rate = rate > 0 ? rate : PC_AUDIO_RATE;
            v->gain = 1.0f;
            v->frac = 3.0; /* shift in three frames before the first output */
            v->kind = kind; /* last: the voice is not mixed before this */
            return i;
        }
    }
    return -1;
}

int pc_sound_open_stream(int channels, int rate, int capacity)
{
    int16_t *ring = malloc((size_t)capacity * channels * sizeof(int16_t));
    int v;

    if (ring == NULL)
        return -1;
    pc_audio_lock();
    v = open_voice(KIND_STREAM, channels, rate);
    if (v >= 0) {
        voices[v].ring = ring;
        voices[v].cap = (uint32_t)capacity;
    }
    pc_audio_unlock();
    if (v < 0)
        free(ring);
    return v;
}

int pc_sound_open_source(PcSoundFetch fetch, void *user, int rate)
{
    int v;

    pc_audio_lock();
    v = open_voice(KIND_SOURCE, 2, rate);
    if (v >= 0) {
        voices[v].fetch = fetch;
        voices[v].user = user;
    }
    pc_audio_unlock();
    return v;
}

static Voice *get(int v)
{
    return v >= 0 && v < PC_SOUND_VOICES && voices[v].kind != KIND_FREE ? &voices[v] : NULL;
}

void pc_sound_close(int v)
{
    Voice *p;
    int16_t *ring = NULL;

    pc_audio_lock();
    p = get(v);
    if (p != NULL) {
        ring = p->ring;
        p->kind = KIND_FREE;
    }
    pc_audio_unlock();
    free(ring);
}

int pc_sound_space(int v)
{
    Voice *p;
    int n = 0;

    pc_audio_lock();
    p = get(v);
    if (p != NULL && p->kind == KIND_STREAM)
        n = (int)(p->cap - (p->wr - p->rd));
    pc_audio_unlock();
    return n;
}

void pc_sound_write(int v, const int16_t *samples, int frames)
{
    Voice *p;
    int i;

    pc_audio_lock();
    p = get(v);
    if (p != NULL && p->kind == KIND_STREAM) {
        for (i = 0; i < frames && p->wr - p->rd < p->cap; i++) {
            memcpy(p->ring + (size_t)(p->wr % p->cap) * p->channels, samples + (size_t)i * p->channels,
                   (size_t)p->channels * sizeof(int16_t));
            p->wr++;
        }
    }
    pc_audio_unlock();
}

#define SET(v, stmt)                  \
    do {                              \
        Voice *p;                     \
        pc_audio_lock();              \
        p = get(v);                   \
        if (p != NULL) {              \
            stmt;                     \
        }                             \
        pc_audio_unlock();            \
    } while (0)

void pc_sound_end(int v) { SET(v, p->ended = 1); }
void pc_sound_set_rate(int v, int rate) { SET(v, p->rate = rate > 0 ? rate : 1); }
void pc_sound_set_gain(int v, float gain) { SET(v, p->gain = gain); }
void pc_sound_set_pan(int v, float pan) { SET(v, p->pan = pan); }
void pc_sound_set_pause(int v, int pause) { SET(v, p->paused = pause); }

void pc_sound_set_position(int v, int on, float x, float y, float z)
{
    SET(v, {
        p->has_pos = on;
        p->pos[0] = x;
        p->pos[1] = y;
        p->pos[2] = z;
    });
}

int pc_sound_finished(int v)
{
    int r = 1;

    SET(v, r = p->finished);
    return r;
}

uint64_t pc_sound_played(int v)
{
    uint64_t n = 0;

    SET(v, n = p->played);
    return n;
}

void pc_sound_set_master(float gain)
{
    pc_audio_lock();
    master = gain;
    pc_audio_unlock();
}

void pc_sound_set_mono(int mono)
{
    pc_audio_lock();
    mono_out = mono;
    pc_audio_unlock();
}

void pc_sound_add_mixer(void (*fn)(float *lr, int frames))
{
    pc_audio_lock();
    if (mixer_count < 4)
        mixers[mixer_count++] = fn;
    pc_audio_mix_hook = mix;
    pc_audio_unlock();
}

void pc_sound_add_updater(void (*fn)(void))
{
    int i;

    for (i = 0; i < updater_count; i++)
        if (updaters[i] == fn)
            return;
    if (updater_count < 8)
        updaters[updater_count++] = fn;
}

void pc_sound_frame(void)
{
    static int64_t last_ns;
    int64_t now = pc_host_time_ns();
    int i;

    for (i = 0; i < updater_count; i++)
        updaters[i]();

    /* Without an audio device nothing pulls the voices: play them into the
     * void at the real rate, so that the game sees them progress and end. */
    if (!pc_audio_open && pc_audio_mix_hook != NULL) {
        int frames = last_ns == 0 ? 0 : (int)((now - last_ns) * PC_AUDIO_RATE / 1000000000);
        float scratch[512 * 2];

        if (frames > PC_AUDIO_RATE / 4)
            frames = PC_AUDIO_RATE / 4;
        while (frames > 0) {
            int n = frames > 512 ? 512 : frames;

            memset(scratch, 0, sizeof(scratch));
            pc_audio_lock();
            mix(scratch, n);
            pc_audio_unlock();
            frames -= n;
        }
    }
    last_ns = now;
}
