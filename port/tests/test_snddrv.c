/*
 * The PC sound driver (pc_snddrv.c) with a bank and a sequence made here
 * (a 441 Hz sine encoded as PS-ADPCM in a Sony HD/BD bank, a one-note SQ
 * sequence; no game data): the requests are those ps2_snddrv.c builds, and
 * the checks are on the mixed output and on the status block the game reads.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/audio/pc_snddrv.h"
#include "../src/host/pc_host.h"
#include "../src/host/pc_sound.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* ---- Test data ---------------------------------------------------------- */

static unsigned char ram[0x200000];
#define DATA_BUFF 0x160000

static void w16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void w32(unsigned char *p, unsigned v) { w16(p, v); w16(p + 2, v >> 16); }

/* PS-ADPCM: for each block of 28 samples, the filter and shift that decode
 * closest to the input. flags: 1 end, 2 repeat, 4 loop start. */
static const int filt[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static int adpcm(const short *x, int n, unsigned char *out, int loop)
{
    int blocks = (n + 27) / 28, b, h1 = 0, h2 = 0;

    for (b = 0; b < blocks; b++) {
        int best_err = -1, best_f = 0, best_s = 0, f, s, i;
        unsigned char *o = out + b * 16;

        for (f = 0; f < 5; f++)
            for (s = 0; s <= 12; s++) {
                int a1 = h1, a2 = h2, err = 0;

                for (i = 0; i < 28; i++) {
                    int in = b * 28 + i < n ? x[b * 28 + i] : 0;
                    int pred = (a1 * filt[f][0] + a2 * filt[f][1] + 32) >> 6;
                    int q = (int)lround((double)(in - pred) * (1 << s) / 4096.0);
                    int d;

                    q = q < -8 ? -8 : (q > 7 ? 7 : q);
                    d = ((short)(q << 12) >> s) + pred;
                    d = d < -32768 ? -32768 : (d > 32767 ? 32767 : d);
                    err += abs(d - in);
                    a2 = a1;
                    a1 = d;
                }
                if (best_err < 0 || err < best_err) {
                    best_err = err;
                    best_f = f;
                    best_s = s;
                }
            }
        memset(o, 0, 16);
        o[0] = (unsigned char)(best_f << 4 | best_s);
        for (i = 0; i < 28; i++) {
            int in = b * 28 + i < n ? x[b * 28 + i] : 0;
            int pred = (h1 * filt[best_f][0] + h2 * filt[best_f][1] + 32) >> 6;
            int q = (int)lround((double)(in - pred) * (1 << best_s) / 4096.0);
            int d;

            q = q < -8 ? -8 : (q > 7 ? 7 : q);
            d = ((short)(q << 12) >> best_s) + pred;
            d = d < -32768 ? -32768 : (d > 32767 ? 32767 : d);
            o[2 + i / 2] |= (q & 15) << ((i & 1) * 4);
            h2 = h1;
            h1 = d;
        }
        if (b == 0 && loop)
            o[1] |= 4;
        if (b == blocks - 1)
            o[1] |= loop ? 3 : 1;
    }
    return blocks * 16;
}

/* A Sony HD bank with one program, one split, one sample: the BD sample at
 * offset 0, 22050 Hz, base note 60, full volume, centre. */
static int make_hd(unsigned char *hd)
{
    unsigned char *c;
    int prog = 0x50, sset = 0xa0, smpl = 0xd0, vagi = 0x110, size = 0x140;

    memset(hd, 0, size);
    memcpy(hd, "IECSsreV", 8);
    w32(hd + 8, 0x10);
    memcpy(hd + 0x10, "IECSdaeH", 8);
    w32(hd + 0x18, 0x40);
    w32(hd + 0x1c, size);
    memset(hd + 0x34, 0xff, 0x1c);
    w32(hd + 0x24, prog);
    w32(hd + 0x28, sset);
    w32(hd + 0x2c, smpl);
    w32(hd + 0x30, vagi);

    c = hd + prog; /* programs: one, at +0x20 */
    memcpy(c, "IECSgorP", 8);
    w32(c + 12, 0);
    w32(c + 16, 0x20);
    c += 0x20;
    w32(c, 0x10); /* split at +0x10 */
    c[4] = 1;
    c[5] = 20;
    c[6] = 127;
    c[7] = 64;
    c += 0x10;
    w16(c, 0);   /* sample set 0 */
    c[2] = 0;    /* keys 0-127 */
    c[4] = 127;
    w16(c + 6, 2 * 128);
    c[16] = 127;
    c[17] = 64;

    c = hd + sset;
    memcpy(c, "IECStesS", 8);
    w32(c + 16, 0x14);
    c += 0x14;
    c[2] = 127;
    c[3] = 1; /* one sample: index 0 */

    c = hd + smpl;
    memcpy(c, "IECSlpmS", 8);
    w32(c + 16, 0x14);
    c += 0x14;
    c[4] = 127;         /* velocity 0-127 */
    c[11] = 60;         /* base note */
    c[13] = 64;
    c[16] = 127;
    w16(c + 18, 0x80ff); /* instant attack, full sustain level */
    w16(c + 20, 0x1fcb); /* held sustain, release shift 11 */

    c = hd + vagi;
    memcpy(c, "IECSigaV", 8);
    w32(c + 16, 0x14);
    c += 0x14;
    w32(c, 0);
    w16(c + 4, 22050);
    return size;
}

/* An SQ sequence: program 0, note 60 for 240 ticks (0.25 s), end. */
static int make_sq(unsigned char *sq)
{
    static const unsigned char events[] = {
        0x00, 0xc0, 0x00,             /* program 0 */
        0x00, 0x90, 60, 100,          /* note on */
        0x81, 0x70, 0x80, 60,         /* 240 ticks later: note off */
        0x00, 0xff, 0x2f, 0x00,       /* end */
    };
    unsigned char *c;

    memset(sq, 0, 0x80);
    memcpy(sq, "IECSsreV", 8);
    w32(sq + 8, 0x10);
    memcpy(sq + 0x10, "IECSuqeS", 8);
    w32(sq + 0x10 + 0x14, 0x30); /* Midi chunk */
    c = sq + 0x30;
    memcpy(c, "IECSidiM", 8);
    w32(c + 12, 0);
    w32(c + 16, 0x14); /* block 0 */
    c += 0x14;
    w32(c, 6); /* events at +6 */
    w16(c + 4, 480);
    memcpy(c + 6, events, sizeof(events));
    return 0x30 + 0x14 + 6 + (int)sizeof(events);
}

/* ---- Requests, as ps2_snddrv.c builds them ----------------------------- */

static PcIopView view = { ram, sizeof(ram), DATA_BUFF, 0, DATA_BUFF };

static void send(const unsigned char *req, int n)
{
    unsigned char buf[64];

    memcpy(buf, req, n);
    buf[n] = 0xff;
    pc_snddrv_requests(buf, n + 1, &view);
}

static void header(int cd, int n, unsigned size)
{
    unsigned char r[8] = { (unsigned char)cd, (unsigned char)n, 0, 0 };

    w32(r + 4, size);
    send(r, 8);
}

/* SdrBDDataTrans: the samples, already in the first half of the buffer. */
static void samples(int bank, unsigned size)
{
    unsigned char r[8] = { 0x2c, (unsigned char)bank, 0, 0, 0 };

    r[5] = size;
    r[6] = size >> 8;
    r[7] = size >> 16;
    send(r, 8);
}

static short status_word(int offset)
{
    unsigned char st[0x42];
    short v;

    pc_snddrv_status(st);
    memcpy(&v, st + offset, 2);
    return v;
}

/* ---- Output ------------------------------------------------------------- */

static short out[PC_AUDIO_RATE * 2];

static void pull(int frames)
{
    pc_audio_pull(out, frames);
}

/* Frequency over frames 400..n of the last pull: sounds start on the
 * driver's next 240 Hz tick, up to 200 frames in. */
static double freq(int n)
{
    int i, c = 0;

    for (i = 401; i < n; i++)
        if (out[(i - 1) * 2] < 0 && out[i * 2] >= 0)
            c++;
    return c * (double)PC_AUDIO_RATE / (n - 400);
}

static int peak_from(int from, int n, int ch)
{
    int i, p = 0;

    for (i = from; i < n; i++)
        if (abs(out[i * 2 + ch]) > p)
            p = abs(out[i * 2 + ch]);
    return p;
}

static int peak(int n, int ch)
{
    int i, p = 0;

    for (i = 0; i < n; i++)
        if (abs(out[i * 2 + ch]) > p)
            p = abs(out[i * 2 + ch]);
    return p;
}

int main(void)
{
    static short sine[22050];
    static unsigned char hd[0x200], sq[0x100];
    int i, hd_size, bd_size, sq_size, n;
    short sum = 0;

    pc_audio_open = 1;
    pc_snddrv_init();
    for (i = 0; i < 22050; i++)
        sine[i] = (short)lround(12000 * sin(2 * M_PI * 441 * i / 22050.0));

    /* Effect bank 0: a one-shot of 0.2 s. */
    hd_size = make_hd(hd);
    for (i = 0; i < hd_size; i++)
        sum += (signed char)hd[i];
    memcpy(ram + DATA_BUFF, hd, hd_size);
    view.last_dma_size = hd_size;
    header(0x29, 0, hd_size);
    CHECK(status_word(0x26) == sum); /* se_sum[0]: the game compares it */
    bd_size = adpcm(sine, 4410, ram + DATA_BUFF, 0);
    samples(0, bd_size); /* driver bank 0 = the game's effect bank 0 */
    send((const unsigned char[]){ 0x2b, 0 }, 2);

    /* Play sound 0 in slot 3 at full volume, panned left. */
    send((const unsigned char[]){ 0x03, 0, 0, 3, 127, 0 }, 6);
    pull(4800);
    n = 4800;
    printf("effect: %.1f Hz, left %d, right %d, status %04x\n", freq(n), peak(n, 0), peak(n, 1),
           (unsigned short)status_word(0));
    CHECK(fabs(freq(n) - 441) < 15);
    CHECK(peak(n, 0) > 8000);
    CHECK(peak(n, 1) < peak(n, 0) / 4);
    CHECK(status_word(0) == 1 << 3); /* se_info[0]: slot 3 busy */
    pull(9600);                      /* past its end (0.2 s from frame 200) */
    CHECK(status_word(0) == 0);
    CHECK(peak_from(5100, 9600, 0) == 0);

    /* A looping effect plays until stopped. */
    bd_size = adpcm(sine, 2250, ram + DATA_BUFF, 1); /* 45 whole cycles */
    memcpy(ram + DATA_BUFF + 0x10000, hd, hd_size);  /* keep the header */
    samples(0, bd_size);
    send((const unsigned char[]){ 0x2b, 0 }, 2);
    send((const unsigned char[]){ 0x00, 0, 0, 5 }, 4);
    pull(24000);
    CHECK(status_word(0) == 1 << 5);
    CHECK(peak(4800, 0) > 8000 && fabs(freq(24000) - 441) < 10);
    send((const unsigned char[]){ 0x08, 0, 0, 5 }, 4); /* SdrSeCancel */
    pull(480);
    CHECK(status_word(0) == 0);
    pull(480);
    CHECK(peak(480, 0) == 0);

    /* A sound the bank does not have is reported finished at once. */
    send((const unsigned char[]){ 0x00, 0, 9, 1 }, 4);
    pull(480);
    CHECK(status_word(0) == 0);

    /* Sequence bank 1 (driver bank 1) on port 0: one note of 0.25 s. */
    memcpy(ram + DATA_BUFF, hd, hd_size);
    view.last_dma_size = hd_size;
    header(0x28, 1, hd_size);
    bd_size = adpcm(sine, 2250, ram + DATA_BUFF, 1);
    samples(1, bd_size);
    send((const unsigned char[]){ 0x2a, 1 }, 2);
    sq_size = make_sq(sq);
    memcpy(ram + DATA_BUFF, sq, sq_size);
    header(0x2d, 1, sq_size);
    send((const unsigned char[]){ 0x20, 0, 1, 127, 0 }, 5); /* SdrBgmReq port 0 bank 1 */
    pull(4800);
    printf("sequence: %.1f Hz, peak %d/%d, playing %04x\n", freq(4800), peak(4800, 0),
           peak(4800, 1), (unsigned short)status_word(0x0c));
    CHECK(status_word(0x0c) == 1); /* midi_info: port 0 */
    CHECK(fabs(freq(4800) - 441) < 15);
    CHECK(peak(4800, 0) > 5000 && abs(peak(4800, 0) - peak(4800, 1)) < 200);
    pull(19200); /* 0.5 s in: ended and released */
    pull(4800);
    CHECK(status_word(0x0c) == 0);
    CHECK(peak(4800, 0) == 0);

    /* Room reverb (SdrSetRev, the hall the game uses) on SPU2 core 1, for
     * a sound its bank sends there: it rings on after the sound, then dies
     * away. */
    hd[0xe4 + 41] = 0x2f; /* direct and effect sends, core 1 */
    memcpy(ram + DATA_BUFF, hd, hd_size);
    view.last_dma_size = hd_size;
    header(0x29, 0, hd_size);
    bd_size = adpcm(sine, 4410, ram + DATA_BUFF, 0);
    samples(0, bd_size);
    send((const unsigned char[]){ 0x2b, 0 }, 2);
    send((const unsigned char[]){ 0x44, 2 << 6 | 5, 0x40, 0x00, 0, 0 }, 6);
    send((const unsigned char[]){ 0x03, 0, 0, 3, 127, 64 }, 6);
    pull(14400);
    printf("reverb: sound %d, tail %d/%d\n", peak(4800, 0), peak_from(10800, 14400, 0),
           peak_from(10800, 14400, 1));
    CHECK(status_word(0) == 0); /* the sound itself is over */
    CHECK(peak_from(10800, 14400, 0) > 300 && peak_from(10800, 14400, 1) > 300);
    for (i = 0; i < 8; i++)
        pull(24000);
    CHECK(peak(24000, 0) < 8 && peak(24000, 1) < 8);

    /* Reverb off: no tail. */
    send((const unsigned char[]){ 0x44, 2 << 6 | 0, 0, 0, 0, 0 }, 6);
    send((const unsigned char[]){ 0x03, 0, 0, 3, 127, 64 }, 6);
    pull(14400);
    CHECK(peak(4800, 0) > 5000 && peak_from(10800, 14400, 0) == 0);

    /* The reverb of the other core does not get this sound. */
    send((const unsigned char[]){ 0x44, 1 << 6 | 5, 0x7f, 0x00, 0, 0 }, 6);
    send((const unsigned char[]){ 0x03, 0, 0, 3, 127, 64 }, 6);
    pull(14400);
    CHECK(peak_from(10800, 14400, 0) == 0);

    if (failures == 0)
        printf("test_snddrv: all checks passed\n");
    return failures != 0;
}
