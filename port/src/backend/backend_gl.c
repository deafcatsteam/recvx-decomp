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

static SDL_Window*    g_window;
static SDL_GLContext  g_glctx;
static bool           g_quit;
static int            g_win_w, g_win_h;

static GLuint         g_fmv_tex;
static int            g_fmv_w, g_fmv_h;
static bool           g_fmv_valid;

static SDL_AudioDeviceID g_audio_dev;
static int               g_audio_rate;

static int gl_init(const recvx_backend_config* cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        RX_LOG("backend_gl", "SDL_Init: %s", SDL_GetError());
        return -1;
    }
    /* Compat profile — see file header. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
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
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1,  1);
    glTexCoord2f(0, 0); glVertex2f(-1,  1);
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

static void gl_audio_init(int sample_rate) {
    if (g_audio_dev && g_audio_rate == sample_rate) return;
    if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev = 0; }
    SDL_AudioSpec want = {0}, have = {0};
    want.freq     = sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio_dev) {
        RX_LOG("backend_gl", "SDL_OpenAudioDevice: %s", SDL_GetError());
        return;
    }
    g_audio_rate = have.freq;
    RX_LOG("backend_gl",
           "SDL audio opened: freq=%d ch=%d fmt=0x%04x silence=%d samples=%d (wanted freq=%d ch=%d)",
           have.freq, have.channels, have.format, have.silence,
           have.samples, want.freq, want.channels);
    SDL_PauseAudioDevice(g_audio_dev, 0);
}

static void gl_audio_queue(const void* samples, int byte_count) {
    if (!g_audio_dev || !samples || byte_count <= 0) return;
    SDL_QueueAudio(g_audio_dev, samples, (Uint32)byte_count);
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

#endif
