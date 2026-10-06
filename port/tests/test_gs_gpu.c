/*
 * The GPU renderer (gs_gpu.c) against the software GS: random scenes, one
 * per group of GS features, are drawn by both and their frame and depth
 * buffers compared pixel by pixel, at the PS2's resolution.
 *
 * What may differ: a colour or depth by 1 (OpenGL interpolates in its own
 * way), a colour by up to 3 where OpenGL blends (it rounds where the GS
 * truncates; with CVX_GPU_EXACT the shader blends, like the GS), and a few
 * pixels along sloped edges (OpenGL's rule for a pixel exactly on an edge
 * is its own; the GS's is kept along rows and columns). Nothing else.
 *
 * Needs an OpenGL 3.3 context without a window (EGL, as Mesa gives it on
 * Linux); without one the test is skipped (77).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs.h"
#include "../src/gs/gs_gpu.h"
#include "../src/gs/gs_mem.h"

#ifdef _WIN32
int main(void)
{
    return 77;
}
#else
#include <dlfcn.h>

static void *(*egl_proc)(const char *);

static void *get_proc(const char *name)
{
    return egl_proc(name);
}

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

/* ---- GIF packets ---- */

static uint64_t pkt[65536];
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

static void upload(int psm, uint32_t bp, int bw, int w, int h, const void *data, int bytes)
{
    int qw = (bytes + 15) / 16;

    tag(4, 0, 0, 1, 0xe);
    ad(0x50, ((uint64_t)bp << 32) | ((uint64_t)bw << 48) | ((uint64_t)psm << 56));
    ad(0x51, 0);
    ad(0x52, (uint64_t)w | ((uint64_t)h << 32));
    ad(0x53, 0);
    send();
    tag(qw, 1, 2, 0, 0);
    memset(&pkt[pn], 0, qw * 16);
    memcpy(&pkt[pn], data, bytes);
    pn += qw * 2;
    send();
}

/* ---- Random scenes ---- */

static uint32_t seed;

