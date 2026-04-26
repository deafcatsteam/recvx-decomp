/*
 * stub_game.c — shims for every symbol system.c references but that
 * lives in a not-yet-compiled decomp file (adv.c, event.c, door.c,
 * effect.c, etc.) or a KATANA internal we haven't brought in yet.
 *
 * The aim is to let the game lib LINK so we can drive njUserInit /
 * njUserMain. Most stubs are no-ops; a few return 1 to make the task-table
 * state machine advance through the boot chain until we reach Title.
 *
 * Each block names the file the real implementation lives in so future
 * phases can trivially swap the stub for the real thing by adding that
 * file to RECVX_GAME_SOURCES in CMakeLists.txt and deleting the stub.
 */

#include "recvx_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ----------------------------------------------------------------------
 * Globals referenced from main.c / system.c whose owners aren't compiled.
 * We give them BSS defaults; real values will land once we bring in the
 * defining .c files.
 * ---------------------------------------------------------------------- */
int Pause_Flag;
int NowLoadDisp;
int PauseBtn;
int pl_sleep_cnt;
int Ps2_sys_cnt;
void* Ps2_PXLCONV;
float GameNear = -2.0f;
float GameFar  = -20000.0f;

/* Ninja / KATANA globals. NJS_MATRIX is 16 floats. Size matches
 * ninja.h NJS_MATRIX typedef. */
float mbuf[128 * 16];
float crmat[16];

/* ----------------------------------------------------------------------
 * sy* — Sega Ynsight / Shinobi helpers (ps2_sg_maloc.c / KATANA).
 * syMalloc/Free already in stub_ninja.c.
 * ---------------------------------------------------------------------- */
void syMallocStat(unsigned int* free_out, unsigned int* size_out) {
    if (free_out) *free_out = 12 * 1024 * 1024;
    if (size_out) *size_out = 12 * 1024 * 1024;
}
void syFree(void* p) { free(p); }
unsigned int syTmrGetCount(void) { return 0; }

/* ----------------------------------------------------------------------
 * bh* core helpers. Most are no-ops for the boot path.
 * ---------------------------------------------------------------------- */
void bhClearVSync(void)  {}
void bhInitVSync(void)   {}
void bhFontScaleSet(float a, float b, float c) { (void)a;(void)b;(void)c; }

/* bhGetFreeMemory is a bump allocator in the real game. Back it with
 * calloc — zero-init so stubbed RequestReadInsideFile (which doesn't
 * actually read anything) leaves the buffer in a predictable state;
 * AdvGetResourcePtr offset reads will see 0s not UB. */
void* bhGetFreeMemory(unsigned int size, int align) {
    (void)align;
    return calloc(size ? size : 1, 1);
}

