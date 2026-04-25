/*
 * SDL2 + OpenGL backend.
 *
 * Compat profile on purpose: phase 4b wants a fullscreen textured quad
 * for FMV output, and immediate mode (glBegin/glEnd + glTexImage2D) is
 * the shortest path from "I have an RGBA buffer" to "pixels on screen"
 * without introducing a shader+VAO dependency this early. Vulkan /
 * GL-core slots stay open behind the backend vtable.
 */

#include "recvx_port.h"

#if defined(RECVX_HAVE_SDL2) && defined(RECVX_HAVE_GL)

#include <SDL.h>
#include <SDL_opengl.h>
#include <stdlib.h>    /* qsort */

static SDL_Window*    g_window;
static SDL_GLContext  g_glctx;
static bool           g_quit;
static int            g_win_w, g_win_h;

static GLuint         g_fmv_tex;
static int            g_fmv_w, g_fmv_h;
static bool           g_fmv_valid;

static SDL_AudioDeviceID g_audio_dev;
static int               g_audio_rate;        /* source rate we were asked to play */
static int               g_device_rate;       /* rate SDL actually opened */
static uint8_t           g_audio_carry[4];    /* 0..3 leftover bytes between calls */
static int               g_audio_carry_len;
/* Last input frame deferred across calls so linear interpolation
 * always has a valid "next" sample for every output. */
static int16_t           g_audio_prev_L;
static int16_t           g_audio_prev_R;
static bool              g_audio_has_prev;

static int gl_init(const recvx_backend_config* cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        RX_LOG("backend_gl", "SDL_Init: %s", SDL_GetError());
        return -1;
    }
    /* Compat profile — see file header. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* Force true 32-bit RGBA8 framebuffer. Without these hints SDL+Windows
     * WGL can select an RGB565 pixel format as a "matching default", which
     * produces visible 5/6/5-bit banding on smooth gradients (skin tones,
     * JPEG backgrounds, FMV fades). Requesting 8 bits per channel forces
     * the driver to pick a true-color pixel format or fail outright. */
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  24);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (cfg->fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    g_win_w = cfg->width; g_win_h = cfg->height;
    g_window = SDL_CreateWindow(cfg->window_title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                cfg->width, cfg->height, flags);
    if (!g_window) {
        RX_LOG("backend_gl", "SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    g_glctx = SDL_GL_CreateContext(g_window);
    if (!g_glctx) {
        RX_LOG("backend_gl", "SDL_GL_CreateContext: %s", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(cfg->vsync ? 1 : 0);

    /* Verify the driver honored our RGBA8 request. If any channel ends up
     * below 8 bits we'll see banding — the log line above is the first
     * thing to check if the image looks posterized. */
    int r = 0, g = 0, b = 0, a = 0, d = 0;
    SDL_GL_GetAttribute(SDL_GL_RED_SIZE,   &r);
    SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &g);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE,  &b);
    SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &a);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &d);
    RX_LOG("backend_gl", "pixel format: R%d G%d B%d A%d depth=%d%s",
           r, g, b, a, d,
           (r >= 8 && g >= 8 && b >= 8) ? "" : " — BANDING LIKELY");
    return 0;
}

static void gl_shutdown(void) {
    if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev = 0; }
    if (g_fmv_tex) { glDeleteTextures(1, &g_fmv_tex); g_fmv_tex = 0; }
    if (g_glctx)   { SDL_GL_DeleteContext(g_glctx); g_glctx = NULL; }
    if (g_window)  { SDL_DestroyWindow(g_window); g_window = NULL; }
    SDL_Quit();
}

