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

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

/* Crash handler — captures the offending RIP plus a 16-frame stack trace
 * with symbol names so we can pinpoint where in the decomp/port the
 * unhandled exception fired. Output goes to the same recvx_log stream the
 * rest of the runtime log uses. */
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    RX_LOG("crash", "*** unhandled exception 0x%08lx at %p ***", code, addr);

    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR badaddr = ep->ExceptionRecord->ExceptionInformation[1];
        RX_LOG("crash", "  access violation: %s 0x%p",
               op == 0 ? "read"  :
               op == 1 ? "write" :
               op == 8 ? "exec"  : "?", (void*)badaddr);
    }

    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, NULL, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

    void* frames[16];
    USHORT n = CaptureStackBackTrace(0, 16, frames, NULL);
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 255;
    IMAGEHLP_LINE64 line; line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (USHORT i = 0; i < n; ++i) {
        DWORD64 disp = 0;
        const char* name = "?";
        if (SymFromAddr(proc, (DWORD64)frames[i], &disp, sym)) name = sym->Name;
        DWORD line_disp = 0;
        if (SymGetLineFromAddr64(proc, (DWORD64)frames[i], &line_disp, &line)) {
            RX_LOG("crash", "  [%2d] %p %s+0x%llx (%s:%lu)",
                   i, frames[i], name, (unsigned long long)disp,
                   line.FileName ? line.FileName : "?", line.LineNumber);
        } else {
            RX_LOG("crash", "  [%2d] %p %s+0x%llx",
                   i, frames[i], name, (unsigned long long)disp);
        }
    }
    SymCleanup(proc);

    /* fflush stdout/stderr so the runtime.log captures the crash report. */
    fflush(stdout);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;  /* terminate, don't pop the WER dialog */
}
#endif

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
static int         g_msa_rate      = 0;   /* 0 = use Smpl-derived rate */
static const char* g_se_remap_str  = NULL; /* "N:M[,N:M...]"; NULL → use msa_init defaults */

/* Parse "N:M,N:M,..." and call recvx_msa_set_remap for each pair. Applied
 * AFTER recvx_msa_init so CLI values overwrite baked defaults, and use -1
 * as sample_idx to explicitly clear an entry (back to identity). */
static void apply_se_remap(const char* spec) {
    if (!spec) return;
    const char* p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        char* endn = NULL;
        long se = strtol(p, &endn, 10);
        if (endn == p || *endn != ':') {
            RX_LOG("game", "bad --se-remap token at '%s' (expected N:M)", p);
            return;
        }
        p = endn + 1;
        long smp = strtol(p, &endn, 10);
        if (endn == p) {
            RX_LOG("game", "bad --se-remap token at '%s' (missing sample id)", p);
            return;
        }
        recvx_msa_set_remap((int)se, (int)smp);
        p = endn;
    }
}

static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            g_iso_path = argv[++i];
        } else if (strcmp(argv[i], "--gamedata") == 0 && i + 1 < argc) {
            g_gamedata_path = argv[++i];
        } else if (strcmp(argv[i], "--game") == 0) {
            g_run_game = true;
        } else if (strcmp(argv[i], "--msa-rate") == 0 && i + 1 < argc) {
            g_msa_rate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--se-remap") == 0 && i + 1 < argc) {
            g_se_remap_str = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: recvx_pc [--iso path\\to\\recvx.iso]\n"
                   "                [--gamedata path\\to\\extracted\\dir]\n"
                   "                [--game] [--msa-rate N]\n"
                   "                [--se-remap N:M[,N:M...]]\n"
                   "  --game      run njUserInit/njUserMain task loop instead of FMV demo\n"
                   "  --gamedata  dir containing SYSTEM.AFS / ADV.AFS / ... for real file I/O\n"
                   "  --msa-rate  override COMMON.MLT source sample rate in Hz\n"
                   "              (try 22050 / 24000 / 32000 / 44100 / 48000)\n"
                   "  --se-remap  remap SeNo→sample-index for COMMON.MLT menu SEs\n"
                   "              e.g. --se-remap 0:3,2:0,3:2   (sample_idx -1 = identity)\n");
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

/* scePad-shifted bit -> short label, mirrors port/src/input/input.c. */
static const char* button_name(int bit) {
    switch (bit) {
        case 0:  return "L2";
        case 1:  return "R2";
        case 2:  return "L1";
        case 3:  return "R1";
        case 4:  return "Tri";
        case 5:  return "Ci";
        case 6:  return "Cr";
        case 7:  return "Sq";
        case 8:  return "Sel";
        case 9:  return "L3";
        case 10: return "R3";
        case 11: return "St";
        case 12: return "U";
        case 13: return "R";
        case 14: return "D";
        case 15: return "L";
        default: return "?";
    }
}

/* Dump a bitmap diff (old->new) as "Press U,Cr  Release Ci". Logs only
 * when anything changed so held keys stay quiet. We use Press/Release
 * rather than Down/Up because "U" and "D" are our button names for the
 * Up/Down arrows, which made the edge labels ambiguous. */
static void log_input_edges(uint32_t prev, uint32_t cur) {
    uint32_t down = cur  & ~prev;
    uint32_t up   = prev & ~cur;
    if (!down && !up) return;
    char line[256] = {0};
    size_t used = 0;
    if (down) {
        int n = snprintf(line + used, sizeof(line) - used, "Press ");
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
                         "%sRelease ", down ? "  " : "");
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

    /* Eager-load the system SE bank so CallSystemSe* works from the very
     * first menu interaction. The decomp's InitGameSoundSystem (system.c)
     * would otherwise not fire until after Warning/Ipl/Firstmovie runs —
     * far too late for any SE queued during those phases. */
    {
        char mlt_path[512];
        snprintf(mlt_path, sizeof mlt_path, "%s/COMMON.MLT", g_gamedata_path);
        if (g_msa_rate > 0) {
            RX_LOG("game", "MSA rate override: %d Hz", g_msa_rate);
            recvx_msa_set_force_rate(g_msa_rate);
        }
        if (recvx_msa_init(mlt_path) != 0) {
            RX_LOG("game", "WARN: MSA init failed — SE disabled");
        }
        /* Apply CLI SeNo→sample overrides AFTER init so they replace the
         * baked RECVX menu defaults rather than get clobbered by them. */
        if (g_se_remap_str) {
            RX_LOG("game", "--se-remap: %s", g_se_remap_str);
            apply_se_remap(g_se_remap_str);
        }
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
        /* Mirror our ninja peripheral into Pad[0] so the game's direct
         * Pad[].press reads (CheckStartButton, AdvGetOkButton, etc.) fire.
         * Defined in port/src/game_texture_stubs.c (recvx_game target). */
        extern void recvx_pump_pad(void);
        recvx_pump_pad();

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
        /* Drain any decoded ADX PCM into the SDL audio queue for this
         * frame. Keeps latency ~1/15s without starving playback. */
        recvx_adx_pump();
        backend->end_frame();
        /* Hard-cap to PS2 NTSC tick rate. SDL vsync alone lets the loop
         * fire at the monitor refresh (144/240 Hz) which speeds up every
         * adv.c Mode counter — menu plate flicker, cursor repeat, fades. */
        recvx_backend_pace(60);

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
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_filter);
#endif
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
    recvx_backend_set_current(backend);
    RX_LOG("boot", "backend: %s", backend->name);

    int rc = g_run_game ? run_game_loop(backend)
                        : run_fmv_demo(backend, iso);

    backend->shutdown();
    if (iso) recvx_iso_close(iso);
    RX_LOG("boot", "clean exit");
    return rc;
}