/* bhChangeHWSetting + bhCheckSubTask live in system.c — don't stub. */
void bhMainSequence(void)    {}
void bhControlEvent(void)    {}
void bhControlMessage(int m) { (void)m; }
int  bhControlMap(void)      { return 0; }
void bhSetMap(void)          {}
void bhExitMap(void)         {}
void bhInitMap(int m)        { (void)m; }
void bhInitDoor(void)        {}
void bhSetDoor(void)         {}
int  bhControlDoor(void)     { return 0; }
void bhExitDoor(void)        {}
int  bhReadDoorData(void)    { return 0; }
void bhInitObjItm(void)      {}
void bhInitEffect(void)      {}
void bhInitCamera(void)      {}
void bhInitPlayer(void)      {}
void bhInitEnemy(void)       {}
void bhInitEvent(void)       {}
void bhReadPlayerData(void)  {}
void bhReadWeaponData(void)  {}
void bhResetPlayer(void)     {}
void bhStandPlayerMotion(void){}
void bhSetRDT(void)          {}
void bhInitReadRDT(void)     {}
void bhFinishRoom(void)      {}
void bhSetMemPvpTexture(void* a, void* b, int c) { (void)a;(void)b;(void)c; }
void bhPushGameData(void)    {}
void bhPushAllTexture(void)  {}
void bhPopAllTexture(void)   {}
void bhReleaseMainTexture(void) {}
void bhCopyMainmem2Texmem(void* t) { (void)t; }
unsigned char* bhCopyTexmem2MainmemSub(void* t, char* dst) { (void)t; return (unsigned char*)dst; }
void bhGarbageTexture(void* tl, int n) { (void)tl;(void)n; }
void bhControlGameOver(void) {}
void bhControlEffect(void)   {}
void bhControlSpEvtComputer(void) {}
void bhKeepSpEvtComputer(void)    {}
void bhSetScreenFade(unsigned int argb, float sec) { (void)argb;(void)sec; }
void bhControlScreenFade(void) {}
void bhDrawScreenFade(void)    {}
void bhSetScreenSaver(int a, float b) { (void)a;(void)b; }
void bhControlScreenSaver(void)       {}
void bhInitScreenSaver(void)          {}
void bhDrawScreenSaver(void)          {}
int  bhCkFlg(void* flg, int bit)      { (void)flg;(void)bit; return 0; }
void bhStFlg(void* flg, int bit)      { (void)flg;(void)bit; }
void bhSetPad(void)                   {}
void bhDeleteYakkyou(void)            {}
float bhMesLen(const unsigned short* m) { (void)m; return 0.0f; }
void bhDispMessage(float x,float y,float z,int a,int b,int c,int d){(void)x;(void)y;(void)z;(void)a;(void)b;(void)c;(void)d;}
void bhDispMessageEx(float x,float y,float z,int a,int b,int c,int d){(void)x;(void)y;(void)z;(void)a;(void)b;(void)c;(void)d;}
void bhDispTime(void* pos,int n,int tim,int col,float z){(void)pos;(void)n;(void)tim;(void)col;(void)z;}

/* Adv_FirstWarningMessage / Adv_CapcomLogo / Adv_BioCvTitle /
 * Adv_ChangeDiscScreen / Adv_SoundMuseum / Adv_GameOptionScreen all live
 * in adv.c now (compiled into recvx_game). No stubs needed. */

/* ----------------------------------------------------------------------
 * Sound system bootstrap.
 *
 * On PS2 this calls InitSoundDriver("MANATEE.DRV","COMMON.MLT") which
 * kicks off the whole CRI MANATEE pipeline. Our replacement is much
 * slimmer: we parse COMMON.MLT directly, decode every Vagi sample to
 * S16 stereo PCM, and cache them for CallSystemSe to pull from. The
 * ADX mixer already gets us BGM + voice; the MSA loader piggybacks on
 * the same mixer slots 2..15 for SE voice rotation.
 * ---------------------------------------------------------------------- */
