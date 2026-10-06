/*
 * PC stand-in for the game's IOP sound driver, TSNDDRV (Tamsoft's sound
 * driver, an IOP module of the disc).
 *
 * The game's side (ps2_snddrv.c, ps2_sg_sd.c) sends it a stream of requests
 * over SIF RPC: sound banks to load (Sony HD headers, BD samples sent in
 * pieces, SQ sequences), effects to play, change and stop, and sequences to
 * play (ambient loops and jingles driven by the sound driver; the music
 * proper is ADX, see pc_adx.c). It answers through a status block in IOP
 * memory (SND_STATUS): which effect slots and sequence ports still play,
 * and checksums of the received headers, which the game compares with its
 * own before sending the samples.
 *
 * The banks are played by the synth stand-in (pc_hsyn.c) and the sequences
 * by pc_hseq.c, on the audio thread, with the driver's 240 Hz tick.
 *
 * Adapted from recvx-vita (platform/iop/tsnddrv.c, MIT licence,
 * Copyright (c) 2024-2026 AshfordFamily). Differences: an effect slot is
 * busy for as long as its voices sound (instead of a timer computed from
 * the sample length), and the first banks and requests are written to the
 * log to help with problems.
 */
#include "pc_snddrv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host/pc_host.h"
#include "../host/pc_sound.h"
#include "pc_hseq.h"
#include "pc_hsyn.h"
#include "pc_spu2rev.h"

typedef struct {
    uint16_t se_info[6];
    uint16_t midi_info;
    int16_t port_info[8];
    int16_t midi_sum[4];
    int16_t se_sum[5];
    uint16_t dummy[9];
} SndStatus; /* SND_STATUS, 0x42 bytes */

#define BD_HALF 204800 /* the two halves of the IOP buffer for samples */
#define BD_PIECE 49152 /* largest piece sent at once */

typedef struct {
    uint8_t *data;
    uint32_t size;
} Blob;

static SndStatus status;
static Blob midi_hd[4], midi_bd[4], se_hd[5], sq[8], bd[128];
static int last_bd_slot = -1;
static int log_budget = 40;

static void blob_set(Blob *b, const void *src, uint32_t off, uint32_t len)
{
    if (off + len > b->size) {
        uint8_t *grown = realloc(b->data, off + len);

        if (grown == NULL)
            return;
        b->data = grown;
        memset(b->data + b->size, 0, off + len - b->size);
        b->size = off + len;
    }
    memcpy(b->data + off, src, len);
}

