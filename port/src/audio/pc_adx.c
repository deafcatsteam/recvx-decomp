/*
 * PC replacement for the CRI ADX middleware (libadxe).
 *
 * ADXF is CRI's file layer: the game loads all of its data through it, from
 * AFS archives ("partitions") and plain files on the disc. It is implemented
 * here on top of the disc image reader, synchronously.
 *
 * ADXT is the ADX audio stream player: the music and the speech. The
 * streams are decoded here and played by the PC sound engine.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cri_adxf.h"
#include "cri_adxt.h"

#include "../host/pc_sound.h"
#include "../platform/pc_disc.h"

#define PC_ADXF_MAX_PT 256
#define PC_ADXF_MAX_HANDLES 64

typedef struct {
    unsigned int offset; /* bytes from the start of the archive */
    unsigned int size;
} PcAfsEntry;

typedef struct {
    int loaded;
    unsigned int lsn; /* first sector of the archive */
    unsigned int count;
    PcAfsEntry *entries;
} PcAfsPartition;

typedef struct {
    int used;
    uint64_t offset; /* bytes from the start of the disc */
    unsigned int size;
    Sint32 stat;
} PcAdxf;

static PcAfsPartition partitions[PC_ADXF_MAX_PT];
static PcAdxf handles[PC_ADXF_MAX_HANDLES];

static unsigned int le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* ---- ADXF: files and AFS partitions ---------------------------------- */

Sint32 ADXF_LoadPartitionNw(Sint32 ptid, Char8 *fname, void *dir, void *ptinfo)
{
    PcAfsPartition *pt;
    unsigned char head[8];
    unsigned char *table;
    unsigned int lsn, size, i;

    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT)
        return -1;
    pt = &partitions[ptid];
    free(pt->entries);
    memset(pt, 0, sizeof(*pt));

    if (!pc_disc_find(fname, &lsn, &size)) {
        fprintf(stderr, "ADXF: partition %d: '%s' not found on the disc\n", (int)ptid, fname);
        return -1;
    }
    /* CRI only checks the first three bytes: the fourth is not always 0. */
    if (!pc_disc_read_bytes((uint64_t)lsn * PC_DISC_SECTOR, 8, head)
        || memcmp(head, "AFS", 3) != 0) {
        fprintf(stderr, "ADXF: '%s' is not an AFS archive (starts with %02x %02x %02x %02x)\n",
                fname, head[0], head[1], head[2], head[3]);
        return -1;
    }

    pt->lsn = lsn;
    pt->count = le32(head + 4);
    pt->entries = calloc(pt->count ? pt->count : 1, sizeof(PcAfsEntry));
    table = malloc(pt->count * 8 + 1);
    if (!pc_disc_read_bytes((uint64_t)lsn * PC_DISC_SECTOR + 8, pt->count * 8, table)) {
        free(table);
        return -1;
    }
    for (i = 0; i < pt->count; i++) {
        pt->entries[i].offset = le32(table + i * 8);
        pt->entries[i].size = le32(table + i * 8 + 4);
    }
    free(table);
    pt->loaded = 1;
    return 0;
}

Sint32 ADXF_GetPtStat(Sint32 ptid)
{
    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT || !partitions[ptid].loaded)
        return ADXF_STAT_ERROR;
    return ADXF_STAT_READEND;
}

static ADXF new_handle(uint64_t offset, unsigned int size)
{
    int i;

    for (i = 0; i < PC_ADXF_MAX_HANDLES; i++) {
        if (!handles[i].used) {
            handles[i].used = 1;
            handles[i].offset = offset;
            handles[i].size = size;
            handles[i].stat = ADXF_STAT_STOP;
            return (ADXF)&handles[i];
        }
    }
    fprintf(stderr, "ADXF: out of file handles\n");
    return NULL;
}

ADXF ADXF_OpenAfs(Sint32 ptid, Sint32 flid)
{
    PcAfsPartition *pt;

    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT || !partitions[ptid].loaded)
        return NULL;
    pt = &partitions[ptid];
    if (flid < 0 || (unsigned int)flid >= pt->count)
        return NULL;
    return new_handle((uint64_t)pt->lsn * PC_DISC_SECTOR + pt->entries[flid].offset,
                      pt->entries[flid].size);
}

