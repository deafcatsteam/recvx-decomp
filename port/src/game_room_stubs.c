/*
 * game_room_stubs.c — no-op stubs for the gameplay subsystems pulled in
 * by room.c / eneset.c / objitm.c (enemy AI handlers, collision, lights,
 * effects, player weapon logic). Lives in the game target (like
 * game_texture_stubs.c) so it sees the real KATANA/types.h layouts.
 *
 * Real math (njSqrt/njCalcVector/njUnitVector/njInnerProduct/njRotateXYZ)
 * is in ninja_3d.c; real line/plane collision comes from ps2_NaColi.c.
 * Everything here is a placeholder to be replaced by the real decomp
 * file when its subsystem is brought into the build.
 */

#include "ninja.h"
#include "types.h"
#include <string.h>

/* ---- enemy AI handlers (eneset.c bhJumpEnemy dispatch table) -------- */
void bhEne01(BH_PWORK* epw) { (void)epw; }
void bhEne02(BH_PWORK* epw) { (void)epw; }
void bhEne03(BH_PWORK* epw) { (void)epw; }
void bhEne04(BH_PWORK* epw) { (void)epw; }
void bhEne05(BH_PWORK* epw) { (void)epw; }
void bhEne06(BH_PWORK* epw) { (void)epw; }
void bhEne07(BH_PWORK* epw) { (void)epw; }
void bhEne08(BH_PWORK* epw) { (void)epw; }
void bhEne09(BH_PWORK* epw) { (void)epw; }
void bhEne10(BH_PWORK* epw) { (void)epw; }
void bhEne11(BH_PWORK* epw) { (void)epw; }
void bhEne12(BH_PWORK* epw) { (void)epw; }
void bhEne13(BH_PWORK* epw) { (void)epw; }
void bhEne14(BH_PWORK* epw) { (void)epw; }
void bhEne15(BH_PWORK* epw) { (void)epw; }
void bhEne16(BH_PWORK* epw) { (void)epw; }
void bhEne17(BH_PWORK* epw) { (void)epw; }
void bhEne18(BH_PWORK* epw) { (void)epw; }
void bhEne19(BH_PWORK* ewP) { (void)ewP; }
void bhEne20(BH_PWORK* epw) { (void)epw; }
void bhEne21(BH_PWORK* epw) { (void)epw; }
void bhEne22(BH_PWORK* epw) { (void)epw; }
void bhEne23(BH_PWORK* epw) { (void)epw; }
void bhEne24(BH_PWORK* epw) { (void)epw; }
void bhEne25(BH_PWORK* epw) { (void)epw; }
void bhEne26(BH_PWORK* epw) { (void)epw; }
void bhEne27(BH_PWORK* epw) { (void)epw; }
void bhEne28(void)          {}
void bhEne29(BH_PWORK* ewP) { (void)ewP; }
void bhEne30(BH_PWORK* epw) { (void)epw; }
void bhEne53(BH_PWORK* epw) { (void)epw; }
void bhEne54(BH_PWORK* epw) { (void)epw; }
void bhEne55(BH_PWORK* epw) { (void)epw; }
void bhEne71(BH_PWORK* epw) { (void)epw; }
void bhEne_InitDamage(BH_PWORK* epw) { (void)epw; }
void bhSubpl(BH_PWORK* epw) { (void)epw; }

/* ---- collision / floor (hitchk.c) ----------------------------------- */
/* bhCheckCut now real (cut.c). */
void bhCheckEnemies(BH_PWORK* pp)     { (void)pp; }
int  bhCheckFloorNum(float py)        { (void)py; return 0; }
int  bhCheckL2Wall(NJS_LINE* lp, unsigned int flg, float* len)
                                      { (void)lp;(void)flg;(void)len; return 0; }
void bhCheckPlayer(BH_PWORK* pp)      { (void)pp; }
void bhCheckWall(BH_PWORK* pw)        { (void)pw; }
void bhCheckWall2Box(BH_PWORK* pw)    { (void)pw; }
ATR_WORK* bhCheckWallType(NJS_POINT3* pos, unsigned int flg, float ar, float ah)
                                      { (void)pos;(void)flg;(void)ar;(void)ah; return NULL; }
ATR_WORK* bhCheckWallType2(NJS_POINT3* pos, unsigned int flg, float aw, float ad, float ah, int idx_ct)
                                      { (void)pos;(void)flg;(void)aw;(void)ad;(void)ah;(void)idx_ct; return NULL; }
void bhResetAtariAttr(void)           {}
void bhSetFloorNum(BH_PWORK* pp)      { (void)pp; }
int  bhCheckClipModel(BH_PWORK* pp)   { (void)pp; return 0; }

