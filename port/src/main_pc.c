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

/* Defined in port/src/afs/afs_mount.c. Must run AFTER njUserInit since
 * MountSoundAfs wants sys->sys_partid / itm_partid / dor_partid writable
 * and njUserInit allocates / zeroes the SYS_WORK block. */
extern int  MountSoundAfs(void);
extern void recvx_set_gamedata_dir(const char* dir);

/* Defined in adv.c. Normally called from sdfunc.c SoundSetup (which we
 * don't compile). Must run AFTER MountSoundAfs so PatId[3] is valid
 * when MountAdvAfs latches it into AdvWork.PatId — otherwise the first
 * Adv_FirstWarningMessage mode-4 call reads from partition 0
 * (BGM1.AFS) instead of partition 3 (ADV.AFS). Idempotent: the
 * AdvFirstInitFlag guard makes re-entry a no-op. */
extern void InitAdvSystem(void);

static uint32_t now_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static void audio_sink_to_backend(void* opaque, int rate,
                                  const void* pcm, int bytes) {
    const recvx_backend* b = (const recvx_backend*)opaque;
    if (b->audio_init)  b->audio_init(rate);
    if (b->audio_queue) b->audio_queue(pcm, bytes);
}

static const char* g_iso_path      = NULL;
static const char* g_gamedata_path = NULL;
static bool        g_run_game      = false;

static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            g_iso_path = argv[++i];
        } else if (strcmp(argv[i], "--gamedata") == 0 && i + 1 < argc) {
            g_gamedata_path = argv[++i];
        } else if (strcmp(argv[i], "--game") == 0) {
            g_run_game = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: recvx_pc [--iso path\\to\\recvx.iso]\n"
                   "                [--gamedata path\\to\\extracted\\dir]\n"
                   "                [--game]\n"
                   "  --game      run njUserInit/njUserMain task loop instead of FMV demo\n"
                   "  --gamedata  dir containing SYSTEM.AFS / ADV.AFS / ... for real file I/O\n");
            exit(0);
        }
    }
    if (!g_iso_path)      g_iso_path      = "recvx.iso";
    if (!g_gamedata_path) g_gamedata_path = "C:\\Claude\\codeveronica\\gamedata";
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

/* SYS_WORK* sys from decomp main.c. Offsets listed in types.h are for
 * the PS2 32-bit ABI; on MSVC x64 the single `void* typ_exp` at 0x50
 * grows from 4 to 8 bytes, shifting every subsequent field by +4.
 *
 *   types.h says: tk_flg @ 0x54, ts_flg @ 0x58
 *   PC x64 real:  tk_flg @ 0x58, ts_flg @ 0x5C
 *
 * First crash log read 0x54/0x58 and saw tk_flg=0 ts_flg=0x300002 —
 * the "tk_flg" slot had been looking at the low 4 bytes of typ_exp. */
extern void* sys;
#define SYS_TK_FLG(s) (*(uint32_t*)((char*)(s) + 0x58))
#define SYS_TS_FLG(s) (*(uint32_t*)((char*)(s) + 0x5C))

/* AdvWork is ADV_WORK in adv.c:27. Mode lives at offset 0x4 (adv.h).
 * Every field we care about is before any pointer-sized member (ptr[] is
 * at 0x40 but we only read Mode/Mode2 here), so no x64 ABI shift. Declare
 * as char[] so the linker binds the address without needing types.h. */
extern char AdvWork[];
#define ADV_MODE()  (*(int32_t*)(AdvWork + 0x04))
#define ADV_MODE2() (*(int32_t*)(AdvWork + 0x08))

static const char* task_name(int bit) {
    static const char* names[] = {
        "Init","Warning","Ipl","Firstmovie","Title","Opening","Pad","Game",
        "Event","Itemselect","Map","Doordemo","Movie","Ending","Gameover",
        "Typewriter","Option","CompEvent","DiscChange","SoundMuseum",
        "Monitor","SndMonitor","ScreenSaver"
    };
    return (bit >= 0 && bit < 23) ? names[bit] : "?";
}

static void log_task_flags(uint32_t tk, uint32_t ts) {
    char active[256] = {0};
    size_t used = 0;
    for (int i = 0; i < 23; ++i) {
        if (tk & (1u << i)) {
            int n = snprintf(active + used, sizeof(active) - used,
                             "%s%s", used ? "," : "", task_name(i));
            if (n > 0) used += (size_t)n;
        }
    }
    RX_LOG("game", "tk_flg=0x%08x ts_flg=0x%08x active=[%s]",
           tk, ts, active);
}

/* PDD_DGT_* bit -> short label, mirrors port/src/input/input.c. Covers
 * only the digital buttons we actually pump from SDL. */
static const char* button_name(int bit) {
    switch (bit) {
        case 0:  return "Sq";    /* TC */
        case 1:  return "Ci";    /* TB */
        case 2:  return "Cr";    /* TA */
        case 3:  return "St";    /* ST */
        case 4:  return "U";     /* KU */
        case 5:  return "D";     /* KD */
        case 6:  return "L";     /* KL */
        case 7:  return "R";     /* KR */
        case 8:  return "L2";    /* TZ */
        case 9:  return "Tri";   /* TY */
        case 10: return "R2";    /* TX */
        case 11: return "TD";
        case 16: return "R1";    /* TR */
        case 17: return "L1";    /* TL */
        default: return "?";
    }
}

