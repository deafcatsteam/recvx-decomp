/*
 * SDL2 + OpenGL 3.3 core backend.
 *
 * Phase 0: only opens a window, clears, and swaps. Phase 1 will grow the
 * sprite/3D paths the Ninja shim needs.
 *
 * This file compiles to a stub when SDL2/GL aren't available so the build
 * still succeeds on a bare machine.
 */

#include "recvx_port.h"

#if defined(RECVX_HAVE_SDL2) && defined(RECVX_HAVE_GL)

#include <SDL.h>
#include <SDL_opengl.h>

static SDL_Window*    g_window;
static SDL_GLContext  g_glctx;
static bool           g_quit;

static int gl_init(const recvx_backend_config* cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        RX_LOG("backend_gl", "SDL_Init: %s", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (cfg->fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

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
    if (g_glctx) { SDL_GL_DeleteContext(g_glctx); g_glctx = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
    SDL_Quit();
}

static void gl_begin(void) {
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
    }
    return !g_quit;
}

static const recvx_backend g_gl = {
    .name        = "sdl2_gl33",
    .init        = gl_init,
    .shutdown    = gl_shutdown,
    .begin_frame = gl_begin,
    .end_frame   = gl_end,
    .pump_events = gl_pump,
};

const recvx_backend* recvx_backend_gl(void) { return &g_gl; }

#else /* SDL2/GL not available — fall back to null */

const recvx_backend* recvx_backend_gl(void) { return NULL; }

#endif
