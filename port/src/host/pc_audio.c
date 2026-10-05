/*
 * Sound output: a queue of 16-bit stereo samples at PC_AUDIO_RATE, filled by
 * the game side (movies for now) and emptied by the window's audio device
 * (SDL callback, another thread). One writer and one reader, so the queue
 * only needs atomic positions.
 */
#include "pc_host.h"

#include <string.h>

#define QUEUE_FRAMES 131072 /* 2.7 seconds, a power of two for the wrapping */

static int16_t queue[QUEUE_FRAMES][2];
static volatile uint32_t write_pos, read_pos; /* in frames, wrapping */
static volatile int clear_requested;           /* done by the reader */

volatile int pc_audio_open;

/* Resampling state: position between the last input frame and the next. */
static int16_t last[2];
static uint32_t frac; /* 16.16 */

static void put(int16_t l, int16_t r)
{
    uint32_t w = __atomic_load_n(&write_pos, __ATOMIC_RELAXED);
    uint32_t rd = __atomic_load_n(&read_pos, __ATOMIC_ACQUIRE);

    if (w - rd >= QUEUE_FRAMES)
        return; /* full: drop */
    queue[w % QUEUE_FRAMES][0] = l;
    queue[w % QUEUE_FRAMES][1] = r;
    __atomic_store_n(&write_pos, w + 1, __ATOMIC_RELEASE);
}

void pc_audio_push(const int16_t *lr, int frames, int rate)
{
    uint32_t step;
    int i;

    if (!pc_audio_open || frames <= 0 || rate <= 0)
        return;

    if (rate == PC_AUDIO_RATE) {
        for (i = 0; i < frames; i++)
            put(lr[i * 2], lr[i * 2 + 1]);
        return;
    }

    /* Linear interpolation between consecutive input frames. */
    step = (uint32_t)(((uint64_t)rate << 16) / PC_AUDIO_RATE);
    for (i = 0; i < frames; i++) {
        while (frac < 0x10000) {
            int32_t t = frac;
            put((int16_t)(last[0] + (((lr[i * 2] - last[0]) * t) >> 16)),
                (int16_t)(last[1] + (((lr[i * 2 + 1] - last[1]) * t) >> 16)));
            frac += step;
        }
        frac -= 0x10000;
        last[0] = lr[i * 2];
        last[1] = lr[i * 2 + 1];
    }
}

void pc_audio_pull(int16_t *out, int frames)
{
    uint32_t rd = __atomic_load_n(&read_pos, __ATOMIC_RELAXED);
    uint32_t w = __atomic_load_n(&write_pos, __ATOMIC_ACQUIRE);
    int i;

    if (__atomic_exchange_n(&clear_requested, 0, __ATOMIC_ACQ_REL))
        rd = w;

    for (i = 0; i < frames; i++) {
        if (rd == w) {
            memset(out + i * 2, 0, (size_t)(frames - i) * 4);
            break;
        }
        out[i * 2] = queue[rd % QUEUE_FRAMES][0];
        out[i * 2 + 1] = queue[rd % QUEUE_FRAMES][1];
        rd++;
    }
    __atomic_store_n(&read_pos, rd, __ATOMIC_RELEASE);
}

int pc_audio_queued(void)
{
    return (int)(__atomic_load_n(&write_pos, __ATOMIC_ACQUIRE) -
                 __atomic_load_n(&read_pos, __ATOMIC_ACQUIRE));
}

void pc_audio_clear(void)
{
    __atomic_store_n(&clear_requested, 1, __ATOMIC_RELEASE);
    frac = 0;
    last[0] = last[1] = 0;
}
