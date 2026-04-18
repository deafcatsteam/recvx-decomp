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
#include <stdlib.h>
#include <string.h>

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
 * plain malloc for now — the game will just leak until we free it. */
void* bhGetFreeMemory(unsigned int size, int align) {
    (void)align;
    return malloc(size);
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

/* ----------------------------------------------------------------------
 * Adv_* — screens implemented in adv.c. Returning 1 makes bhSysCall*
 * advance to the next task immediately so we get out of the boot chain
 * into Title without needing real rendering. Returning 0 would stall
 * forever on the warning screen.
 * ---------------------------------------------------------------------- */
int Adv_FirstWarningMessage(void) { return 1; }
int Adv_CapcomLogo(void)          { return 1; }
int Adv_BioCvTitle(void)          { return 6; /* default: skip_save → Title */ }
int Adv_ChangeDiscScreen(void)    { return 1; }
int Adv_SoundMuseum(void)         { return 1; }
int Adv_GameOptionScreen(void)    { return 1; }

/* ----------------------------------------------------------------------
 * Sound (will be replaced by SDL audio once we wire it in phase 5b).
 * ---------------------------------------------------------------------- */
void InitGameSoundSystem(void)                       {}
void RequestAllStopSoundEx(int a,int b,int c)        { (void)a;(void)b;(void)c; }

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
 * ---------------------------------------------------------------------- */
void CallSystemVoice(int a, int b)                { (void)a;(void)b; }
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
 * ---------------------------------------------------------------------- */
int  RequestReadIsoFile(int a, int b)    { (void)a;(void)b; return 0; }
int  RequestReadInsideFile(int a, int b) { (void)a;(void)b; return 0; }
int  GetIsoFileSize(int a)               { (void)a; return 0; }
int  GetInsideFileSize(int a)            { (void)a; return 0; }
int  GetReadFileStatus(int a)            { (void)a; return 1; /* ready */ }
int  PlayStartMovieEx(int a, int b)      { (void)a;(void)b; return 0; }
int  PlayStopMovieEx(void)               { return 0; }
int  WaitPrePlayMovie(void)              { return 1; }
int  PlayMovieMain(void)                 { return 0; }
void mwPlySetDispMode(int m)             { (void)m; }

/* ----------------------------------------------------------------------
 * Sub-task handlers (itemselect, typewriter, ending, monitor etc.).
 * ---------------------------------------------------------------------- */
int  ItemTaskCheck(void)    { return 0; }
int  StatusMain(void)       { return 0; }
void ControlTypewriter(void){}
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

/* ----------------------------------------------------------------------
 * adv.c pulls in: sound bank (PlayBgm/Voice), vibration (vibman),
 * softreset key state, display adjust, AFS mount, ExitApplication,
 * Ps2 texture helpers. Most are plain no-ops; CheckSoftResetKeyFlag
 * returns 0 so the title screen never thinks we pressed L1+R1+Start+Sel.
 * ---------------------------------------------------------------------- */
void PlayBgmEx2(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }
void PlayVoiceEx2(int a, int b, void* p, int c, int d, int e) {
    (void)a;(void)b;(void)p;(void)c;(void)d;(void)e;
}
void MountAdvAfs(void)                { }
void ExitApplication(void)            { /* boot chain shouldn't hit this */ }
/* SetUseVibrationUnit, CheckSoftResetKeyFlag come from vibman.c / padman.c.
 * SetEventVibrationMode is in sdfunc.c (not compiled yet) so still stub. */
void SetEventVibrationMode(int m)     { (void)m; }

/* KATANA pad-vib rumble primitives — no-op on PC. Keeps vibman.c linking. */
int  pdVibMxIsReady(uint32_t port)                    { (void)port; return 1; }
void pdVibMxSetStopTime(uint32_t port, uint32_t time) { (void)port;(void)time; }
void pdVibMxStart(uint32_t port, int motor, int power){ (void)port;(void)motor;(void)power; }
void pdVibMxStop(uint32_t port, int motor)            { (void)port;(void)motor; }

void Ps2CheckTextureAlpha(void* pp)   { (void)pp; }
void RequestAdjustDisplay(int a, int b) { (void)a;(void)b; }
void SetSoundMode(int m)              { (void)m; }

/* Pad[4] comes from padman.c. */