static void gl_begin(void) {
    glViewport(0, 0, g_win_w, g_win_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void gl_end(void) {
    SDL_GL_SwapWindow(g_window);
}

static bool map_sdl_key(SDL_Keycode k, recvx_key* out) {
    switch (k) {
        case SDLK_UP:        *out = RX_KEY_UP;     return true;
        case SDLK_DOWN:      *out = RX_KEY_DOWN;   return true;
        case SDLK_LEFT:      *out = RX_KEY_LEFT;   return true;
        case SDLK_RIGHT:     *out = RX_KEY_RIGHT;  return true;
        case SDLK_z:         *out = RX_KEY_ACTION; return true;  /* Cross */
        case SDLK_x:         *out = RX_KEY_CANCEL; return true;  /* Circle */
        case SDLK_a:         *out = RX_KEY_AIM;    return true;  /* Square */
        case SDLK_s:         *out = RX_KEY_MENU;   return true;  /* Triangle */
        case SDLK_q:         *out = RX_KEY_L1;     return true;
        case SDLK_w:         *out = RX_KEY_R1;     return true;
        case SDLK_e:         *out = RX_KEY_L2;     return true;
        case SDLK_r:         *out = RX_KEY_R2;     return true;
        case SDLK_RETURN:    *out = RX_KEY_START;  return true;
        case SDLK_BACKSPACE: *out = RX_KEY_SELECT; return true;
        default: return false;
    }
}

/* Sample-audition debug keybinds: F1..F10 plays COMMON.MLT sample 0..9
 * directly (bypassing the SeNo remap). Lets the user identify each
 * sample by ear so the correct --se-remap mapping can be determined.
 * F1=sample 0 ... F9=sample 8, F10=sample 9. Typing-safe: ignored when
 * MSA isn't initialized (e.g. running the FMV demo without --game). */
static void handle_sample_audition(SDL_Keycode k) {
    int idx = -1;
    if      (k == SDLK_F1)  idx = 0;
    else if (k == SDLK_F2)  idx = 1;
    else if (k == SDLK_F3)  idx = 2;
    else if (k == SDLK_F4)  idx = 3;
    else if (k == SDLK_F5)  idx = 4;
    else if (k == SDLK_F6)  idx = 5;
    else if (k == SDLK_F7)  idx = 6;
    else if (k == SDLK_F8)  idx = 7;
    else if (k == SDLK_F9)  idx = 8;
    else if (k == SDLK_F10) idx = 9;
    else if (k == SDLK_F11) idx = 10; /* reversed-SELECTION (synthesized back-out) */
    if (idx < 0) return;
    if (idx >= recvx_msa_sample_count()) {
        RX_LOG("backend_gl", "audition F%d: only %d samples loaded",
               idx + 1, recvx_msa_sample_count());
        return;
    }
    recvx_msa_play_sample(idx, 100);
}

static bool gl_pump(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) g_quit = true;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) g_quit = true;
        if (ev.type == SDL_WINDOWEVENT &&
            ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            g_win_w = ev.window.data1;
            g_win_h = ev.window.data2;
        }
        if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            handle_sample_audition(ev.key.keysym.sym);
        }
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            recvx_key k;
            if (!ev.key.repeat && map_sdl_key(ev.key.keysym.sym, &k)) {
                recvx_input_set_key(k, ev.type == SDL_KEYDOWN);
            }
        }
    }
    return !g_quit;
}

static void gl_draw_rgba(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return;
    if (!g_fmv_tex) {
        glGenTextures(1, &g_fmv_tex);
        glBindTexture(GL_TEXTURE_2D, g_fmv_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, g_fmv_tex);
    if (w != g_fmv_w || h != g_fmv_h || !g_fmv_valid) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        g_fmv_w = w; g_fmv_h = h; g_fmv_valid = true;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    /* DIAGNOSTIC: draw solid red on screen LEFT half (untextured), FMV
     * textured quad in RIGHT half only. Symptoms:
     *   - red on left + FMV on right → fixed
     *   - red on left + black on right → texture upload/sampling is the bug
     *   - all black → rendering pipeline broken (state corruption?) */
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f( 0.0f, -1.0f);
    glVertex2f( 0.0f,  1.0f);
    glVertex2f(-1.0f,  1.0f);
    glEnd();

    /* Modulate texture by white so RGB passes through unchanged. */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(0, -1);     /* right-half only */
    glTexCoord2f(1, 1); glVertex2f(1, -1);
    glTexCoord2f(1, 0); glVertex2f(1,  1);
    glTexCoord2f(0, 0); glVertex2f(0,  1);
    glEnd();

    glDisable(GL_TEXTURE_2D);

    /* Catch GL errors from the upload/draw path, log once per occurrence
     * (GL only reports one error code at a time per driver state). */
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        static GLenum last_err = GL_NO_ERROR;
        if (err != last_err) {
            RX_LOG("backend_gl", "gl_draw_rgba GL error 0x%04x", err);
            last_err = err;
        }
    }
}