static uint32_t rnd(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

static int rn(int n)
{
    return (int)(rnd() % (uint32_t)n);
}

enum {
    F_TEX = 1, F_CLUT = 2, F_BLEND = 4, F_DEPTH = 8, F_ATEST = 16, F_FOG = 32, F_STQ = 64, F_BIL = 128,
    F_MASK = 256, F_CT16 = 512, F_DATE = 1024, F_WRAP = 2048, F_FEEDBACK = 4096, F_GOURAUD = 8192,
    F_Z24 = 16384,
};

#define W 128
#define H 96
#define FBP 0      /* frame buffer page */
#define ZBP 40     /* depth buffer page */
#define TEX_BP 4000
#define T8_BP 5000
#define CLUT_BP 6000

static void scene_setup(int features)
{
    static uint32_t img[64 * 64], clut[256];
    static uint8_t idx[64 * 64];
    int fpsm = (features & F_CT16) ? GS_PSMCT16 : GS_PSMCT32;

    gs_reset();
    for (int i = 0; i < 64 * 64; i++) {
        img[i] = rnd();
        if (features & F_BLEND)
            img[i] = (img[i] & 0x00ffffff) | ((uint32_t)rn(129) << 24); /* As <= 0x80 */
        idx[i] = (uint8_t)rnd();
    }
    for (int i = 0; i < 256; i++) {
        clut[i] = rnd();
        if (features & F_BLEND)
            clut[i] = (clut[i] & 0x00ffffff) | ((uint32_t)rn(129) << 24);
    }
    upload(GS_PSMCT32, TEX_BP, 1, 64, 64, img, sizeof(img));
    upload(GS_PSMT8, T8_BP, 1, 64, 64, idx, sizeof(idx));
    upload(GS_PSMCT32, CLUT_BP, 1, 16, 16, clut, sizeof(clut));

    tag(9, 1, 0, 1, 0xe);
    ad(0x4c, FBP | (2ull << 16) | ((uint64_t)fpsm << 24));            /* FRAME_1: 128 wide */
    ad(0x4e, ZBP | ((uint64_t)(features & F_Z24 ? 1 : 0) << 24));        /* ZBUF_1: Z32 or Z24 */
    ad(0x18, (1000ull * 16) | ((1000ull * 16) << 32));                /* XYOFFSET_1 */
    ad(0x40, 0 | ((uint64_t)(W - 1) << 16) | (0ull << 32) | ((uint64_t)(H - 1) << 48));
    ad(0x47, 0);
    ad(0x1a, 1);
    ad(0x46, 1);
    ad(0x42, 0);
    ad(0x3d, 0x204060);                                               /* FOGCOL */
    send();
    /* A background with some alpha, and depth at mid range. */
    tag(6, 1, 0, 1, 0xe);
    ad(0x47, (1ull << 16) | (1ull << 17));
    ad(0x00, 6);
    ad(0x01, 0x40000000 | (rnd() & 0xffffff) | ((features & F_DATE) ? 0x80000000 : 0));
    ad(0x05, (1000 << 4) | ((uint64_t)(1000 << 4) << 16) | (0x800000ull << 32));
    ad(0x05, ((1000 + W) << 4) | ((uint64_t)((1000 + H) << 4) << 16) | (0x800000ull << 32));
    ad(0x47, 0);
    send();
    /* A pattern of 1 pixel squares with alpha bits set or not, for DATE. */
    if (features & (F_DATE | F_BLEND)) {
        for (int i = 0; i < 40; i++) {
            int x = rn(W - 8) + 1000, y = rn(H - 8) + 1000;
            tag(3, 1, 0, 1, 0xe);
            ad(0x00, 6);
            ad(0x01, rnd() | (rn(2) ? 0x80000000u : 0));
            ad(0x05, (x << 4) | ((uint64_t)(y << 4) << 16) | (0x800000ull << 32));
            send();
            tag(1, 1, 0, 1, 0xe);
            ad(0x05, ((x + 1 + rn(6)) << 4) | ((uint64_t)((y + 1 + rn(6)) << 4) << 16) | (0x800000ull << 32));
            send();
        }
    }
}

/* The picture read back as a texture, as effects do it: an area copied to
 * another (or each pixel onto itself), not a primitive reading what it
 * draws (undefined on the GS, which reads through a texture cache). */
static void feedback_sprite(void)
{
    int w = 4 + rn(40), h = 4 + rn(40), same = rn(3) == 0;
    int sx = rn(64 - w), sy = rn(H - h), dx = same ? sx : 64 + rn(64 - w), dy = same ? sy : rn(H - h);

    tag(8, 1, 0, 1, 0xe);
    ad(0x06, (uint64_t)FBP * 32 | (2ull << 14) | ((uint64_t)GS_PSMCT32 << 20) | (7ull << 26) | (7ull << 30) |
                 (1ull << 34) | ((uint64_t)rn(2) << 35));
    ad(0x14, 0);
    ad(0x08, 0);
    ad(0x47, 0);
    ad(0x42, (uint64_t)rn(3) | ((uint64_t)rn(3) << 2) | (2ull << 4) | ((uint64_t)rn(3) << 6) |
                 ((uint64_t)rn(129) << 32));
    ad(0x4c, FBP | (2ull << 16) | ((uint64_t)GS_PSMCT32 << 24));
    ad(0x00, 6 | (1 << 4) | (1 << 8) | ((uint64_t)rn(2) << 6));
    ad(0x01, 0x80808080);
    send();
    tag(4, 1, 0, 1, 0xe);
    ad(0x03, (uint64_t)(sx * 16) | ((uint64_t)(sy * 16) << 16));
    ad(0x05, (uint64_t)((1000 + dx) * 16) | ((uint64_t)((1000 + dy) * 16) << 16));
    ad(0x03, (uint64_t)((sx + w) * 16) | ((uint64_t)((sy + h) * 16) << 16));
    ad(0x05, (uint64_t)((1000 + dx + w) * 16) | ((uint64_t)((1000 + dy + h) * 16) << 16));
    send();
}

static void random_prim(int features)
{
    int sprite = rn(3) == 0;
    int tme = (features & F_TEX) && rn(4) != 0;
    int fst = !(features & F_STQ) || rn(2);
    uint64_t prim = (sprite ? 6 : 3) | ((features & F_GOURAUD) && rn(2) ? 1 << 3 : 0) | (tme ? 1 << 4 : 0) |
                    ((features & F_FOG) && rn(2) ? 1 << 5 : 0) | ((features & F_BLEND) && rn(4) ? 1 << 6 : 0) |
                    (fst ? 1 << 8 : 0);
    uint64_t tex0, test = 0, alpha, clamp = 0;
    int t8 = (features & F_CLUT) && rn(2);
    int n = sprite ? 2 : 3, px = 0, py = 0;

    tag(9, 1, 0, 1, 0xe);
    /* TEX0: 64x64, TFX and TCC random, CLUT loaded each time (CLD 1) */
    tex0 = (uint64_t)(t8 ? T8_BP : TEX_BP) | (1ull << 14) | ((uint64_t)(t8 ? GS_PSMT8 : GS_PSMCT32) << 20) |
           (6ull << 26) | (6ull << 30) | ((uint64_t)rn(2) << 34) | ((uint64_t)rn(4) << 35) |
           ((uint64_t)CLUT_BP << 37) | (1ull << 61);
    ad(0x06, tex0);
    ad(0x14, (features & F_BIL) && rn(2) ? (1ull << 5) | (1ull << 6) : 0); /* TEX1: MMAG, MMIN */
    if (features & F_WRAP) {
        int m = rn(4);
        clamp = (uint64_t)m | ((uint64_t)rn(4) << 2);
        if (m == 3 || (clamp >> 2) == 3)
            clamp |= (15ull << 4) | (32ull << 14) | (7ull << 24) | (16ull << 34); /* region repeat masks */
        else
            clamp |= (5ull << 4) | (50ull << 14) | (3ull << 24) | (40ull << 34);
    }
    ad(0x08, clamp);
    if (features & F_ATEST && rn(2))
        test |= 1 | ((uint64_t)rn(8) << 1) | ((uint64_t)rn(256) << 4) | ((uint64_t)rn(4) << 12);
    if (features & F_DATE && rn(2))
        test |= (1ull << 14) | ((uint64_t)rn(2) << 15);
    if (features & F_DEPTH)
        test |= (1ull << 16) | ((uint64_t)(1 + rn(3)) << 17);
    ad(0x47, test);
    alpha = (uint64_t)rn(3) | ((uint64_t)rn(3) << 2) | ((uint64_t)rn(3) << 4) | ((uint64_t)rn(3) << 6) |
            ((uint64_t)rn(129) << 32);
    ad(0x42, alpha);
    ad(0x4a, (features & F_BLEND) && rn(4) == 0);                      /* FBA */
    ad(0x4c, FBP | (2ull << 16) | ((uint64_t)((features & F_CT16) ? GS_PSMCT16 : GS_PSMCT32) << 24) |
                 ((uint64_t)((features & F_MASK) && rn(2) ? (rn(2) ? 0xff000000u : rnd() & 0x00ff00ffu) : 0)
                  << 32));
    ad(0x4e, ZBP | ((uint64_t)(features & F_Z24 ? 1 : 0) << 24) | ((uint64_t)((features & F_DEPTH) && rn(3) == 0)
                                                                   << 32));
    ad(0x00, prim);
    send();

    for (int i = 0; i < n; i++) {
        /* Near the frame, as the game's clipped polygons (with an offset
         * so that negative positions stay positive in XYOFFSET terms). */
        int x = rn((W + 32) * 16) + 1000 * 16 - 16 * 16, y = rn((H + 32) * 16) + 1000 * 16 - 16 * 16;
        uint32_t z = 0x400000 + rnd() % 0x800000;
        uint32_t rgba = rnd();
        float q = 0.5f + (rnd() % 1000) / 500.0f;
        float s = (rnd() % 2000) / 1000.0f - 0.5f, t = (rnd() % 2000) / 1000.0f - 0.5f;
        uint32_t qb, sb, tb;

        if (features & F_BLEND)
            rgba = (rgba & 0x00ffffff) | ((uint32_t)rn(129) << 24);
        if (sprite && i == 1) { /* not too large */
            x = px + rn(48 * 16) - 24 * 16;
            y = py + rn(48 * 16) - 24 * 16;
            x = x < 0 ? 0 : x;
            y = y < 0 ? 0 : y;
        }
        px = x;
        py = y;
        memcpy(&qb, &q, 4);
        s *= q;
        t *= q;
        memcpy(&sb, &s, 4);
        memcpy(&tb, &t, 4);
        tag(4, 1, 0, 1, 0xe);
        ad(0x02, sb | ((uint64_t)tb << 32));
        ad(0x01, rgba | ((uint64_t)qb << 32));
        ad(0x03, (uint64_t)rn(64 * 16 * 2) | ((uint64_t)rn(64 * 16 * 2) << 16));
        ad(i == n - 1 ? 0x04 : 0x0c, (uint64_t)(x & 0xffff) | ((uint64_t)(y & 0xffff) << 16) |
                                     ((uint64_t)(z >> 8) << 32) | ((uint64_t)rn(256) << 56)); /* XYZF */
        send();
    }
}

static uint32_t frame_px[W * H], depth_px[W * H];

static void snapshot(int features, uint32_t *f, uint32_t *z)
{
    int fpsm = (features & F_CT16) ? GS_PSMCT16 : GS_PSMCT32;
    int zpsm = (features & F_Z24) ? GS_PSMZ24 : GS_PSMZ32;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            f[y * W + x] = gs_read_pixel(fpsm, FBP * 32, 2, x, y);
            z[y * W + x] = gs_read_pixel(zpsm, ZBP * 32, 2, x, y);
        }
}

