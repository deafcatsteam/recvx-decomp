/*
 * The SPU2's reverb (see pc_spu2rev.h).
 *
 * The algorithm and the preset are those of the PlayStation's SPU, which
 * the SPU2 keeps, as documented by Martin Korth (nocash) in psx-spx,
 * "SPU Reverb Formula" and "Reverb Examples". It runs at half the output
 * rate (24 kHz on the SPU2) on a ring of 16-bit samples: two comb-filtered
 * reflections per side feed four echo taps, then two all-pass filters.
 * The buffer offsets are in samples (the PS1 register values times 4).
 */
#include "pc_spu2rev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
    mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2,
    vLIN, vRIN, NREGS
};

/* Hall (mode 5): a 0xADE0-byte work area. */
static const uint16_t hall[NREGS] = {
    0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00,
    0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1,
    0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A,
    0x8000, 0x8000,
};
#define HALL_SIZE (0xADE0 / 2)

typedef struct {
    int mode, depth;
    int32_t r[NREGS]; /* volumes signed, offsets in samples */
    int16_t *ring;
    uint32_t size, pos;
    /* 48 kHz <-> 24 kHz */
    int half;
    int32_t in_l, in_r;
    int32_t prev_l, prev_r, cur_l, cur_r;
} Rev;

static Rev revs[SPU2REV_CORES];

static int16_t clamp16(int32_t v)
{
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

static int16_t *at(Rev *r, int32_t off)
{
    int32_t i = (int32_t)r->pos + off % (int32_t)r->size;

    if (i < 0)
        i += (int32_t)r->size;
    return &r->ring[(uint32_t)i % r->size];
}

static int32_t mul(int32_t a, int32_t v)
{
    return a * v >> 15;
}

void spu2rev_set(int core, int mode, int depth)
{
    Rev *r;
    int i;

    if (core < 0 || core >= SPU2REV_CORES)
        return;
    r = &revs[core];
    if (mode != 0 && mode != 5) {
        printf("snd: reverb mode %d not done, off\n", mode);
        mode = 0;
    }
    r->depth = depth < 0 ? 0 : depth > 0x7fff ? 0x7fff : depth;
    if (mode == r->mode)
        return;
    r->mode = mode;
    printf("snd: reverb of core %d %s\n", core, mode ? "on (hall)" : "off");
    free(r->ring);
    r->ring = NULL;
    r->pos = 0;
    r->half = 0;
    r->prev_l = r->prev_r = r->cur_l = r->cur_r = 0;
    if (mode == 0)
        return;
    for (i = 0; i < NREGS; i++) {
        int16_t v = (int16_t)hall[i];
        int vol = i == vIIR || (i >= vCOMB1 && i <= vAPF2) || i == vLIN || i == vRIN;

        r->r[i] = vol ? v : (int32_t)hall[i] * 4;
    }
    r->size = HALL_SIZE;
    r->ring = calloc(r->size, sizeof(int16_t));
    if (r->ring == NULL)
        r->mode = 0;
}

/* One 24 kHz step: the inputs in, the outputs (before the depth) out. */
static void step(Rev *r, int32_t in_l, int32_t in_r, int32_t *out_l, int32_t *out_r)
{
    const int32_t *g = r->r;
    int32_t lin = mul(in_l, g[vLIN]), rin = mul(in_r, g[vRIN]);
    int32_t l, rr, a;

    /* same-side and other-side reflections */
    a = *at(r, g[mLSAME] - 1);
    *at(r, g[mLSAME]) = clamp16(mul(lin + mul(*at(r, g[dLSAME]), g[vWALL]) - a, g[vIIR]) + a);
    a = *at(r, g[mRSAME] - 1);
    *at(r, g[mRSAME]) = clamp16(mul(rin + mul(*at(r, g[dRSAME]), g[vWALL]) - a, g[vIIR]) + a);
    a = *at(r, g[mLDIFF] - 1);
    *at(r, g[mLDIFF]) = clamp16(mul(lin + mul(*at(r, g[dRDIFF]), g[vWALL]) - a, g[vIIR]) + a);
    a = *at(r, g[mRDIFF] - 1);
    *at(r, g[mRDIFF]) = clamp16(mul(rin + mul(*at(r, g[dLDIFF]), g[vWALL]) - a, g[vIIR]) + a);

    /* early echo */
    l = mul(*at(r, g[mLCOMB1]), g[vCOMB1]) + mul(*at(r, g[mLCOMB2]), g[vCOMB2]) +
        mul(*at(r, g[mLCOMB3]), g[vCOMB3]) + mul(*at(r, g[mLCOMB4]), g[vCOMB4]);
    rr = mul(*at(r, g[mRCOMB1]), g[vCOMB1]) + mul(*at(r, g[mRCOMB2]), g[vCOMB2]) +
         mul(*at(r, g[mRCOMB3]), g[vCOMB3]) + mul(*at(r, g[mRCOMB4]), g[vCOMB4]);

    /* late reverb: two all-pass filters */
    a = *at(r, g[mLAPF1] - g[dAPF1]);
    l = clamp16(l - mul(a, g[vAPF1]));
    *at(r, g[mLAPF1]) = (int16_t)l;
    l = clamp16(mul(l, g[vAPF1]) + a);
    a = *at(r, g[mRAPF1] - g[dAPF1]);
    rr = clamp16(rr - mul(a, g[vAPF1]));
    *at(r, g[mRAPF1]) = (int16_t)rr;
    rr = clamp16(mul(rr, g[vAPF1]) + a);

    a = *at(r, g[mLAPF2] - g[dAPF2]);
    l = clamp16(l - mul(a, g[vAPF2]));
    *at(r, g[mLAPF2]) = (int16_t)l;
    l = clamp16(mul(l, g[vAPF2]) + a);
    a = *at(r, g[mRAPF2] - g[dAPF2]);
    rr = clamp16(rr - mul(a, g[vAPF2]));
    *at(r, g[mRAPF2]) = (int16_t)rr;
    rr = clamp16(mul(rr, g[vAPF2]) + a);

    r->pos = (r->pos + 1) % r->size;
    *out_l = l;
    *out_r = rr;
}

void spu2rev_mix(int core, const int32_t *in, int32_t *out, int n)
{
    Rev *r;
    int k;

    if (core < 0 || core >= SPU2REV_CORES)
        return;
    r = &revs[core];
    if (r->mode == 0)
        return;
    for (k = 0; k < n; k++) {
        /* two input samples make one reverb step; the output is
         * interpolated back between steps */
        r->in_l += in[2 * k];
        r->in_r += in[2 * k + 1];
        if (r->half) {
            r->prev_l = r->cur_l;
            r->prev_r = r->cur_r;
            step(r, clamp16(r->in_l >> 1), clamp16(r->in_r >> 1), &r->cur_l, &r->cur_r);
            r->in_l = r->in_r = 0;
            out[2 * k] += mul(r->prev_l, r->depth);
            out[2 * k + 1] += mul(r->prev_r, r->depth);
        } else {
            out[2 * k] += mul((r->prev_l + r->cur_l) >> 1, r->depth);
            out[2 * k + 1] += mul((r->prev_r + r->cur_r) >> 1, r->depth);
        }
        r->half ^= 1;
    }
}
