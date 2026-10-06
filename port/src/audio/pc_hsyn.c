/*
 * Hardware-synth stand-in (see pc_hsyn.h): HD program lookup, PS-ADPCM
 * voices with the SPU2 envelope, mixed at 48 kHz.
 *
 * Adapted from recvx-vita (platform/iop/hsyn.c, MIT licence,
 * Copyright (c) 2024-2026 AshfordFamily); the HD reads are bounds-checked
 * here, since a bank that is not what is expected must not crash the game.
 *
 * HD layout (little endian, offsets relative to the chunk that holds them):
 *   "IECSsreV" version chunk, then the "IECSdaeH" head chunk at 0x10:
 *   Head  +20 prog chunk, +24 sample-set chunk, +28 sample chunk, +32 vag-info chunk
 *   chunk +12 max index, +16 u32 offsets[max+1] (0xffffffff = none)
 *   prog  +0 split offset (from the program), +4 nsplit, +5 split size,
 *         +6 volume, +7 pan, +8 transpose, +9 detune
 *   split +0 sample set, +2 key low, +4 key high, +6 bend range (1/128 semitone),
 *         +16 volume, +17 pan, +18 transpose, +19 detune
 *   sset  +3 count, +4 u16 sample index[]
 *   smpl  +0 vag index, +2/+4 velocity range, +11 base note, +12 detune, +13 pan,
 *         +16 volume, +18 ADSR1, +20 ADSR2, +41 SPU attributes (4/8 effect
 *         send left/right, 0x10/0x20 core 0/1)
 *   vagi  +0 BD offset, +4 sample rate, +6 loop
 * BD: PS-ADPCM, 16-byte blocks of 28 samples (flags: 1 end, 2 repeat, 4 loop start).
 */
#include "pc_hsyn.h"

#include "../host/pc_spatial.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VOICES 48 /* as many as the SPU2 has */

typedef struct {
    uint8_t *hd, *bd;
    uint32_t hdsize, bdsize;
    int owned;
} Bank;

typedef struct {
    uint8_t prog, vol, pan, expr;
    uint16_t bend;
    float dir[4]; /* 3D sound: where it is, [3] = 1 when known */
} Chan;

typedef struct {
    int master;       /* 0-0x3fff */
    uint8_t vol, pan; /* port volume, pan offset (centre 64) */
    uint16_t bend;    /* added to every channel's bend, 0x2000 centre */
    float dir[4];     /* for the channels without their own */
} Port;

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    uint8_t active, port, ch, note;
    uint32_t serial;
    const uint8_t *bd;
    uint32_t bdsize;
    /* ADPCM stream */
    uint32_t block, loop; /* byte offsets in bd */
    int16_t pcm[28];
    int idx; /* next sample in pcm[] */
    int32_t h1, h2;
    int ended; /* the last block has been decoded */
    int16_t s0, s1;
    uint32_t frac; /* 16.16 */
    uint32_t step;
    /* pitch */
    float rate, semis, bend_range;
    /* envelope */
    uint16_t adsr1, adsr2;
    int phase;
    int32_t env;
    int32_t env_wait;
    /* gain */
    int32_t gain; /* 0..32767 from program, split, sample and velocity */
    int pan;      /* -64..63 before the channel's pan */
    int32_t vol_l, vol_r;
    int fx;   /* sent to the reverb of this SPU2 core + 1, 0 if not */
    /* 3D sound for headphones: vol_c is the gain before any pan */
    int spatial;
    int32_t vol_c;
    PcSpatial sp;
} Voice;

static Bank banks[HSYN_PORTS];
static Chan chans[HSYN_PORTS][16];
static Port ports[HSYN_PORTS];
static Voice voices[MAX_VOICES];
static uint32_t serial;
static int master = 0x3fff;
static int chans_ready;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static void chans_init(void)
{
    int p, c;

    for (p = 0; p < HSYN_PORTS; p++) {
        for (c = 0; c < 16; c++) {
            Chan d = { 0, 127, 64, 127, 0x2000 };
            chans[p][c] = d;
        }
        ports[p].master = 0x3fff;
        ports[p].vol = 127;
        ports[p].pan = 64;
        ports[p].bend = 0x2000;
    }
    chans_ready = 1;
}

