/*
 * Checks the software GS: image uploads, CLUT textures, sprites, alpha
 * blending, triangles and DMA chains, through GIF packets built like the
 * game builds them.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/gs/gs.h"
#include "../src/gs/gs_mem.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* A small GIF packet builder. */
static uint64_t pkt[8192];
static int pn;

static void tag(int nloop, int eop, int flg, int nreg, uint64_t regs)
{
    pkt[pn++] = (uint64_t)nloop | ((uint64_t)eop << 15) | ((uint64_t)flg << 58) | ((uint64_t)nreg << 60);
    pkt[pn++] = regs;
}

static void ad(int reg, uint64_t value)
{
    pkt[pn++] = value;
    pkt[pn++] = (uint64_t)reg;
}

static void send(void)
{
    gs_gif_write(pkt, pn / 2);
    pn = 0;
}

/* Frame buffer at page 0, 64 pixels wide, CT32, no depth test; offset so
 * that window coordinates equal GS coordinates. */
static void setup_frame(void)
{
    tag(7, 1, 0, 1, 0xe);
    ad(0x4c, 0 | (1ull << 16) | ((uint64_t)GS_PSMCT32 << 24));   /* FRAME_1 */
    ad(0x18, 0);                                                  /* XYOFFSET_1 */
    ad(0x40, 0 | (63ull << 16) | (0ull << 32) | (63ull << 48));   /* SCISSOR_1 */
    ad(0x47, 0);                                                  /* TEST_1 */
    ad(0x1a, 1);                                                  /* PRMODECONT */
    ad(0x46, 1);                                                  /* COLCLAMP */
    ad(0x42, 0);                                                  /* ALPHA_1 */
    send();
}

static uint64_t xyz(int x, int y)
{
    return (uint64_t)(x << 4) | ((uint64_t)(y << 4) << 16);
}

static void upload(int psm, uint32_t bp, int bw, int w, int h, const void *data, int bytes)
{
    int qw = (bytes + 15) / 16;

    tag(4, 0, 0, 1, 0xe);
    ad(0x50, ((uint64_t)bp << 32) | ((uint64_t)bw << 48) | ((uint64_t)psm << 56));
    ad(0x51, 0);
    ad(0x52, (uint64_t)w | ((uint64_t)h << 32));
    ad(0x53, 0);
    tag(qw, 1, 2, 0, 0);
    memset(&pkt[pn], 0, qw * 16);
    memcpy(&pkt[pn], data, bytes);
    pn += qw * 2;
    send();
}

static uint32_t fb(int x, int y)
{
    return gs_read_pixel(GS_PSMCT32, 0, 1, x, y);
}

static void test_upload_ct32(void)
{
    uint32_t img[16 * 8];

    for (int i = 0; i < 16 * 8; i++)
        img[i] = 0x01000000u * i + i;
    upload(GS_PSMCT32, 64, 1, 16, 8, img, sizeof(img));
    CHECK(gs_read_pixel(GS_PSMCT32, 64, 1, 0, 0) == img[0]);
    CHECK(gs_read_pixel(GS_PSMCT32, 64, 1, 15, 7) == img[16 * 7 + 15]);
    CHECK(gs_read_pixel(GS_PSMCT32, 64, 1, 9, 3) == img[16 * 3 + 9]);
}

