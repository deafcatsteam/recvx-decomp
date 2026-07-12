/*
 * Sega Ninja 3D math + matrix stack — port implementation.
 *
 * The real PS2 versions live in src/ps2/veronica/prog/ps2_NaMatrix.c
 * (1986 lines, full of EE/VU0 inline assembly that MSVC x64 can't
 * compile) and src/ps2/veronica/prog/ps2_NinjaCnk.c (chunked-model
 * renderer that depends on GS DMA packets). Rather than try to
 * compile those, we reimplement the API in standard C against the
 * NJS_MATRIX = float[16] layout from include/recvx-decomp-katana/
 * KATANA/Include/ninjadef.h:61.
 *
 * Matrix layout: row-major 4x4. Memory order m[0..3] = row 0, etc.
 * Row 3 = (Tx, Ty, Tz, 1) holds the translation. This matches what
 * the PS2 asm path does (njTranslate writes vf11 to offset 0x30,
 * which is row 3).
 *
 * BAMS angles: 16-bit signed int, 0x4000 = 90°. We convert to
 * radians via (angle / 65536.0f) * 2π.
 */

/* Match game_texture_stubs.c include order: ninja headers FIRST so
 * KATANA's CRT shadow guards in the prelude fire correctly, then
 * stdlib. Including stdbool.h or anything that defines `bool` before
 * sg_xpt.h causes "error C2332: enum: missing tag name" because the
 * KATANA header declares `enum bool { false, true }`. */
#include "ninja.h"
#include "types.h"   /* ML_WORK, O_WORK */

#include <math.h>
#include <string.h>
#include <stdint.h>  /* uintptr_t */

/* ------------------------------------------------------------------ */
/* Matrix stack                                                       */
/* ------------------------------------------------------------------ */

/* These mirror the static globals in ps2_NaMatrix.c so the API
 * matches and callers that read pNaMatMatrixStuckPtr (none in our
 * compiled set, but just in case) see the same state. */
int          lNaMatIsUnitMatrix    = 0;
int          lNaMatMatrixStuckMax  = 0;
int          lNaMatMatrixStuckCnt  = 0;
NJS_MATRIX*  pNaMatMatrixStuckPtr  = NULL;
NJS_MATRIX*  pNaMatMatrixStuckTop  = NULL;

static void mat_copy(float* dst, const float* src) {
    memcpy(dst, src, sizeof(NJS_MATRIX));
}

static void mat_identity(float* m) {
    m[0]=1; m[1]=0; m[2]=0; m[3]=0;
    m[4]=0; m[5]=1; m[6]=0; m[7]=0;
    m[8]=0; m[9]=0; m[10]=1; m[11]=0;
    m[12]=0; m[13]=0; m[14]=0; m[15]=1;
}

/* Result = a * b. Standard row-major 4x4 multiply. */
static void mat_mul(float* result, const float* a, const float* b) {
    float r[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i*4+j] = a[i*4+0]*b[0*4+j]
                     + a[i*4+1]*b[1*4+j]
                     + a[i*4+2]*b[2*4+j]
                     + a[i*4+3]*b[3*4+j];
        }
    }
    memcpy(result, r, sizeof r);
}

static float bams_to_rad(int ang) {
    /* BAMS: 0x10000 = 360°. Mask to 16-bit then convert. */
    ang &= 0xFFFF;
    return (float)ang * (6.28318530718f / 65536.0f);
}

void njInitMatrix(NJS_MATRIX* m, Sint32 n, Int flag) {
    pNaMatMatrixStuckTop  = m;
    pNaMatMatrixStuckPtr  = m;
    lNaMatMatrixStuckCnt  = 0;
    lNaMatMatrixStuckMax  = n;
    lNaMatIsUnitMatrix    = flag;
    /* Initialize top to identity so transforms start from a sane state. */
    if (m) mat_identity((float*)m);
}