/* ---- HD lookup (every read checked against the header's size) ---------- */

typedef struct {
    const uint8_t *hd;
    uint32_t size;
} Hd;

static const uint8_t *at(const Hd *h, uint32_t off, uint32_t len)
{
    return off <= h->size && len <= h->size - off ? h->hd + off : NULL;
}

/* A chunk: which is 20 (programs), 24 (sample sets), 28 (samples) or 32
 * (sample info), the offset of its pointer in the head chunk. */
static const uint8_t *chunk(const Hd *h, int which)
{
    const uint8_t *p = at(h, 0x10 + which, 4);

    return p != NULL ? at(h, rd32(p), 16) : NULL;
}

static const uint8_t *entry(const Hd *h, const uint8_t *ck, int idx, uint32_t len)
{
    const uint8_t *o;
    uint32_t off;

    if (ck == NULL || idx < 0 || (uint32_t)idx > rd32(ck + 12))
        return NULL;
    o = at(h, (uint32_t)(ck - h->hd) + 16 + 4 * (uint32_t)idx, 4);
    if (o == NULL)
        return NULL;
    off = rd32(o);
    return off == 0xffffffff ? NULL : at(h, (uint32_t)(ck - h->hd) + off, len);
}

int hsyn_hd_valid(const uint8_t *hd, uint32_t size)
{
    return hd != NULL && size >= 0x40 && memcmp(hd, "IECSsreV", 8) == 0 &&
           memcmp(hd + 0x10, "IECSdaeH", 8) == 0;
}

static Hd hd_of(const uint8_t *hd)
{
    Hd h = { hd, 0 };
    int p;

    /* The size is the loaded bank's when it is one, else the header's own. */
    for (p = 0; p < HSYN_PORTS; p++)
        if (banks[p].hd == hd)
            h.size = banks[p].hdsize;
    if (h.size == 0 && hd != NULL)
        h.size = rd32(hd + 0x10 + 12);
    return h;
}

void hsyn_hd_attrs(const uint8_t *hd, uint32_t size, int counts[4])
{
    Hd h = { hd, size };
    const uint8_t *ck = hsyn_hd_valid(hd, size) ? chunk(&h, 28) : NULL;
    int i, max;

    memset(counts, 0, 4 * sizeof(int));
    if (ck == NULL)
        return;
    max = (int)rd32(ck + 12);
    for (i = 0; i <= max && i < 4096; i++) {
        const uint8_t *sm = entry(&h, ck, i, 42);

        if (sm == NULL)
            continue;
        counts[0]++;
        if ((sm[41] & ~0x3f) != 0)
            counts[3]++;
        else if ((sm[41] & 0x0c) != 0)
            counts[sm[41] & 0x20 ? 2 : 1]++;
    }
}

int hsyn_hd_max_program(const uint8_t *hd)
{
    Hd h = hd_of(hd);
    const uint8_t *ck = hd != NULL ? chunk(&h, 20) : NULL;

    return ck != NULL ? (int)rd32(ck + 12) : -1;
}

static int vag_info(const Hd *h, uint32_t bdsize, int vi, HsynSampleInfo *o)
{
    const uint8_t *vagi = chunk(h, 32);
    const uint8_t *v = entry(h, vagi, vi, 8);
    const uint8_t *next;
    uint32_t end;

    if (v == NULL)
        return 0;
    o->bd_offset = rd32(v);
    o->rate = rd16(v + 4);
    o->loop = v[6];
    next = entry(h, vagi, vi + 1, 8);
    end = next != NULL ? rd32(next) : bdsize;
    o->bd_size = end > o->bd_offset ? end - o->bd_offset : 0;
    return o->bd_offset < bdsize;
}

int hsyn_hd_program_sample(const uint8_t *hd, uint32_t bdsize, int prog, HsynSampleInfo *o)
{
    Hd h = hd_of(hd);
    const uint8_t *p, *split, *ss, *sm;

    if (hd == NULL)
        return 0;
    p = entry(&h, chunk(&h, 20), prog, 10);
    if (p == NULL || p[4] == 0)
        return 0;
    split = at(&h, (uint32_t)(p - hd) + rd32(p), 20);
    if (split == NULL)
        return 0;
    ss = entry(&h, chunk(&h, 24), rd16(split), 6);
    if (ss == NULL || ss[3] == 0)
        return 0;
    sm = entry(&h, chunk(&h, 28), rd16(ss + 4), 22);
    if (sm == NULL)
        return 0;
    o->base_note = sm[11];
    return vag_info(&h, bdsize, rd16(sm), o);
}