static void test_layout_roundtrip(void)
{
    /* Each format round-trips, and distinct pixels of a 128x128 area map to
     * distinct places (checked by writing then reading back unique values). */
    static const int psms[] = { GS_PSMCT32, GS_PSMCT16, GS_PSMCT16S, GS_PSMT8, GS_PSMT4 };
    for (unsigned k = 0; k < sizeof(psms) / sizeof(psms[0]); k++) {
        int psm = psms[k], ok = 1;
        uint32_t mask = psm == GS_PSMT4 ? 15 : psm == GS_PSMT8 ? 255 : psm == GS_PSMCT32 ? 0xffffffff : 0xffff;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
                gs_write_pixel(psm, 1024, 2, x, y, (uint32_t)(x * 131 + y * 7919) & mask);
        for (int y = 0; y < 128 && ok; y++)
            for (int x = 0; x < 128 && ok; x++)
                ok = gs_read_pixel(psm, 1024, 2, x, y) == ((uint32_t)(x * 131 + y * 7919) & mask);
        CHECK(ok);
    }
    /* 8-bit pixels read from a 32-bit write: the four bytes of the first
     * word are the 8-bit pixels (0,0), (4,2), (8,0) and (12,2). */
    gs_write_pixel(GS_PSMCT32, 2048, 1, 0, 0, 0x44332211);
    CHECK(gs_read_pixel(GS_PSMT8, 2048, 2, 0, 0) == 0x11);
    CHECK(gs_read_pixel(GS_PSMT8, 2048, 2, 8, 0) == 0x33);
    CHECK(gs_read_pixel(GS_PSMT8, 2048, 2, 4, 2) == 0x22);
    CHECK(gs_read_pixel(GS_PSMT8, 2048, 2, 12, 2) == 0x44);
}

static void test_sprite_flat(void)
{
    setup_frame();
    tag(3, 1, 0, 1, 0xe);
    ad(0x00, 6);                                          /* PRIM: sprite */
    ad(0x01, 0x80102030);                                 /* RGBAQ */
    ad(0x05, xyz(4, 4));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(8, 6));
    send();
    CHECK(fb(4, 4) == 0x80102030);
    CHECK(fb(7, 5) == 0x80102030);
    CHECK(fb(8, 5) != 0x80102030); /* right and bottom edges are exclusive */
    CHECK(fb(4, 6) != 0x80102030);
}

static void test_clut_texture(void)
{
    uint8_t idx[16 * 16];
    uint32_t clut[256];

    /* Palette entry i = colour i (CSM1 layout is handled by the GS). */
    for (int i = 0; i < 256; i++)
        clut[i] = 0x80000000u | (uint32_t)i | ((uint32_t)(255 - i) << 8);
    /* Upload the CLUT as the 16x16 CSM1 arrangement: bits 3 and 4 swapped. */
    uint32_t arranged[256];
    for (int i = 0; i < 256; i++) {
        int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        arranged[j] = clut[i];
    }
    upload(GS_PSMCT32, 3000, 1, 16, 16, arranged, sizeof(arranged));
    for (int i = 0; i < 256; i++)
        idx[i] = (uint8_t)i;
    upload(GS_PSMT8, 2000, 2, 16, 16, idx, sizeof(idx));

    setup_frame();
    tag(6, 1, 0, 1, 0xe);
    /* TEX0: TBP 2000, TBW 2, PSMT8, 16x16, TCC=1, DECAL, CBP 3000, CT32, CLD=1 */
    ad(0x06, 2000ull | (2ull << 14) | ((uint64_t)GS_PSMT8 << 20) | (4ull << 26) | (4ull << 30) |
                 (1ull << 34) | (1ull << 35) | (3000ull << 37) | (1ull << 61));
    ad(0x00, 6 | (1 << 4) | (1 << 8));                    /* sprite, textured, UV */
    ad(0x03, 0);                                          /* UV (0,0) */
    ad(0x05, xyz(16, 16));
    ad(0x03, (16 << 4) | ((uint64_t)(16 << 4) << 16));    /* UV (16,16) */
    ad(0x05, xyz(32, 32));
    send();
    CHECK(fb(16, 16) == clut[0]);
    CHECK(fb(17, 16) == clut[1]);
    CHECK(fb(16 + 9, 16 + 3) == clut[3 * 16 + 9]);
    CHECK(fb(31, 31) == clut[255]);
}