/* KATANA signature is `void njClearMatrix()` — NO parameters. The real
 * impl (ps2_NaMatrix.c:224) resets the matrix stack to its base and
 * loads the view matrix into the current slot:
 *
 *   lNaMatMatrixStuckCnt = 0;
 *   pNaMatMatrixStuckPtr = pNaMatMatrixStuckTop;
 *   njSetMatrix(NULL, &NaViwViewMatrix);
 *
 * We don't maintain a real NaViwViewMatrix (njSetView/njInitView are
 * still stubs), so substitute identity — itemview's camera setup
 * applies njRotate/njTranslate immediately after njClearMatrix to
 * build the view transform from scratch anyway.
 *
 * The previous signature took an NJS_MATRIX* param that doesn't exist
 * in the ABI; callers invoke njClearMatrix() with no args, so the impl
 * read a garbage register as the pointer and wrote identity through it
 * (AV writing to ~0x1). */
void njClearMatrix(void) {
    lNaMatMatrixStuckCnt = 0;
    if (pNaMatMatrixStuckTop) pNaMatMatrixStuckPtr = pNaMatMatrixStuckTop;
    if (pNaMatMatrixStuckPtr) mat_identity((float*)pNaMatMatrixStuckPtr);
}

void njUnitMatrix(NJS_MATRIX* m) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (m) mat_identity((float*)m);
}

void njSetMatrix(NJS_MATRIX* md, NJS_MATRIX* ms) {
    /* Real signature is two-arg copy md<-ms. Some callers pass NULL md
     * meaning "load into current top". Some only pass one arg through
     * a NULL trick — but we always have ms when not NULL. */
    if (!md) md = pNaMatMatrixStuckPtr;
    if (!md || !ms) return;
    mat_copy((float*)md, (float*)ms);
}

void njGetMatrix(NJS_MATRIX* m) {
    if (!m || !pNaMatMatrixStuckPtr) return;
    mat_copy((float*)m, (float*)pNaMatMatrixStuckPtr);
}

void njMultiMatrix(NJS_MATRIX* md, NJS_MATRIX* ms) {
    if (!md) md = pNaMatMatrixStuckPtr;
    if (!md || !ms) return;
    mat_mul((float*)md, (float*)md, (float*)ms);
}

void njTransposeMatrix(NJS_MATRIX* m) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    for (int i = 0; i < 4; ++i)
        for (int j = i+1; j < 4; ++j) {
            float t = a[i*4+j]; a[i*4+j] = a[j*4+i]; a[j*4+i] = t;
        }
}

/* Push current top onto the stack — advances pNaMatMatrixStuckPtr
 * forward by one NJS_MATRIX. The new top is a copy of the previous.
 * KATANA signature returns Bool (Sint32) — 1 on success, 0 on stack
 * overflow. */
Bool njPushMatrix(NJS_MATRIX* m) {
    if (!pNaMatMatrixStuckPtr) return 0;
    if (lNaMatMatrixStuckCnt + 1 >= lNaMatMatrixStuckMax) return 0;
    NJS_MATRIX* prev = pNaMatMatrixStuckPtr;
    pNaMatMatrixStuckPtr = pNaMatMatrixStuckPtr + 1;
    lNaMatMatrixStuckCnt++;
    if (m) mat_copy((float*)pNaMatMatrixStuckPtr, (float*)m);
    else   mat_copy((float*)pNaMatMatrixStuckPtr, (float*)prev);
    return 1;
}

Bool njPopMatrix(Uint32 n) {
    Uint32 popped = 0;
    while (n && lNaMatMatrixStuckCnt > 0) {
        pNaMatMatrixStuckPtr = pNaMatMatrixStuckPtr - 1;
        lNaMatMatrixStuckCnt--;
        n--;
        popped++;
    }
    return popped > 0;
}

/* Ex variants: push a copy of the current top / pop one level. */
Bool njPushMatrixEx(void) { return njPushMatrix(NULL); }
Bool njPopMatrixEx(void)  { return njPopMatrix(1); }

void njTranslate(NJS_MATRIX* m, Float x, Float y, Float z) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    /* Post-multiply m by translation T (so result = m * T).
     * For row-major, this updates row 3 = row0*x + row1*y + row2*z + row3.
     * Matches the PS2 asm at ps2_NaMatrix.c:467-471 exactly. */
    a[12] += a[0]*x + a[4]*y + a[8]*z;
    a[13] += a[1]*x + a[5]*y + a[9]*z;
    a[14] += a[2]*x + a[6]*y + a[10]*z;
}

