/*
 * Plays the ADX streams of the test ISO's /SOUND.AFS (sine waves made by
 * make_test_adx.py, no game data) through the PC ADXT player and the sound
 * engine, pulling the output like the audio device does, and checks the
 * pitch, level, looping, end, play time, volume and stop.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cri_adxf.h"
#include "cri_adxt.h"

#include "../src/host/pc_host.h"
#include "../src/host/pc_sound.h"
#include "../src/platform/pc_disc.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define FRAME 800 /* one 60 Hz frame at 48 kHz */

static short out[PC_AUDIO_RATE * 2 * 2]; /* up to 2 seconds */

static int at; /* sample frames written to out */

/* Plays game frames, appending to out; returns the sample frames in out. */
static int run(int frames)
{
    int i;

    for (i = 0; i < frames; i++) {
        pc_sound_frame();
        pc_audio_pull(out + at * 2, FRAME);
        at += FRAME;
    }
    return at;
}

/* Frequency of one channel from its rising zero crossings. */
static double freq(int from, int to, int ch)
{
    int i, n = 0;

    for (i = from + 1; i < to; i++)
        if (out[(i - 1) * 2 + ch] < 0 && out[i * 2 + ch] >= 0)
            n++;
    return n * (double)PC_AUDIO_RATE / (to - from);
}

static int peak(int from, int to, int ch)
{
    int i, p = 0;

    for (i = from; i < to; i++)
        if (abs(out[i * 2 + ch]) > p)
            p = abs(out[i * 2 + ch]);
    return p;
}

/* Largest step between neighbouring samples: a click shows up as a jump. */
static int max_step(int from, int to, int ch)
{
    int i, m = 0;

    for (i = from + 1; i < to; i++)
        if (abs(out[i * 2 + ch] - out[(i - 1) * 2 + ch]) > m)
            m = abs(out[i * 2 + ch] - out[(i - 1) * 2 + ch]);
    return m;
}

int main(void)
{
    ADXT t;
    Sint32 count, scale;
    int n, frames;

    if (!pc_disc_open() || ADXF_LoadPartitionNw(0, "\\SOUND.AFS", NULL, NULL) != 0) {
        printf("test_sound: cannot open the test ISO (CVX_ISO)\n");
        return 1;
    }
    pc_audio_open = 1; /* the test pulls the output itself */
    ADXT_Init();
    t = ADXT_Create(2, NULL, 0);
    CHECK(t != NULL && t->used);

    /* Mono 1000 Hz at 22.05 kHz, 0.3 s, no loop. */
    ADXT_SetOutVol(t, 0);
    ADXT_StartAfs(t, 0, 1);
    CHECK(ADXT_GetStat(t) == ADXT_STAT_PLAYING);
    for (frames = 0; frames < 60 && ADXT_GetStat(t) == ADXT_STAT_PLAYING; frames++)
        n = run(1);
    printf("mono: %d game frames, %.1f Hz, peak %d/%d\n", frames, freq(2000, 12000, 0),
           peak(2000, 12000, 0), peak(2000, 12000, 1));
    CHECK(ADXT_GetStat(t) == ADXT_STAT_PLAYEND);
    CHECK(frames >= 18 && frames <= 21); /* 0.3 s = 18 frames, plus the end */
    CHECK(fabs(freq(2000, 12000, 0) - 1000) < 15);
    CHECK(peak(2000, 12000, 0) > 9300 && peak(2000, 12000, 0) < 10700);
    CHECK(peak(2000, 12000, 1) == peak(2000, 12000, 0)); /* mono on both sides */
    ADXT_GetTime(t, &count, &scale);
    CHECK(scale == 22050 && count >= 6600 && count <= 6615);
    CHECK(peak(n - FRAME, n, 0) == 0);

    /* Stereo, looping over its second half: still playing after 1.5 s,
     * without a click at the loop point. */
    ADXT_StartAfs(t, 0, 0);
    at = 0;
    n = run(90);
    printf("stereo: %.1f Hz left, %.1f Hz right, steps %d %d\n", freq(4800, n, 0),
           freq(4800, n, 1), max_step(4800, n, 0), max_step(4800, n, 1));
    CHECK(ADXT_GetStat(t) == ADXT_STAT_PLAYING);
    CHECK(fabs(freq(4800, n, 0) - 440) < 5);
    CHECK(fabs(freq(4800, n, 1) - 880) < 5);
    /* A 10000 sine at 880 Hz moves by at most about 1160 per sample. */
    CHECK(max_step(4800, n, 0) < 800 && max_step(4800, n, 1) < 1400);
    ADXT_GetTime(t, &count, &scale);
    CHECK(scale == 44100 && count > 60000); /* more than the file's 22050 */

    /* -20 dB */
    ADXT_SetOutVol(t, -200);
    at = 0;
    n = run(10);
    CHECK(peak(FRAME * 2, n, 0) > 900 && peak(FRAME * 2, n, 0) < 1100);

    ADXT_Stop(t);
    CHECK(ADXT_GetStat(t) == ADXT_STAT_STOP);
    at = 0;
    n = run(2);
    CHECK(peak(0, n, 0) == 0 && peak(0, n, 1) == 0);

    /* Without an audio device the streams still advance in real time. */
    pc_audio_open = 0;
    ADXT_SetOutVol(t, 0);
    ADXT_StartAfs(t, 0, 1);
    for (frames = 0; frames < 120 && ADXT_GetStat(t) == ADXT_STAT_PLAYING; frames++) {
        pc_host_sleep_ns(16666667);
        pc_sound_frame();
    }
    printf("no device: ended after %d frames\n", frames);
    CHECK(ADXT_GetStat(t) == ADXT_STAT_PLAYEND && frames >= 15 && frames <= 30);

    ADXT_Destroy(t);
    if (failures == 0)
        printf("test_sound: all checks passed\n");
    return failures != 0;
}
