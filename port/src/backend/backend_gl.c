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