void njRotateX(NJS_MATRIX* m, Angle ang) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    float r = bams_to_rad(ang);
    float c = cosf(r), s = sinf(r);
    /* Row 1 and Row 2 rotate around X. */
    float r10 = a[4], r11 = a[5], r12 = a[6], r13 = a[7];
    float r20 = a[8], r21 = a[9], r22 = a[10], r23 = a[11];
    a[4] = c*r10 + s*r20; a[5] = c*r11 + s*r21; a[6] = c*r12 + s*r22; a[7] = c*r13 + s*r23;
    a[8] = -s*r10 + c*r20; a[9] = -s*r11 + c*r21; a[10] = -s*r12 + c*r22; a[11] = -s*r13 + c*r23;
}

void njRotateY(NJS_MATRIX* m, Angle ang) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    float r = bams_to_rad(ang);
    float c = cosf(r), s = sinf(r);
    float r00 = a[0], r01 = a[1], r02 = a[2], r03 = a[3];
    float r20 = a[8], r21 = a[9], r22 = a[10], r23 = a[11];
    a[0] = c*r00 - s*r20; a[1] = c*r01 - s*r21; a[2] = c*r02 - s*r22; a[3] = c*r03 - s*r23;
    a[8] = s*r00 + c*r20; a[9] = s*r01 + c*r21; a[10] = s*r02 + c*r22; a[11] = s*r03 + c*r23;
}

void njRotateZ(NJS_MATRIX* m, Angle ang) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    float r = bams_to_rad(ang);
    float c = cosf(r), s = sinf(r);
    float r00 = a[0], r01 = a[1], r02 = a[2], r03 = a[3];
    float r10 = a[4], r11 = a[5], r12 = a[6], r13 = a[7];
    a[0] = c*r00 + s*r10; a[1] = c*r01 + s*r11; a[2] = c*r02 + s*r12; a[3] = c*r03 + s*r13;
    a[4] = -s*r00 + c*r10; a[5] = -s*r01 + c*r11; a[6] = -s*r02 + c*r12; a[7] = -s*r03 + c*r13;
}

void njScale(NJS_MATRIX* m, Float sx, Float sy, Float sz) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    a[0]*=sx; a[1]*=sx; a[2]*=sx; a[3]*=sx;
    a[4]*=sy; a[5]*=sy; a[6]*=sy; a[7]*=sy;
    a[8]*=sz; a[9]*=sz; a[10]*=sz; a[11]*=sz;
}

/* KATANA: Float njScalor(NJS_VECTOR* v) — returns vector length. */
Float njScalor(NJS_VECTOR* v) {
    if (!v) return 0.0f;
    return sqrtf(v->x*v->x + v->y*v->y + v->z*v->z);
}

/* Transform a single point: out = in * m (post-multiply for row-major). */
void njCalcPoint(NJS_MATRIX* m, NJS_POINT3* ps, NJS_POINT3* pd) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m || !ps || !pd) return;
    float* a = (float*)m;
    float x = ps->x, y = ps->y, z = ps->z;
    pd->x = x*a[0] + y*a[4] + z*a[8]  + a[12];
    pd->y = x*a[1] + y*a[5] + z*a[9]  + a[13];
    pd->z = x*a[2] + y*a[6] + z*a[10] + a[14];
}

/* Batch transform. */
void njCalcPoints(NJS_MATRIX* m, NJS_POINT3* ps, NJS_POINT3* pd, Int num) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m || !ps || !pd) return;
    for (int i = 0; i < num; ++i) njCalcPoint(m, &ps[i], &pd[i]);
}

/* Screen-space offset applied by njProjectScreen below (view shake etc.
 * on PS2). Nothing in the compiled port drives these yet, so they sit
 * at their PS2 power-on default of 0. */
Float fNaViwOffsetX = 0.0f;
Float fNaViwOffsetY = 0.0f;

/* The real ps2_NaMatrix.c version multiplies m into a screen matrix
 * (NaViewScreenMatrix) via VU0 asm, then does a VU0 perspective divide
 * (Q = 1/vf18z) before adding the view offset. We don't have
 * NaViewScreenMatrix ported, so this reimplements the same shape —
 * transform by m, divide x/y by camera-space z — using the existing
 * CPU njCalcPoint. Only consumer once light.c compiles is the
 * point-light visibility cull in bhControlLight, which only gates
 * whether a point light gets recorded into lg_ptb/lg_pnt; point-light
 * rendering itself is still stubbed no-op, so approximation here is
 * inert for on-screen output. */
