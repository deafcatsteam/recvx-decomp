/*
 * Checks the software GS: image uploads, CLUT textures, sprites, alpha
 * blending, triangles and DMA chains, through GIF packets built like the
 * game builds them.
 *
 * With CVX_TEST_GPU=n, the same checks run on the GPU renderer (gs_gpu.c)
 * at n times the resolution, in an OpenGL context without a window (EGL,
 * as Mesa gives it on Linux). Without one the test is skipped (77).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs.h"
#include "../src/gs/gs_gpu.h"
#include "../src/gs/gs_mem.h"

#ifndef _WIN32
#include <dlfcn.h>

static void *(*egl_proc)(const char *);

static void *get_proc(const char *name)
{
    return egl_proc(name);
}

/* An OpenGL 3.3 core context with no surface, current on this thread. */
static int open_gl(void)
{
    void *egl = dlopen("libEGL.so.1", RTLD_NOW);
    void *dpy, *ctx;
    int major, minor;
    static const int attrs[] = { 0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038 }; /* 3.3 core */

    if (egl == NULL)
        return 0;
    egl_proc = (void *(*)(const char *))dlsym(egl, "eglGetProcAddress");
    if (egl_proc == NULL)
        return 0;
    void *(*get_display)(unsigned, void *, const int *) =
        (void *(*)(unsigned, void *, const int *))egl_proc("eglGetPlatformDisplayEXT");
    unsigned (*init)(void *, int *, int *) = (unsigned (*)(void *, int *, int *))egl_proc("eglInitialize");
    unsigned (*bind)(unsigned) = (unsigned (*)(unsigned))egl_proc("eglBindAPI");
    void *(*create)(void *, void *, void *, const int *) =
        (void *(*)(void *, void *, void *, const int *))egl_proc("eglCreateContext");
    unsigned (*make_current)(void *, void *, void *, void *) =
        (unsigned (*)(void *, void *, void *, void *))egl_proc("eglMakeCurrent");
    if (get_display == NULL || init == NULL || bind == NULL || create == NULL || make_current == NULL)
        return 0;
    dpy = get_display(0x31DD, NULL, NULL); /* EGL_PLATFORM_SURFACELESS_MESA */
    if (dpy == NULL || !init(dpy, &major, &minor) || !bind(0x30A2)) /* EGL_OPENGL_API */
        return 0;
    ctx = create(dpy, NULL, NULL, attrs);
    return ctx != NULL && make_current(dpy, NULL, NULL, ctx);
}
#endif

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

/* Top-left rule: a 4x4 square drawn as a fan of two triangles, with its
 * edges on pixels, covers 16 pixels, none of them twice (additive blending
 * would show it), and not the column and row past its right and bottom. */
static void test_fill_rule(void)
{
    int once = 0, twice = 0;

    gs_reset();
    setup_frame();
    tag(7, 1, 0, 1, 0xe);
    ad(0x42, 0 | (2 << 2) | (2 << 4) | (1 << 6) | (128ull << 32)); /* Cs + Cd */
    ad(0x00, 5 | (1 << 6));                                      /* fan, blended */
    ad(0x01, 0x80101010);
    ad(0x05, xyz(8, 8));
    ad(0x05, xyz(12, 8));
    ad(0x05, xyz(12, 12));
    ad(0x05, xyz(8, 12));
    send();
    for (int y = 4; y < 16; y++)
        for (int x = 4; x < 16; x++) {
            once += (fb(x, y) & 0xffffff) == 0x101010;
            twice += (fb(x, y) & 0xffffff) == 0x202020;
        }
    CHECK(once == 16);
    CHECK(twice == 0);
    CHECK((fb(8, 8) & 0xffffff) == 0x101010 && (fb(11, 11) & 0xffffff) == 0x101010);
    CHECK((fb(12, 10) & 0xffffff) == 0 && (fb(10, 12) & 0xffffff) == 0);
}

/* Primitives may wait to be drawn together (by bands of rows, on several
 * threads): a texture replaced between two of them, or read from memory
 * they draw to, must still give what drawing them one by one gives. */
static void textured_sprite(uint32_t tbp, int x, int y)
{
    tag(6, 1, 0, 1, 0xe);
    /* TEX0: CT32, 8x8 (TW = TH = 3), TBW 1, TCC, DECAL */
    ad(0x06, (uint64_t)tbp | (1ull << 14) | (3ull << 26) | (3ull << 30) | (1ull << 34) | (1ull << 35));
    ad(0x00, 6 | (1 << 4) | (1 << 8));                 /* sprite, textured, UV */
    ad(0x03, 0);
    ad(0x05, xyz(x, y));
    ad(0x03, (8 << 4) | ((uint64_t)(8 << 4) << 16));
    ad(0x05, xyz(x + 8, y + 8));
    send();
}

