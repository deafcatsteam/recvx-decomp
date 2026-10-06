/*
 * Draws a GS frame dump (F10 in the game, see gs_dump_frame) again and saves
 * what the display shows as a BMP picture:
 *
 *   gs_replay cvx_gsdump_000.bin out.bmp [page]
 *
 * The display often shows a copy of the previous frame (drawn by the game,
 * not by this replay); page shows a 640-pixel-wide CT32 buffer at that frame
 * buffer page instead, such as the one the frame is drawn into.
 *
 * With CVX_REPLAY_SCALE=1 to 4 (Linux, OpenGL 3.3 through EGL, as in
 * test_gs_gpu), the frame is drawn by the GPU renderer at that scale and
 * saved at its resolution.
 */
#include "../src/gs/gs.h"
#include "../src/gs/gs_gpu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

unsigned char port_scratchpad[0x4000];

#ifndef _WIN32
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
#endif

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int main(int argc, char **argv)
{
    static uint32_t pixels[GS_DISPLAY_MAX_W * GS_DISPLAY_MAX_H * 16];
    const char *scale = getenv("CVX_REPLAY_SCALE");
    int w, h;
    FILE *f;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: gs_replay DUMP OUT.bmp [PAGE]\n");
        return 2;
    }
    gs_reset();
    if (scale != NULL) {
#ifndef _WIN32
        if (!open_gl() || !gs_gpu_init(get_proc, atoi(scale))) {
#else
        {
#endif
            fprintf(stderr, "gs_replay: no OpenGL 3.3 here\n");
            return 1;
        }
    }
    if (gs_replay(argv[1]) != 0) {
        fprintf(stderr, "gs_replay: cannot read %s (or made by another version)\n", argv[1]);
        return 1;
    }
    if (argc == 4)
        gs_set_display((uint64_t)atoi(argv[3]) | (10ull << 9), (2559ull << 32) | (479ull << 44));
    if (!gs_gpu_on || !gs_gpu_read_display(pixels, GS_DISPLAY_MAX_W * GS_DISPLAY_MAX_H * 16, &w, &h))
        gs_read_display(pixels, &w, &h);
    f = fopen(argv[2], "wb");
    if (f == NULL)
        return 1;
    /* 32-bit BMP, bottom-up rows of B, G, R, A */
    fputc('B', f); fputc('M', f);
    put32(f, 54 + w * h * 4); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, w); put32(f, h); put16(f, 1); put16(f, 32);
    put32(f, 0); put32(f, w * h * 4); put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) {
            uint32_t c = pixels[y * w + x];
            fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); fputc(255, f);
        }
    fclose(f);
    printf("gs_replay: %dx%d picture saved to %s (%u primitives)\n", w, h, argv[2], gs_stats.prims);
    return 0;
}