static int prims = 150;

static void draw_scene(int features, uint32_t s)
{
    seed = s;
    scene_setup(features);
    for (int i = 0; i < prims; i++) {
        if ((features & F_FEEDBACK) && rn(2))
            feedback_sprite();
        else
            random_prim(features);
    }
}

static int channel_diff(uint32_t a, uint32_t b, int is16)
{
    int worst = 0;

    if (is16) {
        for (int c = 0; c < 3; c++) {
            int d = abs((int)((a >> (c * 5)) & 31) - (int)((b >> (c * 5)) & 31));
            worst = d > worst ? d : worst;
        }
        return ((a ^ b) & 0x8000) ? 255 : worst;
    }
    for (int c = 0; c < 4; c++) {
        int d = abs((int)((a >> (c * 8)) & 255) - (int)((b >> (c * 8)) & 255));
        worst = d > worst ? d : worst;
    }
    return worst;
}

int main(void)
{
    static const struct {
        const char *name;
        int features;
    } scenes[] = {
        { "flat colours", 0 },
        { "gouraud", F_GOURAUD },
        { "textures", F_TEX },
        { "CLUT textures", F_TEX | F_CLUT },
        { "perspective", F_TEX | F_STQ | F_GOURAUD },
        { "bilinear", F_TEX | F_BIL | F_CLUT },
        { "wrap modes", F_TEX | F_WRAP | F_CLUT },
        { "fog", F_FOG | F_GOURAUD | F_TEX },
        { "alpha test", F_TEX | F_ATEST | F_DEPTH },
        { "depth", F_DEPTH | F_GOURAUD },
        { "Z24", F_DEPTH | F_Z24 | F_STQ | F_TEX },
        { "blending", F_BLEND | F_TEX | F_GOURAUD },
        { "masks", F_MASK | F_TEX | F_BLEND },
        { "16-bit frame", F_CT16 | F_TEX | F_GOURAUD },
        { "destination alpha", F_DATE | F_TEX },
        { "frame read as texture", F_FEEDBACK | F_TEX },
        { "everything", F_TEX | F_CLUT | F_BLEND | F_DEPTH | F_ATEST | F_FOG | F_BIL | F_GOURAUD | F_STQ },
    };
    enum { N = sizeof(scenes) / sizeof(scenes[0]) };
    static uint32_t soft_f[N][W * H], soft_z[N][W * H];
    int exact = getenv("CVX_GPU_EXACT") != NULL;
    int close = exact ? 1 : 3; /* colour difference allowed */
    /* Pixels along sloped edges; with OpenGL's blending, also those where a
     * colour off by 1 later went through a bit mask (FBMSK) and became
     * anything. */
    int edge_pixels = exact ? 40 : 80;
    const char *only = getenv("CVX_TEST_SCENE"); /* for debugging: one scene, CVX_TEST_PRIMS primitives */
    int failures = 0;

    if (getenv("CVX_TEST_PRIMS") != NULL)
        prims = atoi(getenv("CVX_TEST_PRIMS"));

    /* Software first, then the same scenes on the GPU. */
    for (int i = 0; i < N; i++) {
        if (only != NULL && atoi(only) != i)
            continue;
        draw_scene(scenes[i].features, 1234u + i * 77u);
        snapshot(scenes[i].features, soft_f[i], soft_z[i]);
    }
    if (!open_gl() || !gs_gpu_init(get_proc, 1)) {
        printf("test_gs_gpu: no OpenGL 3.3 here, skipped\n");
        return 77;
    }
    for (int i = 0; i < N; i++) {
        int is16 = (scenes[i].features & F_CT16) != 0, near = 0, bad = 0, zbad = 0, first = -1;
        if (only != NULL && atoi(only) != i)
            continue;
        draw_scene(scenes[i].features, 1234u + i * 77u);
        snapshot(scenes[i].features, frame_px, depth_px);
        for (int p = 0; p < W * H; p++) {
            int d = channel_diff(soft_f[i][p], frame_px[p], is16);
            int64_t dz = (int64_t)soft_z[i][p] - depth_px[p];
            near += d > 0 && d <= close;
            if (d > close) {
                bad++;
                if (first < 0)
                    first = p;
            }
            zbad += dz < -1 || dz > 1;
            if ((dz < -1 || dz > 1) && only != NULL && zbad <= 10)
                printf("  z %d,%d: %08x gpu %08x\n", p % W, p / W, soft_z[i][p], depth_px[p]);
            if (d > close && only != NULL && (bad <= 40 || getenv("CVX_TEST_ALL") != NULL))
                printf("  %d,%d: %08x gpu %08x\n", p % W, p / W, soft_f[i][p], frame_px[p]);
        }
        printf("%-24s %5d pixels off by %s, %3d different, %3d depths different", scenes[i].name, near,
               close == 1 ? "1" : "1 to 3", bad, zbad);
        if (first >= 0)
            printf(" (first at %d,%d: %08x, gpu %08x)", first % W, first / W, soft_f[i][first], frame_px[first]);
        printf("\n");
        if (bad > edge_pixels || zbad > 40)
            failures++;
    }
    if (failures == 0)
        printf("test_gs_gpu: the GPU draws what the software GS draws\n");
    return failures != 0;
}
#endif