static void test_batch_hazards(void)
{
    uint32_t img[64];

    gs_reset();
    setup_frame();
    /* The same texture memory, uploaded again between two sprites. */
    for (int i = 0; i < 64; i++)
        img[i] = 0x800000ff;
    upload(GS_PSMCT32, 2000, 1, 8, 8, img, sizeof(img));
    textured_sprite(2000, 0, 0);
    for (int i = 0; i < 64; i++)
        img[i] = 0x8000ff00;
    upload(GS_PSMCT32, 2000, 1, 8, 8, img, sizeof(img));
    textured_sprite(2000, 8, 0);
    CHECK(fb(1, 1) == 0x800000ff);
    CHECK(fb(9, 1) == 0x8000ff00);

    /* A texture read from the depth buffer that a sprite just filled. */
    tag(3, 1, 0, 1, 0xe);
    ad(0x4e, 8);                                       /* ZBUF_1: page 8, Z32 */
    ad(0x47, (1ull << 16) | (1ull << 17));             /* depth test ALWAYS */
    ad(0x00, 6);
    send();
    tag(3, 1, 0, 1, 0xe);
    ad(0x01, 0x80102030);
    ad(0x05, xyz(0, 16) | (0x80ff0000ull << 32));
    ad(0x05, xyz(64, 64) | (0x80ff0000ull << 32));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x47, 0);                                       /* no depth test */
    send();
    /* The depth buffer's first page (rows 0-31) holds only that value now
     * in rows 16-31; its rows 0-15 are still 0. Rows 16-23 as a texture: */
    textured_sprite(8 * 32 + 0, 40, 40);
    {
        uint32_t c = fb(44, 44);
        CHECK(c == 0x80ff0000 || c == 0);
    }
    /* A texture fully inside the filled area: page 9 (rows 32-63). */
    textured_sprite(9 * 32, 48, 40);
    CHECK(fb(50, 42) == 0x80ff0000);
    CHECK(fb(20, 20) == 0x80102030);
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

/* A gouraud-shaded overlay at the same depth as what is under it passes a
 * "greater or equal" depth test everywhere, whatever the rounding of the
 * interpolation (the game's text fades rely on it). */
static void test_equal_depth(void)
{
    int bad = 0;

    gs_reset();
    setup_frame();
    tag(3, 1, 0, 1, 0xe);
    ad(0x4e, 8 | ((uint64_t)0 << 24));      /* ZBUF_1: page 8, Z32 */
    ad(0x47, (1ull << 16) | (1ull << 17)); /* TEST_1: depth test, ALWAYS */
    ad(0x00, 6);                            /* sprite at z 65534 */
    send();
    tag(3, 1, 0, 1, 0xe);
    ad(0x01, 0x80ffffff);
    ad(0x05, xyz(0, 0) | (65534ull << 32));
    ad(0x05, xyz(64, 64) | (65534ull << 32));
    send();
    tag(2, 1, 0, 1, 0xe);
    ad(0x47, (1ull << 16) | (2ull << 17)); /* GEQUAL */
    ad(0x00, 4 | (1 << 3));                 /* gouraud triangle strip */
    send();
    /* Corners like the game's: far beyond the frame, at odd positions. */
    tag(8, 1, 0, 1, 0xe);
    ad(0x01, 0x80000000); ad(0x05, 3 | (5ull << 16) | (65534ull << 32));
    ad(0x01, 0x80000000); ad(0x05, (640 * 16 + 7) | (5ull << 16) | (65534ull << 32));
    ad(0x01, 0x40000000); ad(0x05, 3 | ((uint64_t)(104 * 16 + 9) << 16) | (65534ull << 32));
    ad(0x01, 0x40000000); ad(0x05, (640 * 16 + 7) | ((uint64_t)(104 * 16 + 9) << 16) | (65534ull << 32));
    send();
    /* Pixel row and column 0 are outside the triangles. */
    for (int y = 1; y < 64; y++)
        for (int x = 1; x < 64; x++)
            bad += (fb(x, y) & 0xffffff) != 0;
    CHECK(bad == 0);
}

/* An image sent over what was drawn: over a whole page, and over a part of
 * one (the rest of what was drawn stays). */
static void test_upload_over_drawing(void)
{
    static uint32_t img[64 * 32];

    gs_reset();
    setup_frame();
    tag(3, 1, 0, 1, 0xe);
    ad(0x00, 6);
    ad(0x01, 0x80112233);
    ad(0x05, xyz(0, 0));
    send();
    tag(1, 1, 0, 1, 0xe);
    ad(0x05, xyz(64, 64));
    send();
    for (int i = 0; i < 64 * 32; i++)
        img[i] = 0x80000000u | (uint32_t)i;
    upload(GS_PSMCT32, 0, 1, 64, 32, img, sizeof(img));  /* page 0: rows 0-31 */
    upload(GS_PSMCT32, 0, 1, 4, 4, img, 4 * 4 * 4);      /* part of it again */
    CHECK(fb(10, 10) == (0x80000000u | (10 * 64 + 10)));
    CHECK(fb(2, 3) == (0x80000000u | (3 * 4 + 2)));
    /* Rows 32-35: a part of page 1, whose other rows are still drawn. */
    tag(4, 0, 0, 1, 0xe);
    ad(0x50, ((uint64_t)0 << 32) | (1ull << 48) | ((uint64_t)GS_PSMCT32 << 56));
    ad(0x51, (32ull << 48));
    ad(0x52, 4 | (4ull << 32));
    ad(0x53, 0);
    tag(4, 1, 2, 0, 0);
    memcpy(&pkt[pn], img, 64);
    pn += 8;
    send();
    CHECK(fb(1, 33) == (0x80000000u | 5));
    CHECK(fb(20, 40) == 0x80112233);
    CHECK(fb(5, 33) == 0x80112233);
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
    const char *gpu = getenv("CVX_TEST_GPU");

    if (gpu != NULL) {
#ifndef _WIN32
        if (!open_gl() || !gs_gpu_init(get_proc, atoi(gpu))) {
            printf("test_gs: no OpenGL 3.3 here, skipped\n");
            return 77;
        }
#else
        return 77;
#endif
    }
    gs_reset();
    test_upload_ct32();
    test_layout_roundtrip();
    test_sprite_flat();
    test_clut_texture();
    test_alpha_blend();
    test_triangle();
    test_fill_rule();
    test_batch_hazards();
    test_dma_chain();
    test_equal_depth();
    test_upload_over_drawing();
    test_dump_replay();
    if (failures == 0)
        printf("test_gs: all checks passed\n");
    return failures != 0;
}