/* ---- Banks -------------------------------------------------------------- */

static void bank_put(int port, uint8_t *hd, uint32_t hdsize, uint8_t *bd, uint32_t bdsize, int owned)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++)
        if (voices[i].active && voices[i].port == port)
            voices[i].active = 0;
    if (banks[port].owned) {
        free(banks[port].hd);
        free(banks[port].bd);
    }
    banks[port].hd = hd;
    banks[port].bd = bd;
    banks[port].hdsize = hdsize;
    banks[port].bdsize = bdsize;
    banks[port].owned = owned;
}

void hsyn_bank_set(int port, uint8_t *hd, uint32_t hdsize, uint8_t *bd, uint32_t bdsize)
{
    if (port < 0 || port >= HSYN_PORTS) {
        free(hd);
        free(bd);
        return;
    }
    bank_put(port, hd, hdsize, bd, bdsize, 1);
}

void hsyn_bank_ref(int port, const uint8_t *hd, uint32_t hdsize, const uint8_t *bd, uint32_t bdsize)
{
    if (port >= 0 && port < HSYN_PORTS)
        bank_put(port, (uint8_t *)hd, hdsize, (uint8_t *)bd, bdsize, 0);
}

const uint8_t *hsyn_bank_hd(int port)
{
    return port >= 0 && port < HSYN_PORTS ? banks[port].hd : NULL;
}

uint32_t hsyn_bank_bd_size(int port)
{
    return port >= 0 && port < HSYN_PORTS ? banks[port].bdsize : 0;
}

/* ---- PS-ADPCM ----------------------------------------------------------- */