void InitGameSoundSystem(void) {
    const char* gd = recvx_gamedata_dir();
    if (!gd) {
        RX_LOG("snd", "InitGameSoundSystem: no gamedata dir — SE disabled");
        return;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/COMMON.MLT", gd);
    if (recvx_msa_init(path) != 0) {
        RX_LOG("snd", "recvx_msa_init failed — SE disabled");
    }
}
void RequestAllStopSoundEx(int AdxFlag,int InSoundFlag,int FadeCount) {
    RX_LOG("snd", "RequestAllStopSoundEx adx=%d inSnd=%d fade=%d",
           AdxFlag, InSoundFlag, FadeCount);
}

/* ----------------------------------------------------------------------
 * Pad layer (pdGetPeripheral is the low-level KATANA sg_pad.h entry —
 * route it through our njGetPeripheral impl so both paths see the same
 * peripheral state). padman.c globals are stubbed here until we bring
 * the real padman.c into recvx_game.
 * ---------------------------------------------------------------------- */
extern const void* njGetPeripheral(uint32_t port);
const void* pdGetPeripheral(uint32_t port) { return njGetPeripheral(port); }

/* SoftResetFlag + ClearSoftResetKeyFlag + CheckSoftResetKeyFlag come from
 * padman.c now that it's compiled into recvx_game. */

/* ----------------------------------------------------------------------
 * Sound / SFX stubs — live in ps2_sg_sybt.c / sound.c once compiled.
 * CallSystemVoice lives in adv.c (line 50) — removed here.
 * ---------------------------------------------------------------------- */
void RequestRoomSoundBank(int a)                  { (void)a; }
void RequestArmsSoundBank(int a)                  { (void)a; }
void RequestPlayerVoiceSoundBank(int a)           { (void)a; }
int  CheckTransEndSoundBank(int a)                { (void)a; return 1; }
int  GetRoomSoundCaseNo(void)                     { return 0; }
void AllStopEnemySe(void)                         {}
void SendSoundCommand(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }
void ExecSoundSystemMonitor(void)                 {}

/* ----------------------------------------------------------------------
 * Movie / file request stubs (real bodies in ps2_sfd_mw.c, file.c).
 * Note: RequestReadInsideFile / GetInsideFileSize / GetReadFileStatus /
 * RequestReadIsoFile / GetIsoFileSize are implemented in
 * port/src/afs/afs_mount.c — they are NOT stubs, they back real AFS reads.
 * ---------------------------------------------------------------------- */
/* FMV playback — game-side entry points.
 *
 * The real PS2 flow (sdfunc.c PlayStartMovieEx → ps2_sfd_mw.c) hands a
 * .PSS stream through CRI Sofdec. Our port routes the same call chain
 * into recvx_fmv_open_loose → recvx_fmv_advance, then blits each frame
 * via backend->draw_rgba. Audio is pushed to the same SDL sink the ADX
 * mixer uses.
 *
 * Return-code contract (matches the decomp's case labels in adv.c /
 * system.c):
 *   WaitPrePlayMovie : 0=ready, 1=waiting, 2/3=skip/error
 *   PlayMovieMain    : 0=playing, 1..3=done (any non-zero ends mode 9)
 * PlayStartMovieEx signature is (MovieNo, MovieType, PauseFlag).
 *
 * Render layering: the 2D gfx queue is empty during FMV modes (task
 * hands off entirely to the movie path), so backend->draw_rgba inside
 * PlayMovieMain paints the current frame between begin_frame and
 * end_frame of the enclosing game loop iteration — no overdraw.
 *
 * Pacing: PlayMovieMain drains video frames until fmv PTS catches up to
 * wall-clock elapsed since playback start, then blits the latest. Same
 * pattern as run_fmv_demo in main_pc.c. */

static recvx_fmv_t*   g_movie_fmv;
static uint32_t       g_movie_start_ms;
static int            g_movie_done;       /* 1 once EOF reached */
static int            g_movie_pending;    /* 1 between Start and first advance */

static uint32_t stub_now_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static void stub_fmv_audio_sink(void* opaque, int rate,
                                const void* pcm, int bytes) {
    const recvx_backend* b = (const recvx_backend*)opaque;
    if (b && b->audio_init)  b->audio_init(rate);
    if (b && b->audio_queue) b->audio_queue(pcm, bytes);
}

int PlayStartMovieEx(int no, int type, int pause) {
    (void)type; (void)pause;
    /* Tear down any previous stream. */
    if (g_movie_fmv) {
        recvx_fmv_close(g_movie_fmv);
        g_movie_fmv = NULL;
    }
    g_movie_done    = 0;
    g_movie_pending = 0;

    const char* gd = recvx_gamedata_dir();
    if (!gd) {
        RX_LOG("fmv", "PlayStartMovieEx: no gamedata dir set — skip movie %d", no);
        g_movie_done = 1;
        return 0;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/MOVIE/MV_%03d.PSS", gd, no);
    RX_LOG("fmv", "PlayStartMovieEx: opening %s (type=%d pause=%d)",
           path, type, pause);

    g_movie_fmv = recvx_fmv_open_loose(path);
    if (!g_movie_fmv) {
        RX_LOG("fmv", "open failed — skip");
        g_movie_done = 1;
        return 0;
    }
    const recvx_backend* be = recvx_backend_current();
    if (be) {
        recvx_fmv_set_audio_sink(g_movie_fmv, stub_fmv_audio_sink, (void*)be);
    }
    g_movie_start_ms = stub_now_ms();
    g_movie_pending  = 1;
    return 0;
}

int PlayStopMovieEx(void) {
    if (g_movie_fmv) {
        recvx_fmv_close(g_movie_fmv);
        g_movie_fmv = NULL;
    }
    g_movie_done    = 0;
    g_movie_pending = 0;
    return 0;
}

int WaitPrePlayMovie(void) {
    /* Caller (adv.c mode 8) keeps polling until we return non-1. The
     * loose-file path is synchronous so we can declare "ready" as soon as
     * Start succeeded. Return 3 (skip) if no FMV is alive. */
    if (g_movie_done)  return 3;
    if (!g_movie_fmv)  return 3;
    return 0;
}

int PlayMovieMain(void) {
    if (g_movie_done)   return 1;
    if (!g_movie_fmv)   return 1;

    /* PC port: Start press skips the movie. Returning 1 ends mode 9 of
     * Adv_CapcomLogo and case 4 of bhSysCallMovie just like a natural
     * EOF would. */
    extern uint32_t recvx_input_buttons(void);
    if (recvx_input_buttons() & 0x800u) {
        RX_LOG("fmv", "PlayMovieMain: Start-press skip");
        recvx_fmv_close(g_movie_fmv);
        g_movie_fmv  = NULL;
        g_movie_done = 1;
        return 1;
    }

    /* Wall-clock-paced frame drain. Advance until FMV PTS meets or
     * exceeds elapsed time, so dropped/slow frames still stay roughly
     * in sync instead of slipping further behind each tick. */
    double elapsed_s = (double)(stub_now_ms() - g_movie_start_ms) / 1000.0;
    if (g_movie_pending) {
        /* First advance primes the first frame. */
        if (!recvx_fmv_advance(g_movie_fmv)) {
            RX_LOG("fmv", "PlayMovieMain: first advance failed");
            recvx_fmv_close(g_movie_fmv);
            g_movie_fmv     = NULL;
            g_movie_done    = 1;
            return 1;
        }
        RX_LOG("fmv", "PlayMovieMain: first advance OK, pts=%.3f", recvx_fmv_pts_s(g_movie_fmv));
        g_movie_pending = 0;
    } else {
        while (recvx_fmv_pts_s(g_movie_fmv) < elapsed_s) {
            if (!recvx_fmv_advance(g_movie_fmv)) {
                RX_LOG("fmv", "PlayMovieMain: advance EOF");
                recvx_fmv_close(g_movie_fmv);
                g_movie_fmv     = NULL;
                g_movie_done    = 1;
                return 1;
            }
        }
    }

    const recvx_backend* be = recvx_backend_current();
    const void* pix = recvx_fmv_pixels(g_movie_fmv);
    if (be && be->draw_rgba) {
        if (!pix) {
            RX_LOG("fmv", "PlayMovieMain: no pixels! w=%d h=%d pts=%.3f",
                   recvx_fmv_width(g_movie_fmv),
                   recvx_fmv_height(g_movie_fmv),
                   recvx_fmv_pts_s(g_movie_fmv));
        }
        be->draw_rgba(pix,
                      recvx_fmv_width(g_movie_fmv),
                      recvx_fmv_height(g_movie_fmv));
    }
    return 0;
}

void mwPlySetDispMode(int m)             { (void)m; }

/* ----------------------------------------------------------------------
 * Sub-task handlers (itemselect, typewriter, ending, monitor etc.).
 * ---------------------------------------------------------------------- */
int  ItemTaskCheck(void)    { return 0; }
int  StatusMain(void)       { return 0; }
/* ControlTypewriter is implemented in port/src/game_texture_stubs.c so it
 * can access the real SYS_WORK struct (requires KATANA types.h prelude).
 * Needed to skip the text-scroll intro and jump straight to MV_000.PSS
 * after NEW GAME is selected, since bup_00.c (TypewriterMode[] dispatcher)
 * isn't compiled yet. */
int  ControlRanking(void)   { return 0; }
void Expand(void)           {}
void AllItemInit(void)      {}
void SbsTextureInit(void)   {}
void StatusMapFlagInit(void){}
void TypewriterKeepMemory(void) {}
void Ps2ClearOT(void)       {}

/* ----------------------------------------------------------------------
 * EE cache-flush intrinsic — no-op on PC.
 * ---------------------------------------------------------------------- */
void FlushCache(int mode)   { (void)mode; }

/* ----------------------------------------------------------------------
 * More globals the game references. Most are never read/written during
 * the boot chain, so zero-init is fine.
 * ---------------------------------------------------------------------- */
void*        _nj_vertex_buf_;
float        FontScaleX  = 1.0f;
float        FontScaleCR = 1.0f;
int          FontSz      = 13;
int          BackColorFlag;
int          Ps2_albinoid_flag;
int          Ps2_ice_flag;
int          Ps2_rendertex_initflag;
unsigned int Ps2_pad;
unsigned int Pad_act;
int          WpnTab[256];

/* ----------------------------------------------------------------------
 * Additional nj* functions beyond what stub_ninja.c already covers.
 * ---------------------------------------------------------------------- */
void  njFogDisable(void)          {}
void  njFogEnable(void)           {}
void  njWaitVSync(void)           {}
void  njSetScreen(void* s)        { (void)s; }
float njSin(int brad)             { (void)brad; return 0.0f; }
void  njMemCopy(void* d,void* s,int n) { if (d && s && n > 0) memcpy(d, s, (size_t)n); }
void  njChangeSystem(int mode,int frame,int count) { (void)mode;(void)frame;(void)count; }
void  njPrintC(int code,const char* s) { (void)code;(void)s; }
void  njPrintColor(unsigned int c)    { (void)c; }
int   njCalcTexture(int mode)         { (void)mode; return 0; }
void  njGarbageTexture(void* tl,int n){ (void)tl;(void)n; }
void  njReleaseTexture(void* tl)      { (void)tl; }
void  njReleaseTextureAll(void)       {}

void  njMemCopy4(void* d, void* s, int n)          { if (d && s && n > 0) memcpy(d, s, (size_t)n); }

/* njTextureFilterMode / njDrawPolygon / njLoadTexture / njSetTexture /
 * njSetTextureNum / njSetTextureInfo / njSetTextureName / njQuadTextureStart /
 * njQuadTextureEnd / njSetQuadTexture / njDrawQuadTexture all live in
 * port/src/game_texture_stubs.c. That file is compiled into recvx_game so it
 * sees the real NJS_TEXMEMLIST / QUAD / NJS_POLYGON_VTX layouts — they drive
 * the gfx API in backend_gl.c that finally puts pixels on screen. */
int   njGetPaletteMode(void)                       { return 0; }
void  njSetPaletteData(int mode, int offset, int count, void* data) {
    (void)mode;(void)offset;(void)count;(void)data;
}

/* adv.c calls fabsf but MSVC /Od can emit an out-of-line reference that
 * the default lib chain doesn't satisfy. Provide an extern-linkage
 * fabsf implemented via double fabs. Forward-declare fabs so we don't
 * pull in <math.h> (which inlines fabsf and would collide). */
extern double fabs(double);
float fabsf(float x) { return (float)fabs((double)x); }

/* adxwrap.c isn't compiled yet — shim adv.c's ADX calls into our
 * FFmpeg-backed streaming player (port/src/audio/adx_player.c). */
void PlayAdx(unsigned int slot, unsigned int part, unsigned int file) {
    RX_LOG("snd", "PlayAdx slot=%u part=%u file=%u", slot, part, file);
    recvx_adx_play((int)slot, (int)part, (int)file);
}
void StopAdx(unsigned int slot)                    {
    RX_LOG("snd", "StopAdx slot=%u", slot);
    recvx_adx_stop((int)slot);
}
void SetVolumeAdx2(unsigned int slot, float vol)   {
    RX_LOG("snd", "SetVolumeAdx2 slot=%u vol=%.2f", slot, vol);
    recvx_adx_set_volume((int)slot, vol);
}

/* bh helpers not yet in a compiled .c. bhSetFontTexture is the first
 * resource-touching call in Adv_FirstWarningMessage mode 3 — logging
 * the pointer lets us see whether ap->ptr[0] (SYSTEM.AFS entry 1 payload)
 * actually survived the read. */
void bhSetFontTexture(void* p) {
    RX_LOG("bh", "bhSetFontTexture p=%p first8=%02x%02x%02x%02x%02x%02x%02x%02x",
           p,
           p ? ((unsigned char*)p)[0] : 0, p ? ((unsigned char*)p)[1] : 0,
           p ? ((unsigned char*)p)[2] : 0, p ? ((unsigned char*)p)[3] : 0,
           p ? ((unsigned char*)p)[4] : 0, p ? ((unsigned char*)p)[5] : 0,
           p ? ((unsigned char*)p)[6] : 0, p ? ((unsigned char*)p)[7] : 0);
}
void bhReleaseFreeMemory(void* p)                  { (void)p; }

/* Memory card — simulate a card present on port/slot 0 so NEW GAME is
 * selectable at the title menu. Real save/load still stubbed; this just
 * unblocks the FindFirstVmDrive() >= 0 check in adv.c CheckButton so the
 * user can reach the NEW GAME → TitleCall(3) transition that eventually
 * plays the opening FMV. Return value 2 = "PS2 memcard type detected" per
 * the decomp's CheckConnectVmDrive convention. */
void* CreateMemoryCard(void* pCard)                { (void)pCard; return pCard; }
int   GetMcSelectPortType(void* pCard, unsigned int port) {
    (void)pCard;
    return (port == 0) ? 2 : 0;
}
int   CheckMcSelectPortInfoState(unsigned int port){ (void)port; return 0; }

/* Sys load/save screens — always return "ready" so boot chain advances
 * past the VM check in Adv_FirstWarningMessage mode 11. */
void* CreateSysLoadScreen(void* s, void* arg)      { (void)s;(void)arg; return s; }
int   ExecuteSysLoadScreen(void* ps)               { (void)ps; return 1; /* ok */ }
void* CreateSysSaveScreen(void* s, void* arg)      { (void)s;(void)arg; return s; }
int   ExecuteSysSaveScreen(void* ps)               { (void)ps; return 1; }
void  SetAdjustDisplay(void)                       { }

/* Sound config shims. */
void  syCfgSetSoundMode(int mode)                  { (void)mode; }
void  SetSoundModeEx(int mode)                     { (void)mode; }
int   GetSoundMode(void)                           { return 0; }
/* MountSoundAfs / UnmountSoundAfs now live in port/src/afs/afs_mount.c. */
/* sdfunc.h prototypes:
 *   void CallSystemSe     (int param, int SeNo);
 *   void CallSystemSeEx   (int SeNo, int Volume);
 *   void CallSystemSeBasic(int SeNo, int Volume, int FxLevel);
 *
 * Real CallSystemSeBasic computes BankNo=SeNo/256, ListNo=SeNo and hands
 * it to ExPlaySe(&RequestInfo). For the system bank (BankNo==0) that
 * resolves through Sset/Prog/Smpl into a Vagi entry in COMMON.MLT.
 * Stage 1 of the port skips the intermediate tables and plays
 * Vagi[SeNo % sample_count] directly — good enough to hear menu beeps
 * and verify the whole decode/mix chain end to end. */
void  CallSystemSe(int param, int SeNo) {
    (void)param;
    RX_LOG("snd", "CallSystemSe param=%d SeNo=%d", param, SeNo);
    recvx_msa_play_se(SeNo, 100);
}
void  CallSystemSeEx(int SeNo, int Volume) {
    RX_LOG("snd", "CallSystemSeEx SeNo=%d vol=%d", SeNo, Volume);
    recvx_msa_play_se(SeNo, Volume);
}
void  CallSystemSeBasic(int SeNo, int Volume, int FxLevel) {
    (void)FxLevel;
    RX_LOG("snd", "CallSystemSeBasic SeNo=%d vol=%d fx=%d",
           SeNo, Volume, FxLevel);
    recvx_msa_play_se(SeNo, Volume);
}

/* Vibration extra. */
void  StartVibrationEx(int port, int motor, int power, int time) {
    (void)port;(void)motor;(void)power;(void)time;
}
void  StopVibrationEx(int port, int motor)         { (void)port;(void)motor; }

/* Globals adv.c references. PatId is now owned by afs_mount.c (matches
 * sdfunc.c:91 `int PatId[4]` exactly). palbuf holds the current palette.
 * Ps2_current_texmemlist is defined in game_texture_stubs.c (typed
 * NJS_TEXMEMLIST* so adv.c:695 dereferences cleanly). */
unsigned int palbuf[256];

/* ----------------------------------------------------------------------
 * adv.c pulls in: sound bank (PlayBgm/Voice), vibration (vibman),
 * softreset key state, display adjust, AFS mount, ExitApplication,
 * Ps2 texture helpers. Most are plain no-ops; CheckSoftResetKeyFlag
 * returns 0 so the title screen never thinks we pressed L1+R1+Start+Sel.
 * ---------------------------------------------------------------------- */
/* Slot convention for the ADX mixer:
 *   0 = BGM (looped)
 *   1 = voice (one-shot)
 * PlayAdx also uses slot 0 for title voice, which stomps BGM — same as
 * the original PS2 BGM channel sharing, so it matches game expectations. */
#define RX_BGM_SLOT   0
#define RX_VOICE_SLOT 1

/* sdfunc.h: void PlayBgmEx2(unsigned int PatId, int BgmNo, int FadeInRate, int Volume);
 * Volume on PS2 is 0..127. Normalize to 0..1 for the mixer. */
void PlayBgmEx2(unsigned int part, int bgmNo, int fadeIn, int vol) {
    (void)fadeIn;
    RX_LOG("snd", "PlayBgmEx2 part=%u bgmNo=%d fadeIn=%d vol=%d",
           part, bgmNo, fadeIn, vol);
    if (bgmNo < 0) { recvx_adx_stop(RX_BGM_SLOT); return; }
    float g = vol <= 0 ? 0.0f : (vol >= 127 ? 1.0f : (float)vol / 127.0f);
    recvx_adx_set_volume(RX_BGM_SLOT, g);
    recvx_adx_play_ex(RX_BGM_SLOT, (int)part, bgmNo, 1);
}

/* sdfunc.h: void PlayVoiceEx2(int PatId, int VoiceNo, NJS_POINT3* pPos,
 *                             int Mode, int FadeInRate, int PauseFlag); */
void PlayVoiceEx2(int part, int voiceNo, void* pPos, int mode, int fadeIn,
                  int pauseFlag) {
    (void)pPos;(void)mode;(void)fadeIn;(void)pauseFlag;
    RX_LOG("snd", "PlayVoiceEx2 part=%d voiceNo=%d mode=%d fadeIn=%d pause=%d",
           part, voiceNo, mode, fadeIn, pauseFlag);
    if (voiceNo < 0) { recvx_adx_stop(RX_VOICE_SLOT); return; }
    recvx_adx_set_volume(RX_VOICE_SLOT, 1.0f);
    recvx_adx_play_ex(RX_VOICE_SLOT, part, voiceNo, 0);
}
/* MountAdvAfs lives in adv.c (line 157) — don't stub. */
void ExitApplication(void)            { /* boot chain shouldn't hit this */ }
/* SetUseVibrationUnit, CheckSoftResetKeyFlag come from vibman.c / padman.c.
 * SetEventVibrationMode is in sdfunc.c (not compiled yet) so still stub. */
void SetEventVibrationMode(int m)     { (void)m; }

/* KATANA pad-vib rumble primitives — no-op on PC. Keeps vibman.c linking. */
int  pdVibMxIsReady(uint32_t port)                    { (void)port; return 1; }
void pdVibMxSetStopTime(uint32_t port, uint32_t time) { (void)port;(void)time; }
void pdVibMxStart(uint32_t port, int motor, int power){ (void)port;(void)motor;(void)power; }
void pdVibMxStop(uint32_t port, int motor)            { (void)port;(void)motor; }

/* Called from SetPvrInfo (adv.c:587) with the raw TIM2 payload ptr just
 * before njSetTextureInfo. Logs first 16 bytes so we can see whether the
 * TIM2 header ('TIM2' magic + version etc.) looks sane. */
void Ps2CheckTextureAlpha(void* pp) {
    if (!pp) { RX_LOG("ps2", "Ps2CheckTextureAlpha pp=NULL"); return; }
    unsigned char* b = (unsigned char*)pp;
    RX_LOG("ps2", "Ps2CheckTextureAlpha pp=%p hdr=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
           pp, b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
           b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}
void RequestAdjustDisplay(int a, int b) { (void)a;(void)b; }
void SetSoundMode(int m)              { (void)m; }

/* Pad[4] comes from padman.c. */