static void gl_audio_init(int sample_rate) {
    if (g_audio_dev && g_audio_rate == sample_rate) return;
    if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev = 0; }

    /* Query the default device's native rate. SDL's QueueAudio path on
     * WASAPI doesn't always honor a mismatched `want.freq`: it reports
     * our requested rate back through `have.freq` but the underlying
     * device runs at its own rate, so queued bytes play at the device's
     * effective rate (2× fast when device=96 kHz and want=48 kHz). The
     * fix is to open at the device's rate and do manual zero-order-hold
     * upsampling in gl_audio_queue. */
    SDL_AudioSpec default_spec = {0};
    int device_rate = sample_rate;
    if (SDL_GetDefaultAudioInfo(NULL, &default_spec, 0) == 0 &&
        default_spec.freq > 0) {
        device_rate = default_spec.freq;
        RX_LOG("backend_gl",
               "default device reports freq=%d, opening SDL at that rate",
               device_rate);
    }

    SDL_AudioSpec want = {0}, have = {0};
    want.freq     = device_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio_dev) {
        RX_LOG("backend_gl", "SDL_OpenAudioDevice: %s", SDL_GetError());
        return;
    }
    g_device_rate = have.freq;
    g_audio_rate  = sample_rate;
    RX_LOG("backend_gl",
           "SDL audio opened: device freq=%d ch=%d (source freq=%d)",
           have.freq, have.channels, sample_rate);

    /* We do the rate conversion ourselves via simple zero-order-hold
     * frame duplication — SDL_AudioStream produced garbage in this
     * setup and we want to eliminate opaque SDL state as a variable. */
    SDL_PauseAudioDevice(g_audio_dev, 0);
}

/* --------------------------------------------------------------------------
 * 2D gfx — textured / vertex-colored draw primitives for the nj* shims.
 *
 * Design: map PS2 screen-space (0..640, 0..480, Y down) to an orthographic
 * projection that covers the whole client area. Textures live in a flat
 * array keyed by pool slot (see game_texture_stubs.c). A single 2x2 white
 * fallback tex gets bound when a slot isn't populated yet, so draws from
 * not-yet-decoded TIM2 show as solid-color quads rather than disappearing.
 * -------------------------------------------------------------------------- */
#define RX_GFX_TEX_SLOTS 64
static GLuint g_gfx_tex[RX_GFX_TEX_SLOTS];
static int    g_gfx_tex_w[RX_GFX_TEX_SLOTS];
static int    g_gfx_tex_h[RX_GFX_TEX_SLOTS];
static GLuint g_gfx_white_tex;
/* Default to LINEAR. Source textures are 8-bit paletted (256 colors) on
 * PS2 — NEAREST sampling preserves the exact palette entry per output
 * pixel, which makes smooth gradients look like a 256-color JPEG
 * (visible banding on skin, wall gradients, fades). GL_LINEAR averages
 * 2×2 neighborhoods, producing intermediate values that never existed
 * in the 256-entry palette. Matches the PS2 GS default (MMAG/MMIN =
 * LINEAR), which is how the original CRT output looked smooth despite
 * the low-color-depth source assets.
 *
 * The game can still override per-list via njTextureFilterMode → recvx_gfx_set_filter. */
static int    g_gfx_filter = 1;
static int    g_gfx_ps2_w  = 640;
static int    g_gfx_ps2_h  = 480;

static void gfx_ensure_white(void) {
    if (g_gfx_white_tex) return;
    glGenTextures(1, &g_gfx_white_tex);
    glBindTexture(GL_TEXTURE_2D, g_gfx_white_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    const uint32_t white[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu,
                                0xFFFFFFFFu, 0xFFFFFFFFu };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, white);
}

