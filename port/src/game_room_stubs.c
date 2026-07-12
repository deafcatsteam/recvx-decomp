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
/* bhCheckCut now real (cut.c). bhCheckEnemies/bhCheckPlayer/bhCheckWall/
 * bhCheckWall2Box/bhCheckWallType/bhCheckWallType2/bhResetAtariAttr now
 * real (hitchk.c). bhCheckFloorNum/bhCheckL2Wall/bhSetFloorNum/
 * bhCheckClipModel now real (pwksub.c). */

/* ---- effects (effect.c) ---------------------------------------------- */
void bhClearEffect(void)              {}
int  bhSetEffect(int effno, POINT* pnt, unsigned char* lkp, int lkono)
                                      { (void)effno;(void)pnt;(void)lkp;(void)lkono; return 0; }
int  bhSetEffectTb(EF_WORK* efp, NJS_POINT3* off, unsigned char* lkp, int lkono)
                                      { (void)efp;(void)off;(void)lkp;(void)lkono; return 0; }
/* bhSetExplosion now real (weapon.c). */

/* ---- player / model / motion (player.c, MdlPut.c, motion) ------------ */
/* bhActionWeapon/bhObjWpn now real (weapon.c). */
/* bhCalcHair now real (player.c). */
/* bhCalcModel / bhPutModel / bhCalcTree now real (MdlPut.c). */
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

/* ---- ninja chunk lighting: real setters (matched, moved from
 * ps2_NinjaCnk.c since that file is otherwise saturated with VU1 DMA/EE
 * inline asm we can't compile). Consumed by ninja_cnk.c's CPU-side
 * Lambertian shading — see cnk_shade_vertex. ---------------------------- */
CNK_LIGHT NaCnkLightEs = { 1.401298464f, 0, 1.0f, 10.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0 };
VU1_COLOR NaCnkAmbientEs = { 1.0f, 1.0f, 1.0f, 1.0f };

void njCnkSetEasyLight(Float x, Float y, Float z)
{
    NaCnkLightEs.fCx = -x;
    NaCnkLightEs.fCy = -y;
    NaCnkLightEs.fCz = -z;
}
void njCnkSetEasyLightIntensity(Float inten, Float ambient)
{
    NaCnkLightEs.fI = inten;
    NaCnkAmbientEs.fB = ambient;
    NaCnkAmbientEs.fG = ambient;
    NaCnkAmbientEs.fR = ambient;
}
void njCnkSetEasyLightColor(Float r, Float g, Float b)
{
    NaCnkLightEs.fR = r;
    NaCnkLightEs.fG = g;
    NaCnkLightEs.fB = b;
}

/* "Simple" chunk models use the same single-light VU0 register set as
 * "Easy" ones on real hardware (only the vertex format differs) — our
 * cnk_shade_vertex doesn't distinguish the two, so these setters share
 * NaCnkLightEs/NaCnkAmbientEs with the Easy versions above. */
void njCnkSetSimpleLight(Float x, Float y, Float z)
{
    NaCnkLightEs.fCx = -x;
    NaCnkLightEs.fCy = -y;
    NaCnkLightEs.fCz = -z;
}
void njCnkSetSimpleLightIntensity(Float inten, Float ambient)
{
    NaCnkLightEs.fI = inten;
    NaCnkAmbientEs.fB = ambient;
    NaCnkAmbientEs.fG = ambient;
    NaCnkAmbientEs.fR = ambient;
}
void njCnkSetSimpleLightColor(Float r, Float g, Float b)
{
    NaCnkLightEs.fR = r;
    NaCnkLightEs.fG = g;
    NaCnkLightEs.fB = b;
}

/* Point/spot multi-lights (torches, muzzle flashes, etc.) — not part of
 * this pass. cnk_shade_vertex only models the single directional light
 * above, so these stay no-op like before; real point-light rendering is
 * unimplemented, not just unwired. */
