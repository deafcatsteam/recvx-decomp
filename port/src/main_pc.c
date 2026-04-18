/*
 * RECVX PC port — phase 5 entry point.
 *
 * Two run modes:
 *   default     — plays MV_000.PSS from the ISO (phase 4 FMV pipeline).
 *   --game      — calls njUserInit / njUserMain from the decomp so the
 *                 game's task state machine (bhSysTaskJumpTab) drives
 *                 the frame, with input pumped via recvx_input_*.
 *
 * The --game path will crash / misrender until more of the decomp is
 * brought in (adv.c + textures + fonts). It's a scaffold so we can
 * iterate on missing symbols without losing the working FMV demo.
 */

#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* From the decomp — declared here to avoid pulling KATANA headers into
 * the port target. Signatures match ninjapad.h. */
extern void         njUserInit(void);
extern int32_t      njUserMain(void);
extern void         njUserExit(void);

static uint32_t now_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static void audio_sink_to_backend(void* opaque, int rate,
                                  const void* pcm, int bytes) {
    const recvx_backend* b = (const recvx_backend*)opaque;
    if (b->audio_init)  b->audio_init(rate);
    if (b->audio_queue) b->audio_queue(pcm, bytes);
}

static const char* g_iso_path = NULL;
static bool        g_run_game = false;

static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            g_iso_path = argv[++i];
        } else if (strcmp(argv[i], "--game") == 0) {
            g_run_game = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: recvx_pc [--iso path\\to\\recvx.iso] [--game]\n"
                   "  --game   run njUserInit/njUserMain task loop instead of FMV demo\n");
            exit(0);
        }
    }
    if (!g_iso_path) {
        g_iso_path = "recvx.iso";
    }
}

#if defined(RECVX_HAVE_SDL2) && defined(_WIN32)
/* SDL2 main shim provides WinMain, we just write main() */
#endif

static int run_fmv_demo(const recvx_backend* backend, recvx_iso_t* iso) {
    recvx_fmv_t* fmv = NULL;
    uint32_t     fmv_start_ms = 0;
    if (iso) {
        fmv = recvx_fmv_open("\\MOVIE\\MV_000.PSS;1");
        if (fmv) {
            recvx_fmv_set_audio_sink(fmv, audio_sink_to_backend,
                                     (void*)backend);
        }
        if (fmv && !recvx_fmv_advance(fmv)) {
            recvx_fmv_close(fmv);
            fmv = NULL;
        } else if (fmv) {
            fmv_start_ms = now_ms();
        }
    }

    while (backend->pump_events()) {
        backend->begin_frame();
        if (fmv) {
            double elapsed_s = (double)(now_ms() - fmv_start_ms) / 1000.0;
            while (recvx_fmv_pts_s(fmv) < elapsed_s) {
                if (!recvx_fmv_advance(fmv)) {
                    recvx_fmv_close(fmv);
                    fmv = NULL;
                    break;
                }
            }
            if (fmv && backend->draw_rgba) {
                backend->draw_rgba(recvx_fmv_pixels(fmv),
                                   recvx_fmv_width(fmv),
                                   recvx_fmv_height(fmv));
            }
        }
        backend->end_frame();
    }
    if (fmv) recvx_fmv_close(fmv);
    return 0;
}

static int run_game_loop(const recvx_backend* backend) {
    RX_LOG("game", "calling njUserInit...");
    njUserInit();
    RX_LOG("game", "njUserInit returned; entering njUserMain loop");

    while (backend->pump_events()) {
        backend->begin_frame();
        recvx_input_new_frame();
        njUserMain();
        backend->end_frame();
    }

    njUserExit();
    return 0;
}

int main(int argc, char** argv) {
    parse_args(argc, argv);
    RX_LOG("boot", "RECVX PC port — phase 5 scaffold");
    RX_LOG("boot", "ISO path: %s", g_iso_path);
    RX_LOG("boot", "mode: %s", g_run_game ? "game" : "fmv-demo");

    recvx_iso_t* iso = recvx_iso_open(g_iso_path);
    if (!iso) {
        RX_LOG("boot", "WARN: could not open ISO. Continuing in dry-run mode.");
    } else {
        recvx_iso_set_global(iso);
    }

    const recvx_backend* backend = recvx_backend_gl();
    if (!backend) backend = recvx_backend_null();

    recvx_backend_config cfg = {
        .width = 640,
        .height = 480,
        .fullscreen = false,
        .vsync = true,
        .window_title = "Resident Evil: Code Veronica X (PC port)",
    };
    if (backend->init(&cfg) != 0) {
        RX_LOG("boot", "ERR: backend %s failed to init", backend->name);
        return 1;
    }
    RX_LOG("boot", "backend: %s", backend->name);

    int rc = g_run_game ? run_game_loop(backend)
                        : run_fmv_demo(backend, iso);

    backend->shutdown();
    if (iso) recvx_iso_close(iso);
    RX_LOG("boot", "clean exit");
    return rc;
}