/* --------------------------------------------------------------------------
 * Z-sorted command queue.
 *
 * The decomp issues draws in logical code order, not render order — on PS2
 * the GS display list gets z-sorted at flush time. Simplest match on our
 * side: buffer every draw in begin_2d/end_2d bracket, sort ascending by z
 * (smaller z = farther back → drawn first), flush on end_2d. Stable tie-
 * break on submission index keeps shadow+main pairs in correct order when
 * they share a z (shadow is submitted at z-0.001 so usually not tied).
 *
 * Concrete case that motivated this: DisplayGameModePlate (adv.c:1660)
 * draws menu text at z=0.5 THEN DisplayTitleBg at z=0.010. Submission
 * order paints BG on top of text. PS2 z-sorts, so text wins. We now do too.
 * -------------------------------------------------------------------------- */
enum { RX_GFX_CMD_QUAD = 0, RX_GFX_CMD_POLY = 1 };
#define RX_GFX_MAX_CMDS      1024
#define RX_GFX_MAX_POLY_VTX  4096

typedef struct {
    int      slot;
    float    x1, y1, x2, y2, u1, v1, u2, v2;
    uint32_t color;
    int      trans;
} rx_quad_cmd;

typedef struct {
    int      first;   /* index into g_poly_verts */
    int      count;
    int      trans;
} rx_poly_cmd;

typedef struct {
    float    z;
    uint16_t seq;     /* submission-order tiebreaker */
    uint8_t  kind;
    uint8_t  pad;
    union { rx_quad_cmd quad; rx_poly_cmd poly; } u;
} rx_cmd;

static rx_cmd        g_cmds[RX_GFX_MAX_CMDS];
static int           g_ncmds;
static recvx_gfx_vtx g_poly_verts[RX_GFX_MAX_POLY_VTX];
static int           g_poly_nverts;

static int cmp_cmd_by_z(const void* a, const void* b) {
    const rx_cmd* ca = (const rx_cmd*)a;
    const rx_cmd* cb = (const rx_cmd*)b;
    if (ca->z < cb->z) return -1;
    if (ca->z > cb->z) return  1;
    return (int)ca->seq - (int)cb->seq;
}

/* Expand ARGB32 (A in top byte) to four floats 0..1. PS2 color masks are
 * mostly in the same order as ARGB; confirm visually on first draw. */
static void gfx_unpack_argb(uint32_t c, float out[4]) {
    out[3] = ((c >> 24) & 0xFF) / 255.0f;  /* A */
    out[0] = ((c >> 16) & 0xFF) / 255.0f;  /* R */
    out[1] = ((c >>  8) & 0xFF) / 255.0f;  /* G */
    out[2] = ( c        & 0xFF) / 255.0f;  /* B */
}

static void gfx_flush_quad_now(const rx_quad_cmd* q) {
    GLuint tex = (q->slot >= 0 && q->slot < RX_GFX_TEX_SLOTS && g_gfx_tex[q->slot])
                     ? g_gfx_tex[q->slot]
                     : g_gfx_white_tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    if (q->trans) glEnable(GL_BLEND);
    else          glDisable(GL_BLEND);

    float c[4]; gfx_unpack_argb(q->color, c);
    glColor4f(c[0], c[1], c[2], c[3]);

    glBegin(GL_QUADS);
    glTexCoord2f(q->u1, q->v1); glVertex2f(q->x1, q->y1);
    glTexCoord2f(q->u2, q->v1); glVertex2f(q->x2, q->y1);
    glTexCoord2f(q->u2, q->v2); glVertex2f(q->x2, q->y2);
    glTexCoord2f(q->u1, q->v2); glVertex2f(q->x1, q->y2);
    glEnd();
}