static const int8_t vag_f[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static void decode_block(Voice *v)
{
    const uint8_t *b;
    int shift, filt, flags, i;

    if (v->ended || v->block + 16 > v->bdsize) {
        memset(v->pcm, 0, sizeof(v->pcm));
        v->ended = 2; /* nothing left */
        v->idx = 0;
        return;
    }
    b = v->bd + v->block;
    shift = b[0] & 15;
    filt = (b[0] >> 4) & 7;
    flags = b[1];
    if (filt > 4)
        filt = 0;
    if (shift > 12)
        shift = 9;
    if (flags & 4)
        v->loop = v->block;
    for (i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
        int32_t s = (int16_t)(nib << 12) >> shift;

        s += (v->h1 * vag_f[filt][0] + v->h2 * vag_f[filt][1] + 32) >> 6;
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;
        v->h2 = v->h1;
        v->h1 = s;
        v->pcm[i] = (int16_t)s;
    }
    v->idx = 0;
    if (flags & 1) {
        if (flags & 2)
            v->block = v->loop;
        else
            v->ended = 1; /* this block plays out, then the voice stops */
    } else {
        v->block += 16;
    }
}

static int16_t next_sample(Voice *v)
{
    if (v->idx >= 28)
        decode_block(v);
    return v->pcm[v->idx++];
}

/* ---- SPU2 envelope ------------------------------------------------------ */

static void env_step(Voice *v, int exp_mode, int decreasing, int shift, int step)
{
    int32_t cycles, inc;

    if (v->env_wait > 0) {
        v->env_wait--;
        return;
    }
    cycles = 1 << (shift > 11 ? shift - 11 : 0);
    inc = step << (shift < 11 ? 11 - shift : 0);
    if (exp_mode && !decreasing && v->env > 0x6000)
        cycles *= 4;
    if (exp_mode && decreasing)
        inc = inc * v->env >> 15;
    v->env += inc;
    if (v->env > 0x7fff)
        v->env = 0x7fff;
    if (v->env < 0)
        v->env = 0;
    v->env_wait = cycles - 1;
}

static void env_tick(Voice *v)
{
    uint16_t a1 = v->adsr1, a2 = v->adsr2;

    switch (v->phase) {
    case ENV_ATTACK:
        env_step(v, a1 >> 15, 0, (a1 >> 10) & 31, 7 - ((a1 >> 8) & 3));
        if (v->env >= 0x7fff) {
            v->phase = ENV_DECAY;
            v->env_wait = 0;
        }
        break;
    case ENV_DECAY: {
        int32_t sl = ((a1 & 15) + 1) * 0x800;

        env_step(v, 1, 1, (a1 >> 4) & 15, -8);
        if (v->env <= sl) {
            v->env = sl > 0x7fff ? 0x7fff : sl;
            v->phase = ENV_SUSTAIN;
            v->env_wait = 0;
        }
        break;
    }
    case ENV_SUSTAIN: {
        int dec = (a2 >> 14) & 1, s = (a2 >> 6) & 3;

        env_step(v, a2 >> 15, dec, (a2 >> 8) & 31, dec ? -8 + s : 7 - s);
        break;
    }
    case ENV_RELEASE:
        env_step(v, (a2 >> 5) & 1, 1, a2 & 31, -8);
        if (v->env <= 0) {
            v->phase = ENV_OFF;
            v->active = 0;
        }
        break;
    }
}

/* ---- Voices ------------------------------------------------------------- */

static void voice_pitch(Voice *v)
{
    Chan *c = &chans[v->port][v->ch];
    float bend = (c->bend + ports[v->port].bend - 2 * 8192) / 8192.0f * v->bend_range;
    float hz = v->rate * exp2f((v->semis + bend) / 12.0f);

    v->step = (uint32_t)(hz / HSYN_RATE * 65536.0f);
}

static void voice_volume(Voice *v)
{
    Chan *c = &chans[v->port][v->ch];
    Port *pt = &ports[v->port];
    int pan = v->pan + c->pan + pt->pan - 64; /* centre 64 */
    int64_t g;

    if (pan < 0)
        pan = 0;
    if (pan > 127)
        pan = 127;
    g = (int64_t)v->gain * c->vol * c->expr * pt->vol / (127 * 127 * 127);
    g = g * pt->master / 0x3fff * master / 0x3fff;
    v->spatial = 0;
    if (c->dir[3] != 0.0f || pt->dir[3] != 0.0f) {
        /* 3D sound: the direction instead of the pan */
        const float *dir = c->dir[3] != 0.0f ? c->dir : pt->dir;
        float lr[2];

        switch (pc_spatial_mode()) {
        case PC_3D_HEADPHONES:
            pc_spatial_aim(&v->sp, dir);
            v->spatial = 1;
            v->vol_c = (int32_t)g;
            return;
        case PC_3D_SPEAKERS:
            pc_spatial_pan(dir, lr);
            v->vol_l = (int32_t)(g * lr[0]);
            v->vol_r = (int32_t)(g * lr[1]);
            return;
        }
    }
    v->vol_l = (int32_t)(g * (pan <= 64 ? 64 : 127 - pan) / 64);
    v->vol_r = (int32_t)(g * (pan >= 64 ? 64 : pan) / 64);
}

static Voice *voice_alloc(void)
{
    Voice *best = NULL;
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];
        int rel = v->phase == ENV_RELEASE, best_rel;

        if (!v->active)
            return v;
        /* steal the oldest releasing voice, else the oldest */
        best_rel = best != NULL && best->phase == ENV_RELEASE;
        if (best == NULL || rel > best_rel || (rel == best_rel && v->serial < best->serial))
            best = v;
    }
    return best;
}

static int pan_rel(uint8_t p) /* HD pans: 0-127 centre 64, 0x80+ reversed */
{
    return p > 127 ? 64 - (p - 127) - 64 : p - 64;
}

/* Whether a sample is sent to the reverb, and of which core. */
static int sample_fx(const Hd *h, const uint8_t *sm)
{
    const uint8_t *a = at(h, (uint32_t)(sm - h->hd) + 41, 1);

    if (a == NULL || (*a & ~0x3f) != 0 || (*a & 0x0c) == 0)
        return 0;
    return *a & 0x20 ? 2 : 1;
}

