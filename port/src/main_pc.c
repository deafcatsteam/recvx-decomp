/*
 * RECVX PC port — Phase 0 entry point.
 *
 * Opens a window via the chosen backend and runs an empty frame loop.
 * Phase 1 replaces this loop with calls into njUserInit / njUserMain /
 * njUserExit from the decomp source.
 */

#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Wall-clock milliseconds since program start. clock() on MSVC is
 * wall time, which is what we want for FMV pacing; *nix would need a
 * clock_gettime(MONOTONIC) replacement but this file is Windows-only. */
static uint32_t now_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static const char* g_iso_path = NULL;

static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            g_iso_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: recvx_pc [--iso path\\to\\recvx.iso]\n");
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

int main(int argc, char** argv) {
    parse_args(argc, argv);
    RX_LOG("boot", "RECVX PC port — phase 0 skeleton");
    RX_LOG("boot", "ISO path: %s", g_iso_path);

    recvx_iso_t* iso = recvx_iso_open(g_iso_path);
    if (!iso) {
        RX_LOG("boot", "WARN: could not open ISO. Continuing in dry-run mode.");
    } else {
        recvx_iso_set_global(iso);
        /* Sanity probe — RECVX always ships SYSTEM.CNF at root and a
         * MOVIE directory. Prove the directory walk resolves both. */
        const char* probes[] = {
            "\\SYSTEM.CNF;1", "\\SLUS_201.84;1",
            "\\SYSTEM.AFS;1", "\\MOVIE\\MV_000.PSS;1"
        };
        for (int i = 0; i < (int)(sizeof(probes)/sizeof(probes[0])); ++i) {
            uint32_t lba = 0, sz = 0;
            if (recvx_iso_find(iso, probes[i], &lba, &sz) == 0) {
                RX_LOG("iso", "probe %s -> lsn=%u size=%u", probes[i], lba, sz);
            } else {
                RX_LOG("iso", "probe %s NOT FOUND", probes[i]);
            }
        }
    }

    const recvx_backend* backend = recvx_backend_gl();
    if (!backend) {
        backend = recvx_backend_null();
    }

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

    /* Phase 4c: FMV playback with PTS-based pacing. Advance the decoder
     * only when wall-clock catches up to the next frame's PTS, so a
     * 240Hz display no longer runs the movie at ~8x. */
    recvx_fmv_t* fmv = NULL;
    uint32_t     fmv_start_ms = 0;
    if (iso) {
        fmv = recvx_fmv_open("\\MOVIE\\MV_000.PSS;1");
        if (fmv) {
            /* Prime the first frame before starting the clock so PTS 0
             * aligns with the first drawn frame, not the open() return. */
            if (!recvx_fmv_advance(fmv)) {
                recvx_fmv_close(fmv);
                fmv = NULL;
            } else {
                fmv_start_ms = now_ms();
            }
        }
    }

    while (backend->pump_events()) {
        backend->begin_frame();
        if (fmv) {
            double elapsed_s = (double)(now_ms() - fmv_start_ms) / 1000.0;
            /* Catch up: decode until the buffered frame's PTS is at or
             * past the wall clock, then hold it until the next tick. */
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

    backend->shutdown();
    if (iso) recvx_iso_close(iso);
    RX_LOG("boot", "clean exit");
    return 0;
}