/* ---- lights (light.c) ------------------------------------------------ */
void bhInitLight(void)                {}
void bhSetLight(void)                 {}
void bhSetHalfLight(void)             {}
void bhSetEasyDirLight(float it)      { (void)it; }
void bhGetLightVector(int xr, int yr, int zr, NJS_VECTOR* vec)
{
    (void)xr; (void)yr; (void)zr;
    /* Callers read the result — hand back a sane unit down-vector
     * instead of stack garbage. */
    if (vec) { vec->x = 0.0f; vec->y = 1.0f; vec->z = 0.0f; }
}

/* ---- effects (effect.c) ---------------------------------------------- */
void bhClearEffect(void)              {}
int  bhSetEffect(int effno, POINT* pnt, unsigned char* lkp, int lkono)
                                      { (void)effno;(void)pnt;(void)lkp;(void)lkono; return 0; }
int  bhSetEffectTb(EF_WORK* efp, NJS_POINT3* off, unsigned char* lkp, int lkono)
                                      { (void)efp;(void)off;(void)lkp;(void)lkono; return 0; }
void bhSetExplosion(NJS_POINT3* pos)  { (void)pos; }

/* ---- player / model / motion (player.c, MdlPut.c, motion) ------------ */
void bhActionWeapon(BH_PWORK* op)     { (void)op; }
void bhObjWpn(BH_PWORK* op)           { (void)op; }
/* bhCalcHair now real (player.c). */
void bhCalcModel(BH_PWORK* ewP)       { (void)ewP; }
void bhPutModel(BH_PWORK* ewP)        { (void)ewP; }
void bhControlMask(BH_PWORK* pp)      { (void)pp; }
void bhInitMask(BH_PWORK* pp)         { (void)pp; }
/* bhInitRoomChangePlayer now real (player.c). */
int  bhSetMotion(BH_PWORK* ewP, int add, int mode, void* datP)
                                      { (void)ewP;(void)add;(void)mode;(void)datP; return 0; }
int  bhSetShadow(char* jtb, unsigned char* lkp, int lkono, float sx, float sy, float sz)
                                      { (void)jtb;(void)lkp;(void)lkono;(void)sx;(void)sy;(void)sz; return 0; }

/* ---- game.c draw-path extras (cinesco bars, sniper scope, render-to-
 * texture, mirrors, screen light control) — no-op for now -------------- */
void bhControlCinesco(void)              {}
void bhDrawCinesco(void)                 {}
void bhControlLight(void)                {}
void bhDrawEffect(void)                  {}
void bhDrawScope(void)                   {}
void bhDrawThermometer(void)             {}
void bhDrawSmallScreenRenderTexture(void) {}
void bhDrawFullScreenRenderTexture(void)  {}
void njMirror(NJS_MATRIX* m, NJS_PLANE* pl) { (void)m;(void)pl; }
void njSetCheapShadowMode(Int mode)      { (void)mode; }
void njSetCnkBlendMode(Uint32 attr)      { (void)attr; }
/* FOV is fixed at 60 deg in ninja_cnk.c's begin_3d for now. */
void njSetPerspective(Angle ang)         { (void)ang; }
void bhChangeBackColor(void)             {}
void bhChangeBackColorEvt(void)          {}
void bhChangeClipVolume(char stg_no, char rom_no, char rcase, int evc_no)
                                         { (void)stg_no;(void)rom_no;(void)rcase;(void)evc_no; }
void bhChangeClipVolumeRM(void)          {}
void bhChangeViewClipRM(void)            {}
/* Catmull-Rom camera spline (cinematic cameras). Output the first
 * control point instead of stack garbage until implemented. */
void njOverhauserSpline(Float* idata, Float* odata, NJS_SPLINE* attr, Float frame)
{
    (void)attr; (void)frame;
    if (odata && idata) { odata[0] = idata[0]; odata[1] = idata[1]; odata[2] = idata[2]; }
}

/* ---- ninja chunk lighting setters — no-op until the GL lighting
 * path consumes them ---------------------------------------------------- */
void njCnkSetEasyLight(Float x, Float y, Float z) { (void)x;(void)y;(void)z; }
void njCnkSetEasyLightColor(Float r, Float g, Float b) { (void)r;(void)g;(void)b; }
void njCnkSetEasyLightIntensity(Float inten, Float ambient) { (void)inten;(void)ambient; }
void njCnkSetEasyMultiLightSwitch(Int light, Int flag) { (void)light;(void)flag; }
void njCnkSetEasyMultiLightVector(Float vx, Float vy, Float vz) { (void)vx;(void)vy;(void)vz; }
void njCnkSetSimpleMultiAmbient(Float ar, Float ag, Float ab) { (void)ar;(void)ag;(void)ab; }
void njCnkSetSimpleMultiLightColor(Int light, Float lr, Float lg, Float lb) { (void)light;(void)lr;(void)lg;(void)lb; }
void njCnkSetSimpleMultiLightMatrices(void) {}
void njCnkSetSimpleMultiLightSwitch(Int light, Int flag) { (void)light;(void)flag; }
void njCnkSetSimpleMultiLightVector(Float vx, Float vy, Float vz) { (void)vx;(void)vy;(void)vz; }