static uint8_t *blob_dup(const Blob *b)
{
    uint8_t *p = malloc(b->size ? b->size : 1);

    if (p != NULL && b->size)
        memcpy(p, b->data, b->size);
    return p;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- Effects (the driver's "TQ" requests) ------------------------------
 * Each effect bank has 16 slots. A request keys the effect's program on the
 * bank's synth port at the sample's base note. The game sees a slot as busy
 * while its bit is set in status.se_info[bank]. */

#define SE_PORTS 6
#define SE_SYNTH(port) (10 + (port))
enum { TQ_ACTIVE = 1, TQ_CANCEL = 2, TQ_START = 4, TQ_VOL = 0x20, TQ_PAN = 0x40, TQ_PITCH = 0x80 };

typedef struct {
    uint32_t flags;
    uint8_t se, note, vol, pan;
    uint16_t pitch;
    float dir[4]; /* where the sound is, [3] = 1 when known (3D sound) */
} TqSlot;

typedef struct {
    uint8_t note, valid;
} SeEntry;

static TqSlot tq[SE_PORTS][16];
/* Directions given by pc_snddrv_dir, for the next request of each slot
 * (and each sequence port). */
static float dir_next[SE_PORTS][16][4], bgm_dir_next[HSEQ_PORTS][4];
static uint16_t tq_busy[SE_PORTS];
static SeEntry *se_tbl[SE_PORTS];
static int se_max[SE_PORTS] = { -1, -1, -1, -1, -1, -1 };

static void se_build(int port)
{
    const uint8_t *hd = hsyn_bank_hd(SE_SYNTH(port));
    int i;

    free(se_tbl[port]);
    se_tbl[port] = NULL;
    se_max[port] = hsyn_hd_max_program(hd);
    if (se_max[port] < 0)
        return;
    se_tbl[port] = calloc(se_max[port] + 1, sizeof(SeEntry));
    if (se_tbl[port] == NULL) {
        se_max[port] = -1;
        return;
    }
    for (i = 0; i <= se_max[port]; i++) {
        HsynSampleInfo si;

        if (hsyn_hd_program_sample(hd, hsyn_bank_bd_size(SE_SYNTH(port)), i, &si) && si.rate) {
            se_tbl[port][i].valid = 1;
            se_tbl[port][i].note = si.base_note;
        }
    }
}

static void tq_req(int port, int se, int ch, int vol, int pan, int pitch)
{
    TqSlot *t;

    if (log_budget > 0) {
        log_budget--;
        printf("snd: effect bank %d sound %d slot %d (volume %d, pan %d, pitch %d)%s\n", port, se,
               ch, vol, pan, pitch,
               port < SE_PORTS && se <= se_max[port] && se_tbl[port] && se_tbl[port][se].valid
                   ? ""
                   : " - not in the bank");
    }
    if (port >= SE_PORTS || ch > 15 || se > se_max[port] || !se_tbl[port] || !se_tbl[port][se].valid)
        return;
    t = &tq[port][ch];
    t->se = (uint8_t)se;
    t->note = se_tbl[port][se].note;
    t->vol = (uint8_t)vol;
    t->pan = (uint8_t)pan;
    t->pitch = (uint16_t)pitch;
    memcpy(t->dir, dir_next[port][ch], sizeof(t->dir));
    /* a busy slot is cut first (TQ_CANCEL), then restarted */
    t->flags = (t->flags & TQ_ACTIVE ? TQ_CANCEL : 0) | TQ_ACTIVE | TQ_START | TQ_VOL | TQ_PAN | TQ_PITCH;
    tq_busy[port] |= 1 << ch;
}

static void tq_chg(int port, int se, int ch, int vol, int pan, int pitch)
{
    TqSlot *t;

    if (port >= SE_PORTS || ch > 15)
        return;
    t = &tq[port][ch];
    if (!(t->flags & TQ_ACTIVE) || t->se != se)
        return;
    if (vol >= 0) {
        t->vol = (uint8_t)vol;
        t->flags |= TQ_VOL;
    }
    if (pan >= 0) {
        t->pan = (uint8_t)pan;
        memcpy(t->dir, dir_next[port][ch], sizeof(t->dir));
        t->flags |= TQ_PAN;
    }
    if (pitch >= 0) {
        t->pitch = (uint16_t)pitch;
        t->flags |= TQ_PITCH;
    }
}

static void tq_cancel(int port, int ch)
{
    if (port < SE_PORTS && ch < 16 && (tq[port][ch].flags & TQ_ACTIVE))
        tq[port][ch].flags |= TQ_CANCEL;
}

static void tq_end(int port, int ch)
{
    hsyn_sound_off(SE_SYNTH(port), ch);
    tq[port][ch].flags = 0;
    tq_busy[port] &= ~(1 << ch);
}

static void tq_port_stop(int port)
{
    int c;

    for (c = 0; c < 16; c++)
        tq_end(port, c);
}

static void tq_tick(void)
{
    int p, c;

    for (p = 0; p < SE_PORTS; p++) {
        int sp = SE_SYNTH(p);

        for (c = 0; c < 16; c++) {
            TqSlot *t = &tq[p][c];

            if (!(tq_busy[p] & 1 << c))
                continue;
            if (t->flags & TQ_CANCEL) {
                hsyn_sound_off(sp, c);
                t->flags &= ~TQ_CANCEL;
                if (!(t->flags & TQ_START)) {
                    tq_end(p, c);
                    continue;
                }
            }
            if (t->flags & TQ_START) {
                hsyn_volume(sp, c, 127);
                hsyn_program(sp, c, t->se);
                hsyn_dir(sp, c, t->dir[3] != 0.0f ? t->dir : NULL);
                hsyn_note_on(sp, c, t->note, 127);
                t->flags &= ~TQ_START;
            }
            /* The slot is free again once its sound has played out. */
            if (!hsyn_channel_active(sp, c)) {
                tq_end(p, c);
                continue;
            }
            if (t->flags & TQ_VOL)
                hsyn_volume(sp, c, t->vol > 127 ? 127 : t->vol);
            if (t->flags & TQ_PAN) {
                hsyn_pan(sp, c, t->pan > 127 ? 127 : t->pan);
                hsyn_dir(sp, c, t->dir[3] != 0.0f ? t->dir : NULL);
            }
            if (t->flags & TQ_PITCH)
                hsyn_bend(sp, c, t->pitch > 16383 ? 16383 : t->pitch);
            t->flags &= ~(TQ_VOL | TQ_PAN | TQ_PITCH);
        }
    }
}

/* ---- Sequences ---------------------------------------------------------
 * A sequence request loads sequence bank `bank` (HD, BD, SQ) on synth port
 * `port` and plays one block of its SQ. status.midi_info keeps the port's
 * bit until the block ends or is stopped. */

typedef struct {
    int bank; /* sequence bank loaded on the port, -1 none */
    int on, paused;
} Bgm;

static Bgm bgm[HSEQ_PORTS];

static void bgm_stop(int port)
{
    hseq_stop(port);
    bgm[port].on = 0;
    bgm[port].paused = 0;
}

/* A sequence bank is about to change: nothing may keep pointing into it. */
static void bgm_detach(int bank)
{
    int p;

    for (p = 0; p < HSEQ_PORTS; p++) {
        if (bgm[p].bank != bank)
            continue;
        bgm_stop(p);
        hsyn_bank_ref(p, NULL, 0, NULL, 0);
        bgm[p].bank = -1;
    }
}

static void bgm_req(int port, int bank, int vol, int block)
{
    if (log_budget > 0) {
        log_budget--;
        printf("snd: sequence port %d bank %d block %d volume %d\n", port, bank, block, vol);
    }
    if (port >= HSEQ_PORTS || bank >= 4 || !midi_hd[bank].size || !midi_bd[bank].size)
        return;
    bgm_stop(port);
    if (bgm[port].bank != bank) {
        hsyn_bank_ref(port, midi_hd[bank].data, midi_hd[bank].size, midi_bd[bank].data,
                      midi_bd[bank].size);
        bgm[port].bank = bank;
    }
    hsyn_port_volume(port, vol);
    hsyn_port_pan(port, 64);
    hsyn_port_dir(port, bgm_dir_next[port][3] != 0.0f ? bgm_dir_next[port] : NULL);
    hsyn_port_bend(port, 0x2000);
    if (!hseq_start(port, sq[bank].data, sq[bank].size, block)) {
        printf("snd: sequence bank %d has no block %d\n", bank, block);
        return;
    }
    bgm[port].on = 1;
}

/* ---- Bank loading ------------------------------------------------------ */

static void log_bank(const char *kind, int n, const Blob *hd, uint32_t bd_size)
{
    if (!hsyn_hd_valid(hd->data, hd->size)) {
        printf("snd: %s bank %d: header of %u bytes is not a Sony HD bank (%02x %02x %02x %02x "
               "%02x %02x %02x %02x)\n",
               kind, n, hd->size, hd->size > 0 ? hd->data[0] : 0, hd->size > 1 ? hd->data[1] : 0,
               hd->size > 2 ? hd->data[2] : 0, hd->size > 3 ? hd->data[3] : 0,
               hd->size > 4 ? hd->data[4] : 0, hd->size > 5 ? hd->data[5] : 0,
               hd->size > 6 ? hd->data[6] : 0, hd->size > 7 ? hd->data[7] : 0);
    } else if (log_budget > 0) {
        log_budget--;
        int a[4];

        hsyn_hd_attrs(hd->data, hd->size, a);
        printf("snd: %s bank %d loaded (header %u bytes, samples %u bytes; %d sounds, reverb "
               "%d/%d, odd %d)\n",
               kind, n, hd->size, bd_size, a[0], a[1], a[2], a[3]);
    }
}

/* SdrBDDataSet2: the samples uploaded last belong to this effect bank. */
static void bank_bind(int synth_port, const Blob *hd)
{
    Blob *b;

    if (last_bd_slot < 0 || !hd->size)
        return;
    b = &bd[last_bd_slot];
    hsyn_bank_set(synth_port, blob_dup(hd), hd->size, b->data, b->size);
    b->data = NULL;
    b->size = 0;
    last_bd_slot = -1;
}

static int16_t hd_sum(const int8_t *p, uint32_t n)
{
    int16_t s = 0;
    uint32_t i;

    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

/* Length of one request, mirroring sending_req() in ps2_snddrv.c; 0 when
 * it cannot be known. */
static int cmd_len(const uint8_t *p)
{
    int cd = p[0];

    switch (cd & 0xf0) {
    case 0x00:
        return 4 + (cd & 1) + (cd >> 1 & 1) + (cd & 4 ? 2 : 0);
    case 0x10:
        return cd == 0x11 ? 3 : 1;
    case 0x20:
        if (cd >= 0x22 && cd <= 0x25)
            return 3;
        if (cd == 0x26)
            return 4;
        if (cd == 0x20)
            return 5;
        if (cd == 0x27 || cd == 0x28 || cd == 0x29 || cd == 0x2c || cd == 0x2d)
            return 8;
        return 2;
    case 0x40:
        if ((cd >= 0x47 && cd <= 0x4a) || cd == 0x41 || cd == 0x42)
            return 2;
        if (cd == 0x4b)
            return 3;
        if (cd == 0x45 || cd == 0x4c)
            return 4;
        if (cd == 0x44 || cd == 0x4f)
            return 6;
        if (cd == 0x4d || cd == 0x4e)
            return 3;
        return 1;
    case 0x50:
    case 0x60:
        return (cd >= 0x51 && cd <= 0x54) ? 8 : 2;
    }
    return 0;
}

static void do_cmd(const uint8_t *p, const PcIopView *iop)
{
    const uint8_t *buf = iop->ram + iop->data_buff;
    uint32_t room = iop->ram_size - iop->data_buff;
    int port = p[1];

    switch (p[0]) {
    case 0x28: /* SdrHDDataSet: sequence bank header */
    case 0x29: /* SdrHDDataSet2: effect bank header */
    {
        uint32_t size = le32(p + 4);
        int16_t sum;

        if (size > room)
            break;
        sum = hd_sum((const int8_t *)buf, size);
        if (p[0] == 0x28 && port < 4) {
            bgm_detach(port);
            blob_set(&midi_hd[port], buf, 0, size);
            midi_hd[port].size = size;
            status.midi_sum[port] = sum;
        } else if (p[0] == 0x29 && port < 5) {
            blob_set(&se_hd[port], buf, 0, size);
            se_hd[port].size = size;
            status.se_sum[port] = sum;
        }
        break;
    }
    case 0x2a: /* SdrBDDataSet: sequence bank complete */
        if (port < 4 && last_bd_slot >= 0) {
            bgm_detach(port);
            free(midi_bd[port].data);
            midi_bd[port] = bd[last_bd_slot];
            bd[last_bd_slot].data = NULL;
            bd[last_bd_slot].size = 0;
            last_bd_slot = -1;
            log_bank("sequence", port, &midi_hd[port], midi_bd[port].size);
        }
        break;
    case 0x2b: /* SdrBDDataSet2: effect bank complete */
        if (port < 5) {
            uint32_t size = last_bd_slot >= 0 ? bd[last_bd_slot].size : 0;

            tq_port_stop(port);
            bank_bind(SE_SYNTH(port), &se_hd[port]);
            se_build(port);
            log_bank("effect", port, &se_hd[port], size);
        }
        break;
    case 0x2c: /* SdrBDDataTrans: a piece of a bank's samples */
    {
        int half = p[1] >> 7;
        uint32_t adrs = p[2] | p[3] << 8 | p[4] << 16;
        uint32_t size = p[5] | p[6] << 8 | p[7] << 16;
        uint32_t src = half ? BD_HALF : 0;

        port = p[1] & 0x7f;
        if (adrs == 0)
            bd[port].size = 0; /* a new bank starts over */
        if (size <= BD_PIECE && src + size <= room)
            blob_set(&bd[port], buf + src, adrs, size);
        last_bd_slot = port;
        break;
    }
    case 0x2d: /* SdrSQDataSet */
    {
        uint32_t size = le32(p + 4);

        if (port < 8 && size <= room) {
            blob_set(&sq[port], buf, 0, size);
            sq[port].size = size;
        }
        break;
    }
    case 0x10: /* SdrSeAllStop */
    {
        int i;

        for (i = 0; i < SE_PORTS; i++)
            tq_port_stop(i);
        break;
    }
    case 0x11: /* effects master volume */
    {
        int i;

        for (i = 0; i < SE_PORTS; i++)
            hsyn_port_master(SE_SYNTH(i), p[1] << 8 | p[2]);
        break;
    }
    case 0x20: /* SdrBgmReq: port bank vol block */
        bgm_req(port, p[2], p[3], p[4]);
        break;
    case 0x21: /* SdrBgmStop */
        if (port < HSEQ_PORTS)
            bgm_stop(port);
        break;
    case 0x22: /* sequence volume */
        if (port < HSEQ_PORTS && bgm[port].on)
            hsyn_port_volume(port, p[2]);
        break;
    case 0x23: /* sequences master volume */
    {
        int i;

        for (i = 0; i < HSEQ_PORTS; i++)
            hsyn_port_master(i, p[1] << 8 | p[2]);
        break;
    }
    case 0x24: /* sequence pause: 1 pause, 2 resume, 0 toggle */
        if (port < HSEQ_PORTS && bgm[port].on) {
            int pause = p[2] == 0 ? !bgm[port].paused : p[2] == 1;

            bgm[port].paused = pause;
            hseq_pause(port, pause);
        }
        break;
    case 0x25: /* sequence pan */
        if (port < HSEQ_PORTS && bgm[port].on)
            hsyn_port_pan(port, p[2]);
        break;
    case 0x26: /* sequence pitch: low, high, 7 bits each */
        if (port < HSEQ_PORTS && bgm[port].on)
            hsyn_port_bend(port, p[3] << 7 | p[2]);
        break;
    case 0x27: /* SdrBgmChg: port - - vol pan low high */
        if (port < HSEQ_PORTS && bgm[port].on) {
            hsyn_port_volume(port, p[4]);
            hsyn_port_pan(port, p[5]);
            hsyn_port_dir(port, bgm_dir_next[port][3] != 0.0f ? bgm_dir_next[port] : NULL);
            hsyn_port_bend(port, p[7] << 7 | p[6]);
        }
        break;
    case 0x44: /* SdrSetRev: (core + 1) << 6 | mode, depth (big-endian), delay, feedback */
        spu2rev_set((p[1] >> 6) - 1, p[1] & 0x3f, p[2] << 8 | p[3]);
        break;
    case 0x4b: /* SdrMasterVol */
        hsyn_master_volume((p[1] << 8 | p[2]) >> 1);
        break;
    default:
        if (p[0] < 0x10) {
            /* effect play/change/stop: cd bank sound slot [vol] [pan] [pitch, big-endian] */
            int cd = p[0], se = p[2], ch = p[3] & 0x7f, i = 4;
            int vol = -1, pan = -1, pitch = -1;

            port = p[1] & 0x7f;
            if (cd & 1)
                vol = p[i++];
            if (cd & 2)
                pan = p[i++];
            if (cd & 4) {
                pitch = p[i] << 8 | p[i + 1];
                i += 2;
            }
            if (cd == 8)
                tq_cancel(port, ch);
            else if (cd & 8)
                tq_chg(port, se, ch, vol, pan, pitch);
            else
                tq_req(port, se, ch, vol < 0 ? 127 : vol, pan < 0 ? 64 : pan, pitch < 0 ? 8192 : pitch);
        }
        /* the rest: not done yet */
        break;
    }
}

void pc_snddrv_dir(int midi, int port, int ch, const float *dir)
{
    float *d;

    if (midi) {
        if (port < 0 || port >= HSEQ_PORTS)
            return;
        d = bgm_dir_next[port];
    } else {
        if (port < 0 || port >= SE_PORTS)
            return;
        d = dir_next[port][ch & 15];
    }
    if (dir == NULL) {
        d[3] = 0.0f;
        return;
    }
    d[0] = dir[0];
    d[1] = dir[1];
    d[2] = dir[2];
    d[3] = 1.0f;
}

void pc_snddrv_requests(const unsigned char *buf, int size, const PcIopView *iop)
{
    const uint8_t *p = buf, *end = buf + size;

    pc_audio_lock();
    while (p < end && *p != 0xff) {
        int n = cmd_len(p);

        if (n == 0 || p + n > end) {
            printf("snd: unknown request 0x%02x\n", *p);
            break;
        }
        do_cmd(p, iop);
        p += n;
    }
    pc_audio_unlock();
}

/* ---- Audio thread ------------------------------------------------------ */

/* The driver's 240 Hz tick. */
static void tick(void)
{
    uint16_t midi = 0;
    int p;

    tq_tick();
    hseq_tick();
    for (p = 0; p < HSEQ_PORTS; p++) {
        if (bgm[p].on && !bgm[p].paused && !hseq_playing(p))
            bgm[p].on = 0;
        if (bgm[p].on)
            midi |= 1 << p;
    }
    status.midi_info = midi;
    for (p = 0; p < SE_PORTS; p++)
        status.se_info[p] = tq_busy[p];
}

static void mix(float *lr, int frames)
{
    static int tick_pos;
    int32_t buf[512 * 2], fx[SPU2REV_CORES * 512 * 2];
    int done = 0, i, c;

    while (done < frames) {
        int n = HSYN_RATE / 240 - tick_pos;

        if (n > frames - done)
            n = frames - done;
        if (n > 512)
            n = 512;
        memset(buf, 0, (size_t)n * 2 * sizeof(int32_t));
        memset(fx, 0, (size_t)SPU2REV_CORES * n * 2 * sizeof(int32_t));
        hsyn_mix(buf, fx, n);
        for (c = 0; c < SPU2REV_CORES; c++)
            spu2rev_mix(c, fx + c * n * 2, buf, n);
        for (i = 0; i < n * 2; i++)
            lr[done * 2 + i] += (float)buf[i];
        done += n;
        tick_pos += n;
        if (tick_pos == HSYN_RATE / 240) {
            tick_pos = 0;
            tick();
        }
    }
}

void pc_snddrv_init(void)
{
    static int done;
    int p;

    if (done)
        return;
    done = 1;
    for (p = 0; p < HSEQ_PORTS; p++)
        bgm[p].bank = -1;
    pc_sound_add_mixer(mix);
}

void pc_snddrv_status(void *dst)
{
    pc_audio_lock();
    memcpy(dst, &status, sizeof(status));
    pc_audio_unlock();
}