void njCnkSetEasyMultiLight(Int num) { (void)num; }
void njCnkSetEasyMultiLightSwitch(Int light, Int flag) { (void)light;(void)flag; }
void njCnkSetEasyMultiLightColor(Int light, Float lr, Float lg, Float lb) { (void)light;(void)lr;(void)lg;(void)lb; }
void njCnkSetEasyMultiLightVector(Float vx, Float vy, Float vz) { (void)vx;(void)vy;(void)vz; }
void njCnkSetEasyMultiLightPoint(Int light, Float px, Float py, Float pz) { (void)light;(void)px;(void)py;(void)pz; }
void njCnkSetEasyMultiLightRange(Int light, Float nrange, Float frange) { (void)light;(void)nrange;(void)frange; }
void njCnkSetEasyMultiLightMatrices(void) {}
void njCnkSetSimpleMultiAmbient(Float ar, Float ag, Float ab) { (void)ar;(void)ag;(void)ab; }
void njCnkSetSimpleMultiLight(Int num) { (void)num; }
void njCnkSetSimpleMultiLightColor(Int light, Float lr, Float lg, Float lb) { (void)light;(void)lr;(void)lg;(void)lb; }
void njCnkSetSimpleMultiLightVector(Float vx, Float vy, Float vz) { (void)vx;(void)vy;(void)vz; }
void njCnkSetSimpleMultiLightPoint(Int light, Float px, Float py, Float pz) { (void)light;(void)px;(void)py;(void)pz; }
void njCnkSetSimpleMultiLightRange(Int light, Float nrange, Float frange) { (void)light;(void)nrange;(void)frange; }
void njCnkSetSimpleMultiLightMatrices(void) {}
void njCnkSetSimpleMultiLightSwitch(Int light, Int flag) { (void)light;(void)flag; }

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
/* PlyPchInit/PlyPchMain/bhArmIkMdk/bhCPM2_SearchPch/bhCPM2_act_atk_pch/
 * bhCPM2_act_suw_pch/bhCPM2_act_wsc_pch now real (playpch.c). */
void bhCPM0_event(void) {}
void bhCPM2_act_scp(void) {}
void bhCalcFixOffset(BH_PWORK* ewP, char* datP, NJS_POINT3* offP, NJS_POINT3* rtnP)
{
    (void)ewP; (void)datP; (void)offP;
    if (rtnP) { rtnP->x = 0.0f; rtnP->y = 0.0f; rtnP->z = 0.0f; }
}
/* bhAddSpeed/bhCalcLockEneYR/bhSearchNearEnemy/bhSearchNearEnemy2/
 * bhSearchNearEnemyB/bhSearchNextEnemy/bhSetGunFire/bhSetMagazine/
 * bhSetWaterSplash/bhSetWaterSplash3/bhSetYakkyou now real (pwksub.c).
 * bhCheckBullet/bhCheckGunAtari/bhCheckKnifeAtari/bhCountBullet/
 * bhSetWeapon now real (weapon.c). */
void  bhFixPosition(BH_PWORK* ewP, char* datP) { (void)ewP;(void)datP; }
void  bhGetObjMotion(BH_PWORK* ewP, int obj_no, float* pos, int* ang)
{
    (void)ewP; (void)obj_no;
    if (pos) { pos[0] = pos[1] = pos[2] = 0.0f; }
    if (ang) { ang[0] = ang[1] = ang[2] = 0; }
}

/* MdlPut.c dependencies not yet real:
 *   njDrawModel  — basic (non-chunk) NJS_MODEL drawer; no room/player/
 *                  enemy model in this game uses the non-chunk format,
 *                  so a no-op is safe (nothing currently reaches it).
 *   npPushMdlstr/npPopMdlstr — skin vertex-buffer save/restore around a
 *                  skinned draw; no-op is safe without real skinning.
 *   npCalcSkin/npCalcSkinFM  — GPU-skin blend matrices; no-op leaves
 *                  skinned models in bind pose instead of crashing. */
void njDrawModel(NJS_MODEL* model)                   { (void)model; }
void npPushMdlstr(NJS_CNK_OBJECT* objp, int obj_n)   { (void)objp;(void)obj_n; }
void npPopMdlstr(NJS_CNK_OBJECT* objp, int obj_n)    { (void)objp;(void)obj_n; }
void npCalcSkinFM(void* pwp, int obj_n, int* sknp)   { (void)pwp;(void)obj_n;(void)sknp; }
void npCalcSkin(void* pwp, int obj_n, int* sknp)     { (void)pwp;(void)obj_n;(void)sknp; }

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