void hsyn_note_on(int port, int ch, int note, int vel)
{
    Bank *b;
    Hd h;
    Chan *c;
    const uint8_t *p;
    int s, k;

    if (!chans_ready)
        chans_init();
    b = &banks[port];
    if (b->hd == NULL || b->bd == NULL)
        return;
    h.hd = b->hd;
    h.size = b->hdsize;
    c = &chans[port][ch];
    p = entry(&h, chunk(&h, 20), c->prog, 10);
    if (p == NULL)
        return;
    for (s = 0; s < p[4]; s++) {
        const uint8_t *split = at(&h, (uint32_t)(p - h.hd) + rd32(p) + (uint32_t)s * p[5], 20);
        const uint8_t *ss;
        int lo, hi;

        if (split == NULL)
            break;
        lo = split[2];
        hi = split[4];
        if (hi < lo)
            hi = 127;
        if (note < lo || note > hi)
            continue;
        ss = entry(&h, chunk(&h, 24), rd16(split), 4);
        if (ss == NULL || at(&h, (uint32_t)(ss - h.hd) + 4, 2u * ss[3]) == NULL)
            continue;
        for (k = 0; k < ss[3]; k++) {
            const uint8_t *sm = entry(&h, chunk(&h, 28), rd16(ss + 4 + 2 * k), 22);
            HsynSampleInfo vi;
            Voice *v;
            uint16_t br;

            if (sm == NULL || vel < sm[2] || vel > sm[4])
                continue;
            if (!vag_info(&h, b->bdsize, rd16(sm), &vi))
                continue;
            v = voice_alloc();
            memset(v, 0, sizeof(*v));
            v->active = 1;
            v->port = (uint8_t)port;
            v->ch = (uint8_t)ch;
            v->note = (uint8_t)note;
            v->serial = ++serial;
            v->bd = b->bd;
            v->bdsize = b->bdsize;
            v->block = v->loop = vi.bd_offset;
            v->idx = 28;
            v->rate = vi.rate;
            v->semis = note - sm[11] + (int8_t)p[8] + (int8_t)split[18] +
                       ((int8_t)p[9] + (int8_t)split[19] + (int8_t)sm[12]) / 128.0f;
            br = rd16(split + 6);
            v->bend_range = br ? br / 128.0f : 2.0f;
            v->adsr1 = rd16(sm + 18);
            v->adsr2 = rd16(sm + 20);
            v->phase = ENV_ATTACK;
            v->gain = (int32_t)((int64_t)32767 * p[6] * split[16] * sm[16] * vel /
                                (127LL * 127 * 127 * 127));
            v->pan = pan_rel(p[7]) + pan_rel(split[17]) + pan_rel(sm[13]);
            v->fx = sample_fx(&h, sm);
            v->s0 = next_sample(v);
            v->s1 = next_sample(v);
            voice_pitch(v);
            voice_volume(v);
        }
    }
}

void hsyn_note_off(int port, int ch, int note)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];

        if (v->active && v->port == port && v->ch == ch && v->note == note && v->phase != ENV_RELEASE) {
            v->phase = ENV_RELEASE;
            v->env_wait = 0;
        }
    }
}

void hsyn_sound_off(int port, int ch)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++)
        if (voices[i].active && voices[i].port == port && voices[i].ch == ch)
            voices[i].active = 0;
}

int hsyn_channel_active(int port, int ch)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++)
        if (voices[i].active && voices[i].port == port && voices[i].ch == ch)
            return 1;
    return 0;
}

void hsyn_program(int port, int ch, int prog)
{
    if (!chans_ready)
        chans_init();
    chans[port][ch].prog = (uint8_t)prog;
}

static void chan_update(int port, int ch, int pitch)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];

        if (!v->active || v->port != port || v->ch != ch)
            continue;
        if (pitch)
            voice_pitch(v);
        else
            voice_volume(v);
    }
}

void hsyn_volume(int port, int ch, int vol)
{
    if (!chans_ready)
        chans_init();
    chans[port][ch].vol = (uint8_t)vol;
    chan_update(port, ch, 0);
}

void hsyn_pan(int port, int ch, int pan)
{
    if (!chans_ready)
        chans_init();
    chans[port][ch].pan = (uint8_t)pan;
    chan_update(port, ch, 0);
}