/* ---- np* model helpers (ninja plus) ----------------------------------- */
/* Real memory ops — data actually moves through these. */
void npCopyMemory(unsigned char* dst, unsigned char* src, unsigned int size)
{
    if (dst && src) memcpy(dst, src, size);
}
void npSetMemoryL(unsigned int* memp, unsigned int size, int dat)
{
    if (!memp) return;
    for (unsigned int i = 0; i < size; ++i) memp[i] = (unsigned int)dat;
}

void npCalcMorphing(NJS_CNK_OBJECT* obj_a, NJS_CNK_OBJECT* obj_b, float no, int obj_n)
                                      { (void)obj_a;(void)obj_b;(void)no;(void)obj_n; }
void npChangeMatAlphaColor(NJS_CNK_OBJECT* objp, int obj_n, unsigned char alpha)
                                      { (void)objp;(void)obj_n;(void)alpha; }
void npClrTranslate(void)             {}
void npCnkFlatOff(NJS_CNK_OBJECT* objp) { (void)objp; }
unsigned int npGetMatColor(NJS_CNK_OBJECT* objp, int obj_n)
                                      { (void)objp;(void)obj_n; return 0; }
void npPopMdlstr2(NJS_CNK_OBJECT* objp, int obj_n)  { (void)objp;(void)obj_n; }
void npPushMdlstr2(NJS_CNK_OBJECT* objp, int obj_n) { (void)objp;(void)obj_n; }
void npSetAllMatAlphaColor(NJS_CNK_OBJECT* objp, int obj_n, unsigned char alpha)
                                      { (void)objp;(void)obj_n;(void)alpha; }
void npSetOffsetUV(NJS_CNK_MODEL* mdlp, short offu, short offv)
                                      { (void)mdlp;(void)offu;(void)offv; }
void npSkinConvert(NJS_CNK_OBJECT* objp, int* sknp) { (void)objp;(void)sknp; }

/* ---- player.c dependencies (weapons, SE, punch minigame, collision,
 * enemy search, water/floor FX) — no-op until their real files land ---- */
void CallPlayerActionSe(int SeNo, int Flag) { (void)SeNo;(void)Flag; }
void CallPlayerFootStepSe(int FloorType, int Type, int Flag) { (void)FloorType;(void)Type;(void)Flag; }
void CallPlayerVoice(int no) { (void)no; }
void CallPlayerWeaponSeEx(NJS_POINT3* pPos, int SeNo, int SlotNo) { (void)pPos;(void)SeNo;(void)SlotNo; }
void PlyPchInit(BH_PWORK* ewP) { (void)ewP; }
void PlyPchMain(BH_PWORK* ewP) { (void)ewP; }
void bhAddSpeed(BH_PWORK* pp, int r) { (void)pp;(void)r; }
void bhArmIkMdk(BH_PWORK* ewP, int bas_no, NJS_POINT3* effP, int rot) { (void)ewP;(void)bas_no;(void)effP;(void)rot; }
void bhCPM0_event(void) {}
void bhCPM2_SearchPch(void) {}
void bhCPM2_act_atk_pch(void) {}
void bhCPM2_act_scp(void) {}
void bhCPM2_act_suw_pch(void) {}
void bhCPM2_act_wsc_pch(void) {}
void bhCalcFixOffset(BH_PWORK* ewP, char* datP, NJS_POINT3* offP, NJS_POINT3* rtnP)
{
    (void)ewP; (void)datP; (void)offP;
    if (rtnP) { rtnP->x = 0.0f; rtnP->y = 0.0f; rtnP->z = 0.0f; }
}
int   bhCalcLockEneYR(BH_PWORK* pp, int idx) { (void)pp;(void)idx; return 0; }
short bhCheckBullet(void) { return 0; }
void  bhCheckExmAtari(BH_PWORK* pp) { (void)pp; }
ATR_WORK* bhCheckFloorEffect(int flr_no, float px, float pz) { (void)flr_no;(void)px;(void)pz; return NULL; }
void  bhCheckFloorP(BH_PWORK* pp) { (void)pp; }
int   bhCheckFloorSound(BH_PWORK* pp, int flr_no, float px, float pz) { (void)pp;(void)flr_no;(void)px;(void)pz; return 0; }
int   bhCheckGunAtari(GA_WORK* gap) { (void)gap; return 0; }
void  bhCheckKnifeAtari(GA_WORK* gap) { (void)gap; }
int   bhCheckWallEx(BH_PWORK* pw, NJS_POINT3* npos, NJS_POINT3* opos, float par, float pah)
                                      { (void)pw;(void)npos;(void)opos;(void)par;(void)pah; return 0; }
