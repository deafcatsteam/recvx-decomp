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

    /* Phase 4a smoke test: open the first FMV and decode a few frames
     * to confirm the ISO-backed AVIOContext + MPEG-PS demux + MPEG2 /
     * ADPCM decode pipeline holds together. Frames go into RAM only;
     * GL blit wiring comes in phase 4b. */
    if (iso) {
        recvx_fmv_t* fmv = recvx_fmv_open("\\MOVIE\\MV_000.PSS;1");
        if (fmv) {
            int decoded = 0;
            while (decoded < 5 && recvx_fmv_advance(fmv)) ++decoded;
            RX_LOG("fmv", "smoke test: decoded %d frames", decoded);
            recvx_fmv_close(fmv);
        }
    }

    while (backend->pump_events()) {
        backend->begin_frame();
        /* TODO(phase4b): draw current FMV frame as fullscreen quad. */
        backend->end_frame();
    }

    backend->shutdown();
    if (iso) recvx_iso_close(iso);
    RX_LOG("boot", "clean exit");
    return 0;
}