static void gfx_flush_poly_now(const rx_poly_cmd* p) {
    if (p->trans) glEnable(GL_BLEND);
    else          glDisable(GL_BLEND);

    gfx_ensure_white();
    glBindTexture(GL_TEXTURE_2D, g_gfx_white_tex);

    /* TRIANGLE_STRIP, not fan. The PS2 GS PRIM register in njDrawPolygon
     * encodes prim=4 (TRIANGLESTRIP) with IIP+ABE set. The decomp emits
     * quad vertices in zigzag order: poly[0..3] = TL, BL, TR, BR (from
     * the idiomatic `poly[0].x=poly[1].x=left; poly[2].x=poly[3].x=right`
     * initializer in AdvDrawFadePolygon / AdvEasyDrawWindow / etc.).
     * TRIANGLE_STRIP on that order yields (TL,BL,TR) + (BL,TR,BR) — a
     * complete quad. TRIANGLE_FAN on the same order yields (TL,BL,TR) +
     * (TL,TR,BR) — upper-left + upper-right triangles that overlap at
     * the top and leave BL uncovered; full-screen fades then render as
     * four translucent wedges meeting at screen center. */
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < p->count; ++i) {
        const recvx_gfx_vtx* v = &g_poly_verts[p->first + i];
        float c[4]; gfx_unpack_argb(v->color, c);
        glColor4f(c[0], c[1], c[2], c[3]);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(v->x, v->y);
    }
    glEnd();
}

