/*
 * 3D sound for headphones and speakers, see pc_spatial.h.
 *
 * Headphones: each ear hears the sound through a delay and a first-order
 * "head shadow" filter, both set from the angle between the ear and the
 * sound (C. P. Brown and R. O. Duda, "A structural model for binaural sound
 * synthesis", 1998): the ear facing the sound gets it first, with its highs
 * raised a little; the other gets it up to 0.66 ms later with its highs
 * lowered. Low notes reach both ears at the same level, as they do around a
 * real head. A sound behind also loses some of its highs.
 */
#include "pc_spatial.h"
#include "pc_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000.0f
#define HEAD_RADIUS 0.0875f /* m */
#define SOUND_SPEED 343.0f  /* m/s */

static int mode = -1;

int pc_spatial_mode(void)
{
    if (mode < 0) {
        const char *v = pc_config_get("sound_3d");

        mode = PC_3D_OFF;
        if (v != NULL && strcmp(v, "headphones") == 0)
            mode = PC_3D_HEADPHONES;
        else if (v != NULL && strcmp(v, "speakers") == 0)
            mode = PC_3D_SPEAKERS;
        else if (v != NULL && v[0] != 0 && strcmp(v, "off") != 0 && strcmp(v, "no") != 0)
            printf("config: sound_3d should be off, headphones or speakers, not '%s'\n", v);
    }
    return mode;
}

void pc_spatial_set_mode(int m)
{
    mode = m;
}

static void unit(const float dir[3], float u[3])
{
    float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);

    if (l < 1e-6f) { /* on the listener: in front */
        u[0] = u[1] = 0.0f;
        u[2] = 1.0f;
        return;
    }
    u[0] = dir[0] / l;
    u[1] = dir[1] / l;
    u[2] = dir[2] / l;
}

void pc_spatial_aim(PcSpatial *s, const float dir[3])
{
    const float ear_delay = HEAD_RADIUS / SOUND_SPEED * RATE; /* 12.2 samples */
    float u[3];
    int e;

    unit(dir, u);
    for (e = 0; e < 2; e++) {
        /* angle between the ear (left: -x, right: +x) and the sound */
        float c = e == 0 ? -u[0] : u[0];
        float t = acosf(c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c);

        s->target[e] = ear_delay * (t < 1.5707963f ? 1.0f - c : 1.0f + t - 1.5707963f);
        /* 2 (+6 dB of highs) facing the ear, 0.1 at 150 degrees */
        s->target[2 + e] = 1.05f + 0.95f * cosf(t * 1.2f);
    }
    s->target[4] = u[2] < 0.0f ? -u[2] : 0.0f;
    s->target[5] = 0.0f;
    if (!s->primed) {
        memcpy(s->cur, s->target, sizeof(s->cur));
        s->primed = 1;
    }
}

void pc_spatial_reset(PcSpatial *s)
{
    memset(s, 0, sizeof(*s));
}

void pc_spatial_run(PcSpatial *s, float in, float out[2])
{
    /* head shadow: (alpha s + beta) / (s + beta) by the bilinear transform */
    const float k = 2.0f * RATE, beta = 2.0f * SOUND_SPEED / HEAD_RADIUS;
    const float inv = 1.0f / (k + beta), a1 = (beta - k) * inv;
    const float rear_pole = 0.633f; /* one-pole low-pass at about 3.5 kHz */
    int e, i;

    for (i = 0; i < 5; i++)
        s->cur[i] += (s->target[i] - s->cur[i]) * 0.002f; /* about 10 ms */
    s->ring[s->w] = in;
    for (e = 0; e < 2; e++) {
        float pos = (float)s->w - s->cur[e];
        int i0, i1;
        float f, x, y, alpha = s->cur[2 + e];

        if (pos < 0.0f)
            pos += PC_SPATIAL_DELAY;
        i0 = (int)pos;
        f = pos - (float)i0;
        i0 &= PC_SPATIAL_DELAY - 1;
        i1 = (i0 + 1) & (PC_SPATIAL_DELAY - 1);
        x = s->ring[i0] + (s->ring[i1] - s->ring[i0]) * f;

        y = (alpha * k + beta) * inv * x + (beta - alpha * k) * inv * s->shadow[e][0] - a1 * s->shadow[e][1];
        s->shadow[e][0] = x;
        s->shadow[e][1] = y;

        s->rear[e] = s->rear[e] * rear_pole + y * (1.0f - rear_pole);
        out[e] = y - s->cur[4] * 0.5f * (y - s->rear[e]);
    }
    s->w = (s->w + 1) & (PC_SPATIAL_DELAY - 1);
}

void pc_spatial_pan(const float dir[3], float g[2])
{
    float u[3], a;

    unit(dir, u);
    a = (u[0] + 1.0f) * 0.78539816f;
    g[0] = cosf(a) * 1.41421356f;
    g[1] = sinf(a) * 1.41421356f;
    if (g[0] > 1.0f)
        g[0] = 1.0f;
    if (g[1] > 1.0f)
        g[1] = 1.0f;
}