ADXF ADXF_Open(Char8 *fname, void *atr)
{
    unsigned int lsn, size;

    if (!pc_disc_find(fname, &lsn, &size)) {
        fprintf(stderr, "ADXF: '%s' not found on the disc\n", fname);
        return NULL;
    }
    return new_handle((uint64_t)lsn * PC_DISC_SECTOR, size);
}

void ADXF_Close(ADXF adxf)
{
    if (adxf != NULL)
        ((PcAdxf *)adxf)->used = 0;
}

Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void *buf)
{
    PcAdxf *f = (PcAdxf *)adxf;
    unsigned int want;

    if (f == NULL)
        return 0;
    want = (unsigned int)nsct * PC_DISC_SECTOR;
    if (want > f->size)
        want = f->size;
    f->stat = pc_disc_read_bytes(f->offset, want, buf) ? ADXF_STAT_READEND : ADXF_STAT_ERROR;
    return nsct;
}

Sint32 ADXF_Stop(ADXF adxf)
{
    if (adxf != NULL)
        ((PcAdxf *)adxf)->stat = ADXF_STAT_STOP;
    return 0;
}

Sint32 ADXF_GetStat(ADXF adxf)
{
    return adxf != NULL ? ((PcAdxf *)adxf)->stat : ADXF_STAT_ERROR;
}

Sint32 ADXF_GetFsizeByte(ADXF adxf)
{
    return adxf != NULL ? (Sint32)((PcAdxf *)adxf)->size : 0;
}

Sint32 ADXF_GetFsizeSct(ADXF adxf)
{
    return adxf != NULL ? (Sint32)((((PcAdxf *)adxf)->size + PC_DISC_SECTOR - 1) / PC_DISC_SECTOR) : 0;
}

/* ---- ADXT: audio streams ---------------------------------------------- */

/*
 * The game plays its music (slot 0) and speech (slot 1) as ADX streams from
 * AFS archives. Each playing stream decodes ahead into a voice of the sound
 * engine (port/src/host/pc_sound.c); pc_adxt_update(), called every V-blank,
 * keeps about a second decoded and notices when the voice has ended.
 *
 * ADX: a big-endian header, then frames of 18 bytes per channel (a 16-bit
 * scale and 32 4-bit samples) predicted from the two previous samples with
 * coefficients derived from the header's high-pass frequency. Looping files
 * jump from the loop end back to the loop start, keeping the history.
 */

#define ADX_FRAME_SAMPLES 32
#define ADX_CACHE 0x8000

typedef struct {
    char used; /* read by the game as ADX_TALK.used */
    Sint32 stat;
    int voice;
    int vol; /* ADXT_SetOutVol, in 0.1 dB */

    uint64_t base; /* disc offset of the file */
    unsigned int size;
    unsigned int data; /* offset of the first frame */
    int channels, rate, total;
    int loop, loop_start, loop_end;
    int coef1, coef2;
    int hist[2][2];
    int pos; /* next sample to decode */
    int decoded_all;
    Sint32 played; /* for ADXT_GetTime once the voice is gone */

    unsigned char cache[ADX_CACHE];
    unsigned int cache_off, cache_len;
} PcAdxt;

static PcAdxt players[8];
static int output_mono;

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/* Bytes of the file at off, through a small cache; NULL past its end. */
static const unsigned char *adx_bytes(PcAdxt *t, unsigned int off, unsigned int len)
{
    if (off + len > t->size)
        return NULL;
    if (off < t->cache_off || off + len > t->cache_off + t->cache_len) {
        unsigned int n = t->size - off < ADX_CACHE ? t->size - off : ADX_CACHE;

        if (!pc_disc_read_bytes(t->base + off, n, t->cache))
            return NULL;
        t->cache_off = off;
        t->cache_len = n;
    }
    return t->cache + (off - t->cache_off);
}