/* Dump a bitmap diff (old->new) as "Down U,Cr  Up Ci". Logs only when
 * anything changed so held keys stay quiet. */
static void log_input_edges(uint32_t prev, uint32_t cur) {
    uint32_t down = cur  & ~prev;
    uint32_t up   = prev & ~cur;
    if (!down && !up) return;
    char line[256] = {0};
    size_t used = 0;
    if (down) {
        int n = snprintf(line + used, sizeof(line) - used, "Down ");
        if (n > 0) used += (size_t)n;
        int first = 1;
        for (int i = 0; i < 32; ++i) if (down & (1u << i)) {
            n = snprintf(line + used, sizeof(line) - used,
                         "%s%s", first ? "" : ",", button_name(i));
            if (n > 0) used += (size_t)n;
            first = 0;
        }
    }
    if (up) {
        int n = snprintf(line + used, sizeof(line) - used,
                         "%sUp ", down ? "  " : "");
        if (n > 0) used += (size_t)n;
        int first = 1;
        for (int i = 0; i < 32; ++i) if (up & (1u << i)) {
            n = snprintf(line + used, sizeof(line) - used,
                         "%s%s", first ? "" : ",", button_name(i));
            if (n > 0) used += (size_t)n;
            first = 0;
        }
    }
    RX_LOG("input", "%s (on=0x%08x)", line, cur);
}

static int run_game_loop(const recvx_backend* backend) {
    RX_LOG("game", "calling njUserInit...");
    njUserInit();
    RX_LOG("game", "njUserInit returned");

    RX_LOG("game", "gamedata dir: %s", g_gamedata_path);
    recvx_set_gamedata_dir(g_gamedata_path);
    if (MountSoundAfs() != 0) {
        RX_LOG("game", "WARN: MountSoundAfs failed — boot chain will stall");
    }
    InitAdvSystem();
    RX_LOG("game", "InitAdvSystem done (AdvWork.PatId latched to PatId[3]=%d)", 3);

    RX_LOG("game", "entering njUserMain loop");

    uint32_t prev_tk   = 0xFFFFFFFF;
    uint32_t prev_ts   = 0xFFFFFFFF;
    uint32_t prev_btn  = 0;
    int32_t  prev_adv  = INT32_MIN;
    int32_t  prev_adv2 = INT32_MIN;
    int frame = 0;
    while (backend->pump_events()) {
        backend->begin_frame();
        /* Set up PS2 2D ortho (640x480, Y-down) once per frame so every
         * njDrawPolygon / njDrawQuadTexture the game issues lands in the
         * right screen-space. Game coords match PS2 SetQuadPos convention. */
        recvx_gfx_begin_2d(640, 480);
        recvx_input_new_frame();

        /* Sample AdvWork.Mode BEFORE njUserMain so we see the mode that
         * just crashed (if it does) in the log, then sample AFTER to
         * catch transitions. */
        int32_t adv_before  = ADV_MODE();
        int32_t adv2_before = ADV_MODE2();
        if (adv_before != prev_adv || adv2_before != prev_adv2) {
            RX_LOG("adv", "frame %d: AdvWork.Mode=%d Mode2=%d (was %d/%d)",
                   frame, adv_before, adv2_before, prev_adv, prev_adv2);
            prev_adv  = adv_before;
            prev_adv2 = adv2_before;
        }

        njUserMain();
        recvx_gfx_end_2d();
        backend->end_frame();

        /* Also check after the tick so single-tick mode transitions
         * (e.g. mode 3 falls through to 4 without break at adv.c:1228)
         * are visible as "3 -> 4" on the next frame. */
        int32_t adv_after  = ADV_MODE();
        int32_t adv2_after = ADV_MODE2();
        if (adv_after != prev_adv || adv2_after != prev_adv2) {
            RX_LOG("adv", "frame %d (post): AdvWork.Mode=%d Mode2=%d (was %d/%d)",
                   frame, adv_after, adv2_after, prev_adv, prev_adv2);
            prev_adv  = adv_after;
            prev_adv2 = adv2_after;
        }

        uint32_t tk = SYS_TK_FLG(sys);
        uint32_t ts = SYS_TS_FLG(sys);
        if (tk != prev_tk || ts != prev_ts) {
            RX_LOG("game", "frame %d: task flags changed", frame);
            log_task_flags(tk, ts);
            prev_tk = tk;
            prev_ts = ts;
        }

        uint32_t btn = recvx_input_buttons();
        if (btn != prev_btn) {
            log_input_edges(prev_btn, btn);
            prev_btn = btn;
        }
        ++frame;
    }

    njUserExit();
    return 0;
}

int main(int argc, char** argv) {
    parse_args(argc, argv);
    RX_LOG("boot", "RECVX PC port — phase 5 scaffold");
    RX_LOG("boot", "ISO path: %s", g_iso_path);
    RX_LOG("boot", "gamedata: %s", g_gamedata_path);
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