void njProjectScreen(NJS_MATRIX* m, NJS_POINT3* p3, NJS_POINT2* p2) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m || !p3 || !p2) return;
    NJS_POINT3 view;
    njCalcPoint(m, p3, &view);
    Float q = (view.z != 0.0f) ? (1.0f / view.z) : 0.0f;
    p2->x = view.x * q + fNaViwOffsetX;
    p2->y = view.y * q + fNaViwOffsetY;
}

void njGetTranslation(NJS_MATRIX* m, NJS_POINT3* p) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m || !p) return;
    float* a = (float*)m;
    p->x = a[12]; p->y = a[13]; p->z = a[14];
}

/* Sin/Cos lookup via standard math. The real Ninja uses a precomputed
 * table indexed by BAMS angle; result is functionally identical. */
void njSinCos(Angle ang, Float* sin_out, Float* cos_out) {
    float r = bams_to_rad(ang);
    if (sin_out) *sin_out = sinf(r);
    if (cos_out) *cos_out = cosf(r);
}

Float njSin(Angle ang) { return sinf(bams_to_rad(ang)); }
Float njCos(Angle ang) { return cosf(bams_to_rad(ang)); }

/* ps2_NaMath.c:230 njSqrt is a VU0 vsqrt — plain sqrtf here. */
Float njSqrt(Float n) { return sqrtf(n); }

/* ps2_NaMath.c:258 — VU0 vrsqrt. */
Float njInvertSqrt(Float n) { return (n > 0.0f) ? 1.0f / sqrtf(n) : 0.0f; }

/* ps2_NaMatrix.c:649 — composition is Z, then Y, then X (matches the
 * real C body exactly). */
void njRotateXYZ(NJS_MATRIX* m, Angle angx, Angle angy, Angle angz) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    njRotateZ(m, angz);
    njRotateY(m, angy);
    njRotateX(m, angx);
}

/* Rotate a vector by m's 3x3 part — njCalcPoint without the
 * translation row (VU0 vmulax/vmadday/vmaddz in ps2_NaMatrix.c:1420). */
void njCalcVector(NJS_MATRIX* m, NJS_VECTOR* vs, NJS_VECTOR* vd) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m || !vs || !vd) return;
    float* a = (float*)m;
    float x = vs->x, y = vs->y, z = vs->z;
    vd->x = x*a[0] + y*a[4] + z*a[8];
    vd->y = x*a[1] + y*a[5] + z*a[9];
    vd->z = x*a[2] + y*a[6] + z*a[10];
}

/* Normalize in place, return the original length
 * (ps2_NaMatrix.c:1462 vrsqrt path). */
Float njUnitVector(NJS_VECTOR* v) {
    if (!v) return 0.0f;
    float len = sqrtf(v->x*v->x + v->y*v->y + v->z*v->z);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        v->x *= inv; v->y *= inv; v->z *= inv;
    }
    return len;
}

Float njInnerProduct(NJS_VECTOR* v1, NJS_VECTOR* v2) {
    if (!v1 || !v2) return 0.0f;
    return v1->x*v2->x + v1->y*v2->y + v1->z*v2->z;
}

/* Re-normalize the 3x3 rotation rows (drift cleanup after repeated
 * incremental rotates — VU0 vrsqrt path on PS2). */
void njUnitRotPortion(NJS_MATRIX* m) {
    if (!m) m = pNaMatMatrixStuckPtr;
    if (!m) return;
    float* a = (float*)m;
    for (int r = 0; r < 3; ++r) {
        float x = a[r*4], y = a[r*4+1], z = a[r*4+2];
        float len = sqrtf(x*x + y*y + z*z);
        if (len > 0.0f) { a[r*4] = x/len; a[r*4+1] = y/len; a[r*4+2] = z/len; }
    }
}

/* bhKeepObjWork now comes from the real dread.c (its `(int)sp` align
 * was converted to uintptr_t there). */