static int adx_header(PcAdxt *t)
{
    const unsigned char *h = adx_bytes(t, 0, t->size < 0x800 ? t->size : 0x800);
    unsigned int hsize;
    int version, cutoff, enc;
    double a, b, c, z;

    if (h == NULL || t->size < 0x20 || be16(h) != 0x8000)
        return 0;
    hsize = be16(h + 2) + 4;
    enc = h[4];
    t->channels = h[7];
    t->rate = (int)be32(h + 8);
    t->total = (int)be32(h + 12);
    cutoff = (int)be16(h + 16);
    version = h[18];
    if (enc != 3 || h[5] != 18 || h[6] != 4 || t->channels < 1 || t->channels > 2 ||
        t->rate <= 0 || hsize > 0x800) {
        printf("ADX: unsupported stream (encoding %d, %d bytes per frame, %d bits, %d channels)\n",
               enc, h[5], h[6], t->channels);
        return 0;
    }
    if (h[19] != 0)
        printf("ADX: stream flags %02x (encrypted streams are not supported)\n", h[19]);
    t->data = hsize;

    /* Loop points: after the base header (version 3), or after the sample
     * history too (version 4). */
    t->loop = 0;
    {
        unsigned int at = version >= 4 ? 0x24 : 0x18;

        if (at + 0x10 <= hsize && be32(h + at) != 0) {
            t->loop_start = (int)be32(h + at + 4);
            t->loop_end = (int)be32(h + at + 12);
            t->loop = t->loop_end > t->loop_start && t->loop_end <= t->total;
        }
    }

    z = cos(2.0 * M_PI * cutoff / t->rate);
    a = M_SQRT2 - z;
    b = M_SQRT2 - 1.0;
    c = (a - sqrt((a + b) * (a - b))) / b;
    t->coef1 = (int)floor(c * 8192.0);
    t->coef2 = (int)floor(c * c * -4096.0);
    return 1;
}

static int clamp16(int v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}

/* Decodes one frame of every channel into out (interleaved); 0 at the end
 * of the data. */
static int adx_frame(PcAdxt *t, int frame, short *out)
{
    unsigned int fb = 18 * t->channels;
    const unsigned char *p = adx_bytes(t, t->data + (unsigned int)frame * fb, fb);
    int ch, i;

    if (p == NULL)
        return 0;
    for (ch = 0; ch < t->channels; ch++, p += 18) {
        int scale = (short)be16(p) + 1;
        int h1 = t->hist[ch][0], h2 = t->hist[ch][1];

        for (i = 0; i < ADX_FRAME_SAMPLES; i++) {
            int nib = p[2 + i / 2];
            int s;

            nib = (i & 1) ? (nib & 15) : (nib >> 4);
            if (nib >= 8)
                nib -= 16;
            s = clamp16(nib * scale + ((t->coef1 * h1) >> 12) + ((t->coef2 * h2) >> 12));
            out[i * t->channels + ch] = (short)s;
            h2 = h1;
            h1 = s;
        }
        t->hist[ch][0] = h1;
        t->hist[ch][1] = h2;
    }
    return 1;
}

/* Decodes into the voice while it has room. */
static void adx_fill(PcAdxt *t)
{
    short pcm[ADX_FRAME_SAMPLES * 2];

    while (!t->decoded_all && pc_sound_space(t->voice) >= ADX_FRAME_SAMPLES) {
        int end = t->loop ? t->loop_end : t->total;
        int first = t->pos % ADX_FRAME_SAMPLES;
        int n = ADX_FRAME_SAMPLES - first;

        if (t->pos >= end || !adx_frame(t, t->pos / ADX_FRAME_SAMPLES, pcm)) {
            t->decoded_all = 1;
            pc_sound_end(t->voice);
            break;
        }
        if (t->pos + n > end)
            n = end - t->pos;
        pc_sound_write(t->voice, pcm + first * t->channels, n);
        t->pos += n;
        if (t->loop && t->pos >= t->loop_end)
            t->pos = t->loop_start;
    }
}

static void adx_apply_volume(PcAdxt *t)
{
    pc_sound_set_gain(t->voice, t->vol <= -999 ? 0.0f : (float)pow(10.0, t->vol / 200.0));
}

static void adx_release(PcAdxt *t)
{
    if (t->voice >= 0) {
        t->played = (Sint32)pc_sound_played(t->voice);
        pc_sound_close(t->voice);
        t->voice = -1;
    }
}

/* Every V-blank: decode ahead, and end the streams that finished. */
static void pc_adxt_update(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        PcAdxt *t = &players[i];

        if (!t->used || t->stat != ADXT_STAT_PLAYING || t->voice < 0)
            continue;
        adx_fill(t);
        if (pc_sound_finished(t->voice)) {
            adx_release(t);
            t->stat = ADXT_STAT_PLAYEND;
        }
    }
}