void recvx_gfx_begin_2d(int target_w, int target_h) {
    if (target_w > 0 && target_h > 0) {
        g_gfx_ps2_w = target_w;
        g_gfx_ps2_h = target_h;
    }
    glViewport(0, 0, g_win_w, g_win_h);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    /* Top-left origin, Y grows down. Matches PS2 SetQuadPos convention.
     * Using glVertex2f (no z) — depth is irrelevant now that end_2d flushes
     * quads in z-sorted submission order. */
    glOrtho(0.0, (double)g_gfx_ps2_w,
            (double)g_gfx_ps2_h, 0.0,
            -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    gfx_ensure_white();

    g_ncmds       = 0;
    g_poly_nverts = 0;
}

void recvx_gfx_end_2d(void) {
    if (g_ncmds > 0) {
        qsort(g_cmds, (size_t)g_ncmds, sizeof(rx_cmd), cmp_cmd_by_z);
        for (int i = 0; i < g_ncmds; ++i) {
            const rx_cmd* c = &g_cmds[i];
            if (c->kind == RX_GFX_CMD_QUAD) gfx_flush_quad_now(&c->u.quad);
            else                            gfx_flush_poly_now(&c->u.poly);
        }
    }
    g_ncmds       = 0;
    g_poly_nverts = 0;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void recvx_gfx_tex_upload(int slot, const void* rgba, int w, int h) {
    if (slot < 0 || slot >= RX_GFX_TEX_SLOTS) return;
    if (!rgba || w <= 0 || h <= 0) return;
    if (!g_gfx_tex[slot]) {
        glGenTextures(1, &g_gfx_tex[slot]);
        glBindTexture(GL_TEXTURE_2D, g_gfx_tex[slot]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        g_gfx_filter ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        g_gfx_filter ? GL_LINEAR : GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, g_gfx_tex[slot]);
    if (w != g_gfx_tex_w[slot] || h != g_gfx_tex_h[slot]) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        g_gfx_tex_w[slot] = w;
        g_gfx_tex_h[slot] = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
}

void recvx_gfx_draw_quad(int slot,
                         float x1, float y1, float x2, float y2,
                         float u1, float v1, float u2, float v2,
                         float z, uint32_t color, int trans) {
    if (g_ncmds >= RX_GFX_MAX_CMDS) return;
    rx_cmd* c = &g_cmds[g_ncmds];
    c->z             = z;
    c->seq           = (uint16_t)g_ncmds;
    c->kind          = RX_GFX_CMD_QUAD;
    c->u.quad.slot   = slot;
    c->u.quad.x1     = x1; c->u.quad.y1 = y1;
    c->u.quad.x2     = x2; c->u.quad.y2 = y2;
    c->u.quad.u1     = u1; c->u.quad.v1 = v1;
    c->u.quad.u2     = u2; c->u.quad.v2 = v2;
    c->u.quad.color  = color;
    c->u.quad.trans  = trans;
    g_ncmds++;
}

void recvx_gfx_draw_polygon(const recvx_gfx_vtx* verts, int count, int trans) {
    if (!verts || count < 3) return;
    if (g_ncmds >= RX_GFX_MAX_CMDS) return;
    if (g_poly_nverts + count > RX_GFX_MAX_POLY_VTX) return;

    /* Representative z: first vertex. Polygons in this engine are 2D
     * overlays so all verts share z anyway (AdvDrawFadePolygon, etc.). */
    float z = verts[0].z;

    rx_cmd* c = &g_cmds[g_ncmds];
    c->z            = z;
    c->seq          = (uint16_t)g_ncmds;
    c->kind         = RX_GFX_CMD_POLY;
    c->u.poly.first = g_poly_nverts;
    c->u.poly.count = count;
    c->u.poly.trans = trans;
    g_ncmds++;

    for (int i = 0; i < count; ++i) g_poly_verts[g_poly_nverts + i] = verts[i];
    g_poly_nverts += count;
}

void recvx_gfx_set_filter(int mode) {
    g_gfx_filter = mode ? 1 : 0;
    /* Apply to all existing textures immediately. */
    for (int i = 0; i < RX_GFX_TEX_SLOTS; ++i) {
        if (!g_gfx_tex[i]) continue;
        glBindTexture(GL_TEXTURE_2D, g_gfx_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        g_gfx_filter ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        g_gfx_filter ? GL_LINEAR : GL_NEAREST);
    }
}

static void gl_audio_queue(const void* samples, int byte_count) {
    if (!g_audio_dev || !samples || byte_count <= 0) return;
    if (g_device_rate == g_audio_rate || g_audio_rate <= 0) {
        SDL_QueueAudio(g_audio_dev, samples, (Uint32)byte_count);
        return;
    }
    int ratio = g_device_rate / g_audio_rate;
    if (ratio < 1) ratio = 1;

    /* Byte counts coming in can be odd (PSS packets emit 4033 / 4073
     * bytes). Accumulate leftover bytes across calls so we always
     * process complete 4-byte stereo frames without dropping or
     * duplicating any source byte. */
    int combined_len = g_audio_carry_len + byte_count;
    uint8_t* combined = (uint8_t*)SDL_malloc((size_t)combined_len);
    if (!combined) return;
    memcpy(combined, g_audio_carry, (size_t)g_audio_carry_len);
    memcpy(combined + g_audio_carry_len, samples, (size_t)byte_count);

    int process_bytes = combined_len & ~3;
    int leftover      = combined_len - process_bytes;
    if (process_bytes > 0) {
        int frames    = process_bytes / 4;
        const int16_t* src = (const int16_t*)combined;

        if (ratio == 2) {
            /* Linear interpolation with 1-frame deferred lookahead.
             * Bounded output (midpoint is always between its two
             * source samples), so no overshoot → no peak clipping at
             * loud transients. Aliasing rejection is weaker than
             * cubic's but still halves ZOH imaging, and doesn't add
             * the ringing/clipping artifacts a 4-tap kernel can
             * introduce on digital audio near full scale. */
            int have_prev_int = g_audio_has_prev ? 1 : 0;
            int work_count    = frames + have_prev_int;
            int process_pairs = work_count - 1;
            if (process_pairs > 0) {
                int16_t* out = (int16_t*)SDL_malloc((size_t)(process_pairs * 8));
                if (out) {
                    for (int i = 0; i < process_pairs; ++i) {
                        int16_t L, R, Ln, Rn;
                        int a = i, b = i + 1;
                        if (have_prev_int && a == 0) {
                            L = g_audio_prev_L; R = g_audio_prev_R;
                        } else {
                            int si = a - have_prev_int;
                            L = src[si*2]; R = src[si*2+1];
                        }
                        if (have_prev_int && b == 0) {
                            Ln = g_audio_prev_L; Rn = g_audio_prev_R;
                        } else {
                            int si = b - have_prev_int;
                            Ln = src[si*2]; Rn = src[si*2+1];
                        }
                        int16_t Lm = (int16_t)(((int32_t)L + (int32_t)Ln) / 2);
                        int16_t Rm = (int16_t)(((int32_t)R + (int32_t)Rn) / 2);
                        out[i*4]   = L;
                        out[i*4+1] = R;
                        out[i*4+2] = Lm;
                        out[i*4+3] = Rm;
                    }
                    SDL_QueueAudio(g_audio_dev, out, (Uint32)(process_pairs * 8));
                    SDL_free(out);
                }
            }
            if (frames > 0) {
                g_audio_prev_L   = src[(frames-1)*2];
                g_audio_prev_R   = src[(frames-1)*2+1];
                g_audio_has_prev = true;
            }
        } else {
            /* ZOH fallback for uncommon ratios. */
            int out_bytes = process_bytes * ratio;
            uint8_t* out = (uint8_t*)SDL_malloc((size_t)out_bytes);
            if (out) {
                const uint32_t* src32 = (const uint32_t*)combined;
                uint32_t* dst = (uint32_t*)out;
                for (int i = 0; i < frames; ++i) {
                    for (int k = 0; k < ratio; ++k) {
                        dst[i * ratio + k] = src32[i];
                    }
                }
                SDL_QueueAudio(g_audio_dev, out, (Uint32)out_bytes);
                SDL_free(out);
            }
        }
    }
    memcpy(g_audio_carry, combined + process_bytes, (size_t)leftover);
    g_audio_carry_len = leftover;
    SDL_free(combined);
}

/* Frame pacer — PS2 NTSC runs at 59.94 Hz and the game's tick rate is
 * baked into every Mode's anim / fade / cursor-repeat counters. SDL vsync
 * alone is not enough: a 144 Hz monitor still lets njUserMain fire 144x
 * per second. We therefore sleep/spin until the target_hz budget elapses.
 * Sleep the bulk then busy-wait the last 2ms so we hit the edge precisely
 * without burning a full CPU core. */
void recvx_backend_pace(int target_hz) {
    static Uint64 last    = 0;
    static Uint64 freq    = 0;
    if (!freq) freq = SDL_GetPerformanceFrequency();
    if (target_hz <= 0) { last = SDL_GetPerformanceCounter(); return; }

    const Uint64 budget = freq / (Uint64)target_hz;
    Uint64 now = SDL_GetPerformanceCounter();
    if (last) {
        Uint64 elapsed = now - last;
        if (elapsed < budget) {
            Uint64 remain  = budget - elapsed;
            Uint64 spin_lo = (freq * 2) / 1000;          /* last 2ms spin */
            if (remain > spin_lo) {
                Uint32 sleep_ms = (Uint32)(((remain - spin_lo) * 1000) / freq);
                if (sleep_ms) SDL_Delay(sleep_ms);
            }
            while ((SDL_GetPerformanceCounter() - last) < budget) { /* spin */ }
        }
    }
    last = SDL_GetPerformanceCounter();
}

static const recvx_backend g_gl = {
    .name        = "sdl2_gl_compat",
    .init        = gl_init,
    .shutdown    = gl_shutdown,
    .begin_frame = gl_begin,
    .end_frame   = gl_end,
    .pump_events = gl_pump,
    .draw_rgba   = gl_draw_rgba,
    .audio_init  = gl_audio_init,
    .audio_queue = gl_audio_queue,
};

const recvx_backend* recvx_backend_gl(void) { return &g_gl; }

#else /* SDL2/GL not available — fall back to null */

const recvx_backend* recvx_backend_gl(void) { return NULL; }

/* Headless stubs so recvx_gfx_* always resolves at link time. */
void recvx_gfx_begin_2d(int w, int h)                 { (void)w; (void)h; }
void recvx_gfx_end_2d(void)                           {}
void recvx_gfx_tex_upload(int s, const void* p, int w, int h)
                                                      { (void)s; (void)p; (void)w; (void)h; }
void recvx_gfx_draw_quad(int s, float x1, float y1, float x2, float y2,
                         float u1, float v1, float u2, float v2,
                         float z, uint32_t c, int t) {
    (void)s; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)u1; (void)v1; (void)u2; (void)v2; (void)z; (void)c; (void)t;
}
void recvx_gfx_draw_polygon(const recvx_gfx_vtx* v, int n, int t)
                                                      { (void)v; (void)n; (void)t; }
void recvx_gfx_set_filter(int m)                      { (void)m; }
void recvx_backend_pace(int hz)                       { (void)hz; }

#endif