static void test_alpha_blend(void)
{
    setup_frame();
    /* Destination: white. */
    tag(3, 1, 0, 1, 0xe);
    ad(0x00, 6);
    ad(0x01, 0x80ffffff);
    ad(0x05, xyz(40, 40));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(42, 42));
    send();
    /* Source black, ((Cs - Cd) * FIX >> 7) + Cd with FIX = 64: the shift
     * rounds down, so 255 becomes 127. */
    tag(4, 1, 0, 1, 0xe);
    ad(0x42, 0 | (1 << 2) | (2 << 4) | (1 << 6) | (64ull << 32));
    ad(0x00, 6 | (1 << 6));
    ad(0x01, 0x80000000);
    ad(0x05, xyz(40, 40));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(42, 42));
    send();
    CHECK((fb(40, 40) & 0xffffff) == 0x7f7f7f);
}

static void test_triangle(void)
{
    int inside = 0;

    setup_frame();
    tag(4, 1, 0, 1, 0xe);
    ad(0x00, 3);                                          /* triangle */
    ad(0x01, 0x800000ff);
    ad(0x05, xyz(0, 48));
    ad(0x05, xyz(10, 48));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(0, 58));
    send();
    for (int y = 48; y < 60; y++)
        for (int x = 0; x < 12; x++)
            inside += fb(x, y) == 0x800000ff;
    /* Right triangle with legs of 10: about 50 pixels. */
    CHECK(inside >= 45 && inside <= 66);
    CHECK(fb(1, 49) == 0x800000ff);
    CHECK(fb(9, 57) != 0x800000ff);
}

static void test_dma_chain(void)
{
    static uint64_t chain[64] __attribute__((aligned(16)));
    static uint64_t data[16] __attribute__((aligned(16)));
    int n = 0;

    setup_frame();
    /* ref: GIF data elsewhere (2 qwords: tag + PRIM), then cnt with the rest. */
    data[0] = 2 | (1ull << 60);                         /* tag: NLOOP 2, A+D */
    data[1] = 0xe;
    data[2] = 6; data[3] = 0x00;                        /* PRIM sprite */
    data[4] = 0x80abcdef; data[5] = 0x01;               /* RGBAQ */
    chain[n++] = 3 | (3ull << 28) | ((uint64_t)(uintptr_t)data << 32); /* ref, qwc 3 */
    chain[n++] = 0;
    chain[n++] = 3 | (7ull << 28);                      /* end, qwc 3 */
    chain[n++] = 0;
    chain[n++] = 2 | (1ull << 15) | (1ull << 60);       /* tag: NLOOP 2, EOP */
    chain[n++] = 0xe;
    chain[n++] = xyz(50, 50); chain[n++] = 0x05;
    chain[n++] = xyz(52, 52); chain[n++] = 0x05;
    gs_dma_gif_chain((uint32_t)(uintptr_t)chain);
    CHECK(fb(50, 50) == 0x80abcdef);
    CHECK(fb(51, 51) == 0x80abcdef);
}

/* A frame dump drawn again gives the same picture. */
static void test_dump_replay(void)
{
    const char *path = "test_gsdump.bin";

    gs_reset();
    gs_dump_frame(path);
    gs_dump_vblank(); /* starts the dump */
    setup_frame();
    tag(3, 1, 0, 1, 0xe);
    ad(0x00, 6);
    ad(0x01, 0x80405060);
    ad(0x05, xyz(20, 20));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(24, 24));
    send();
    gs_dump_vblank();
    gs_dump_vblank(); /* ends it */
    gs_reset();
    CHECK(fb(21, 21) == 0);
    CHECK(gs_replay(path) == 0);
    CHECK(fb(21, 21) == 0x80405060);
    CHECK(fb(24, 24) != 0x80405060);
    remove(path);
}

int main(void)
{
    gs_reset();
    test_upload_ct32();
    test_layout_roundtrip();
    test_sprite_flat();
    test_clut_texture();
    test_alpha_blend();
    test_triangle();
    test_dma_chain();
    test_dump_replay();
    if (failures == 0)
        printf("test_gs: all checks passed\n");
    return failures != 0;
}