void ADXT_SetupHostFs(void *sprm) { (void)sprm; }
void ADXT_SetupDvdFs(void *sprm) { (void)sprm; pc_disc_open(); }
void ADXPS2_SetupThrd(void *tprm) { (void)tprm; }
void ADXPS2_Lock(void) {}
void ADXPS2_Unlock(void) {}

void ADXT_Init(void)
{
    int i;

    memset(players, 0, sizeof(players));
    for (i = 0; i < 8; i++)
        players[i].voice = -1;
    pc_sound_add_updater(pc_adxt_update);
}

void ADXT_Finish(void)
{
    int i;

    for (i = 0; i < 8; i++)
        adx_release(&players[i]);
}

void ADXT_SetNumRetry(Sint32 num) { (void)num; }

/* The game's sound options: mono output for everything. */
void ADXT_SetOutputMono(Sint32 flag)
{
    output_mono = flag != 0;
    pc_sound_set_mono(output_mono);
}

ADXT ADXT_Create(Sint32 maxnch, void *work, Sint32 worksize)
{
    int i;

    for (i = 0; i < 8; i++) {
        if (!players[i].used) {
            memset(&players[i], 0, sizeof(players[i]));
            players[i].used = 1;
            players[i].voice = -1;
            players[i].stat = ADXT_STAT_STOP;
            return (ADXT)&players[i];
        }
    }
    return NULL;
}

void ADXT_Destroy(ADXT adxt)
{
    PcAdxt *t = (PcAdxt *)adxt;

    if (t != NULL) {
        adx_release(t);
        t->used = 0;
    }
}

void ADXT_StartAfs(ADXT adxt, Sint32 patid, Sint32 fid)
{
    PcAdxt *t = (PcAdxt *)adxt;
    PcAfsPartition *pt;

    if (t == NULL)
        return;
    adx_release(t);
    t->played = 0;
    t->pos = 0;
    t->decoded_all = 0;
    t->cache_len = 0;
    memset(t->hist, 0, sizeof(t->hist));
    /* Anything that cannot be played ends at once, so the game never waits
     * for it. */
    t->stat = ADXT_STAT_PLAYEND;

    if (patid < 0 || patid >= PC_ADXF_MAX_PT || !partitions[patid].loaded)
        return;
    pt = &partitions[patid];
    if (fid < 0 || (unsigned int)fid >= pt->count)
        return;
    t->base = (uint64_t)pt->lsn * PC_DISC_SECTOR + pt->entries[fid].offset;
    t->size = pt->entries[fid].size;
    if (!adx_header(t)) {
        printf("ADX: partition %d file %d is not a playable ADX stream\n", (int)patid, (int)fid);
        return;
    }
    t->voice = pc_sound_open_stream(t->channels, t->rate, t->rate * 2);
    if (t->voice < 0)
        return;
    adx_apply_volume(t);
    t->stat = ADXT_STAT_PLAYING;
    adx_fill(t);
}

void ADXT_Stop(ADXT adxt)
{
    PcAdxt *t = (PcAdxt *)adxt;

    if (t != NULL) {
        adx_release(t);
        t->stat = ADXT_STAT_STOP;
    }
}

Sint32 ADXT_GetStat(ADXT adxt)
{
    return adxt != NULL ? ((PcAdxt *)adxt)->stat : ADXT_STAT_STOP;
}

void ADXT_GetTime(ADXT adxt, Sint32 *ncount, Sint32 *tscale)
{
    PcAdxt *t = (PcAdxt *)adxt;

    *ncount = 0;
    *tscale = 44100;
    if (t == NULL)
        return;
    if (t->rate > 0)
        *tscale = t->rate;
    *ncount = t->voice >= 0 ? (Sint32)pc_sound_played(t->voice) : t->played;
}

void ADXT_SetOutVol(ADXT adxt, Sint32 vol)
{
    PcAdxt *t = (PcAdxt *)adxt;

    if (t != NULL) {
        t->vol = vol;
        if (t->voice >= 0)
            adx_apply_volume(t);
    }
}

void ADXT_SetAutoRcvr(ADXT adxt, Sint32 rmode) {}
void ADXT_SetReloadSct(ADXT adxt, Sint32 minsct) {}
void ADXT_SetSvrFreq(ADXT adxt, Sint32 freq) {}