ATR_WORK* bhCheckWater(NJS_POINT3* pos) { (void)pos; return NULL; }
void  bhClrUseKaidanFlag(BH_PWORK* pp) { (void)pp; }
int   bhCountBullet(void) { return 0; }
void  bhFixPosition(BH_PWORK* ewP, char* datP) { (void)ewP;(void)datP; }
int   bhGetFrameNum(unsigned int fnm_old, unsigned int fnm_new, int fno_now)
                                      { (void)fnm_old;(void)fnm_new;(void)fno_now; return 0; }
/* Without real floor collision, "ground height at pos" = the caller's
 * own y — keeps the player from snapping to 0 or falling forever. */
float bhGetGroundPosition(NJS_POINT3* pos) { return pos ? pos->y : 0.0f; }
void  bhGetObjMotion(BH_PWORK* ewP, int obj_no, float* pos, int* ang)
{
    (void)ewP; (void)obj_no;
    if (pos) { pos[0] = pos[1] = pos[2] = 0.0f; }
    if (ang) { ang[0] = ang[1] = ang[2] = 0; }
}
int  bhSearchNearEnemy(BH_PWORK* pp, int* r, float* h, int* id) { (void)pp;(void)r;(void)h;(void)id; return 0; }
int  bhSearchNearEnemy2(BH_PWORK* pp, int* r, float* h, int* id) { (void)pp;(void)r;(void)h;(void)id; return 0; }
int  bhSearchNearEnemyB(NJS_POINT3* pos, int ay, int ar, float len) { (void)pos;(void)ay;(void)ar;(void)len; return 0; }
int  bhSearchNextEnemy(BH_PWORK* pp, int r, float h) { (void)pp;(void)r;(void)h; return 0; }
void bhSetGunFire(BH_PWORK* pp, int wno, int jno, int hand, int ang) { (void)pp;(void)wno;(void)jno;(void)hand;(void)ang; }
void bhSetLightTab(LGT_WORK* lt, int lno) { (void)lt;(void)lno; }
void bhSetMagazine(BH_PWORK* pp, int wno, int jno, int hand, int ang) { (void)pp;(void)wno;(void)jno;(void)hand;(void)ang; }
void bhSetUseKaidanFlag(BH_PWORK* pp, ATR_WORK* exp, int idx) { (void)pp;(void)exp;(void)idx; }
void bhSetWaterSplash(BH_PWORK* pp, int jno, int type, float sx, float sy, float sz)
                                      { (void)pp;(void)jno;(void)type;(void)sx;(void)sy;(void)sz; }
void bhSetWaterSplash3(NJS_POINT3* pos, int ang, int type, float sx, float sy, float sz)
                                      { (void)pos;(void)ang;(void)type;(void)sx;(void)sy;(void)sz; }
void bhSetYakkyou(BH_PWORK* pp, int wno, int jno, int hand, int ang) { (void)pp;(void)wno;(void)jno;(void)hand;(void)ang; }
void bhSetWeapon(O_WRK* op, int wpn_no, int flg) { (void)op;(void)wpn_no;(void)flg; }

/* light.c's light-definition table — player.c indexes it for the room
 * lighting setup. Zeroed placeholder until light.c is compiled. */
LGT_WORK lgttab[5];

/* Declared in ps2_NaColi.h but never defined in the decomp (not matched
 * yet). Real implementation — ps2_NaColi.c's capsule-vs-box tests call it.
 * Returns nonzero when pPC lies inside the planar quad P1-P2-P3-P4: the
 * cross products edge x (point - vertex) must all point the same side. */
int njCheckPlane4IncludePoint(NJS_POINT3* pP1, NJS_POINT3* pP2, NJS_POINT3* pP3, NJS_POINT3* pP4, NJS_POINT3* pPC)
{
    NJS_POINT3* v[4];
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    int i;

    v[0] = pP1; v[1] = pP2; v[2] = pP3; v[3] = pP4;

    for (i = 0; i < 4; ++i)
    {
        NJS_POINT3* a = v[i];
        NJS_POINT3* b = v[(i + 1) & 3];
        float ex = b->x - a->x, ey = b->y - a->y, ez = b->z - a->z;
        float px = pPC->x - a->x, py = pPC->y - a->y, pz = pPC->z - a->z;
        float cx = ey * pz - ez * py;
        float cy = ez * px - ex * pz;
        float cz = ex * py - ey * px;

        if (i == 0) { rx = cx; ry = cy; rz = cz; }
        else if (cx * rx + cy * ry + cz * rz < 0.0f) return 0;
    }

    return 1;
}