static void set_dir(float d[4], const float *dir)
{
    if (dir == NULL) {
        d[3] = 0.0f;
        return;
    }
    d[0] = dir[0];
    d[1] = dir[1];
    d[2] = dir[2];
    d[3] = 1.0f;
}

void hsyn_dir(int port, int ch, const float *dir)
{
    if (!chans_ready)
        chans_init();
    set_dir(chans[port][ch].dir, dir);
    chan_update(port, ch, 0);
}

void hsyn_bend(int port, int ch, int bend)
{
    if (!chans_ready)
        chans_init();
    chans[port][ch].bend = (uint16_t)bend;
    chan_update(port, ch, 1);
}

void hsyn_expression(int port, int ch, int expr)
{
    if (!chans_ready)
        chans_init();
    chans[port][ch].expr = (uint8_t)expr;
    chan_update(port, ch, 0);
}

static void port_update(int port, int pitch)
{
    int ch;

    for (ch = 0; ch < 16; ch++)
        chan_update(port, ch, pitch);
}

void hsyn_port_master(int port, int vol)
{
    if (!chans_ready)
        chans_init();
    ports[port].master = vol < 0 ? 0 : vol > 0x3fff ? 0x3fff : vol;
    port_update(port, 0);
}

void hsyn_port_volume(int port, int vol)
{
    if (!chans_ready)
        chans_init();
    ports[port].vol = (uint8_t)vol;
    port_update(port, 0);
}

void hsyn_port_pan(int port, int pan)
{
    if (!chans_ready)
        chans_init();
    ports[port].pan = (uint8_t)pan;
    port_update(port, 0);
}

void hsyn_port_dir(int port, const float *dir)
{
    if (!chans_ready)
        chans_init();
    set_dir(ports[port].dir, dir);
    port_update(port, 0);
}

void hsyn_port_bend(int port, int bend)
{
    if (!chans_ready)
        chans_init();
    ports[port].bend = (uint16_t)bend;
    port_update(port, 1);
}

void hsyn_note_release_all(int port)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];

        if (v->active && v->port == port && v->phase != ENV_RELEASE) {
            v->phase = ENV_RELEASE;
            v->env_wait = 0;
        }
    }
}

void hsyn_reset_channels(int port)
{
    int c;

    if (!chans_ready)
        chans_init();
    for (c = 0; c < 16; c++) {
        Chan d = { 0, 127, 64, 127, 0x2000 };
        chans[port][c] = d;
    }
}

void hsyn_master_volume(int vol)
{
    int i;

    if (!chans_ready)
        chans_init();
    master = vol < 0 ? 0 : vol > 0x3fff ? 0x3fff : vol;
    for (i = 0; i < MAX_VOICES; i++)
        if (voices[i].active)
            voice_volume(&voices[i]);
}

/* ---- Mixing ------------------------------------------------------------- */

void hsyn_mix(int32_t *out, int32_t *fx, int n)
{
    int i, k;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];

        if (!v->active)
            continue;
        for (k = 0; k < n && v->active; k++) {
            int32_t f = v->frac >> 4; /* 12 bits */
            int32_t s = v->s0 + ((v->s1 - v->s0) * f >> 12);

            int32_t l, r;

            s = s * v->env >> 15;
            if (v->spatial) {
                float ears[2];

                pc_spatial_run(&v->sp, (float)s, ears);
                l = (int32_t)(ears[0] * v->vol_c * (1.0f / 32768.0f));
                r = (int32_t)(ears[1] * v->vol_c * (1.0f / 32768.0f));
            } else {
                l = s * v->vol_l >> 15;
                r = s * v->vol_r >> 15;
            }
            out[2 * k] += l;
            out[2 * k + 1] += r;
            if (v->fx && fx != NULL) {
                int32_t *e = fx + (v->fx - 1) * 2 * n;

                e[2 * k] += l;
                e[2 * k + 1] += r;
            }
            env_tick(v);
            v->frac += v->step;
            while (v->frac >= 0x10000) {
                v->frac -= 0x10000;
                v->s0 = v->s1;
                v->s1 = next_sample(v);
            }
            /* a one-shot that ran off its end is done once its last block played */
            if (v->ended == 2)
                v->active = 0;
        }
    }
}
