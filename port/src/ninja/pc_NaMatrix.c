/*
 * PC replacement for src/ps2/veronica/prog/ps2_NaMatrix.c.
 *
 * Ninja matrices use row vectors: a point is transformed as
 *     p' = p.x * row0 + p.y * row1 + p.z * row2 + row3
 * so row3 holds the translation, and "applying" a transform T to the current
 * matrix M means M = T * M.
 *
 * The PS2 versions are VU0 assembly; these C translations keep their results.
 * Where the assembly only writes x/y/z of a row, the w component is left
 * untouched here (on PS2 it receives whatever was in the VU register).
 */
#include "ps2_NaMatrix.h"
#include "ps2_NaMath.h"
#include "ps2_NaFog.h"
#include "ps2_NaView.h"
#include "ps2_dummy.h"
#include "main.h"

#define MAT(m, r, c) ((*(m))[(r) * 4 + (c)])

int lNaMatIsUnitMatrix;
int lNaMatMatrixStuckMax;
int lNaMatMatrixStuckCnt;
NJS_MATRIX* pNaMatMatrixStuckPtr;
NJS_MATRIX* pNaMatMatrixStuckTop;

/* VU0 registers vf28-vf31: the combined matrix used by the *CN functions. */
static NJS_MATRIX MatrixCN;

/* Computes the row-vector product p * m (rows 0-2) + row 3 * w. */
static void CalcRow(NJS_MATRIX *m, float x, float y, float z, float w, float *out)
{
    int c;

    for (c = 0; c < 3; c++)
    {
        out[c] = x * MAT(m, 0, c) + y * MAT(m, 1, c) + z * MAT(m, 2, c) + w * MAT(m, 3, c);
    }
}

static NJS_MATRIX *Current(NJS_MATRIX *m)
{
    return m != NULL ? m : pNaMatMatrixStuckPtr;
}

void njInitMatrix(NJS_MATRIX *m, Sint32 n, Int flag)
{
    pNaMatMatrixStuckTop = m;
    pNaMatMatrixStuckPtr = m;

    lNaMatMatrixStuckCnt = 0;
    lNaMatMatrixStuckMax = n;

    lNaMatIsUnitMatrix = flag;
}

void njCalcPoints(NJS_MATRIX *m, NJS_POINT3 *ps, NJS_POINT3 *pd, Int num)
{
    float out[3];

    m = Current(m);

    for ( ; num > 0; num--, ps++, pd++)
    {
        CalcRow(m, ps->x, ps->y, ps->z, 1.0f, out);

        pd->x = out[0];
        pd->y = out[1];
        pd->z = out[2];
    }
}

void njGetTranslation(NJS_MATRIX *m, NJS_POINT3 *p)
{
    m = Current(m);

    p->x = MAT(m, 3, 0);
    p->y = MAT(m, 3, 1);
    p->z = MAT(m, 3, 2);
}

void njUnitTransPortion(NJS_MATRIX *m)
{
    m = Current(m);

    MAT(m, 3, 0) = 0.0f;
    MAT(m, 3, 1) = 0.0f;
    MAT(m, 3, 2) = 0.0f;
    MAT(m, 3, 3) = 1.0f;
}

void njUnitRotPortion(NJS_MATRIX *m)
{
    int r, c;

    m = Current(m);

    for (r = 0; r < 3; r++)
    {
        for (c = 0; c < 4; c++)
        {
            MAT(m, r, c) = (r == c) ? 1.0f : 0.0f;
        }
    }
}

void njClearMatrix()
{
    lNaMatMatrixStuckCnt = 0;

    pNaMatMatrixStuckPtr = pNaMatMatrixStuckTop;

    njSetMatrix(NULL, &NaViwViewMatrix);
}

Bool njPushMatrix(NJS_MATRIX *m)
{
    NJS_MATRIX* fpSrc;

    if (lNaMatMatrixStuckMax <= lNaMatMatrixStuckCnt)
    {
        return FALSE;
    }

    lNaMatMatrixStuckCnt++;

    fpSrc = Current(m);

    pNaMatMatrixStuckPtr++;

    memcpy(pNaMatMatrixStuckPtr, fpSrc, sizeof(NJS_MATRIX));

    return TRUE;
}

Bool njPopMatrix(Uint32 n)
{
    int lNumber;

    lNumber = lNaMatMatrixStuckCnt - n;

    if (lNumber < 0)
    {
        return FALSE;
    }

    lNaMatMatrixStuckCnt = lNumber;

    pNaMatMatrixStuckPtr -= n;

    return TRUE;
}

void njUnitMatrix(NJS_MATRIX *m)
{
    int i;

    m = Current(m);

    for (i = 0; i < 16; i++)
    {
        (*m)[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

void njSetMatrix(NJS_MATRIX *md, NJS_MATRIX *ms)
{
    memmove(Current(md), ms, sizeof(NJS_MATRIX));
}

void njSetMatrixCN(NJS_MATRIX* pMat)
{
    memcpy(&MatrixCN, pMat, sizeof(NJS_MATRIX));
}

void njGetMatrix(NJS_MATRIX *m)
{
    memmove(m, pNaMatMatrixStuckPtr, sizeof(NJS_MATRIX));
}

/* md = ms * md (x/y/z columns; the w column keeps ms's values). */
void njMultiMatrix(NJS_MATRIX *md, NJS_MATRIX *ms)
{
    NJS_MATRIX res;
    int r;

    md = Current(md);

    for (r = 0; r < 4; r++)
    {
        CalcRow(md, MAT(ms, r, 0), MAT(ms, r, 1), MAT(ms, r, 2), (r == 3) ? 1.0f : 0.0f, &res[r * 4]);

        res[r * 4 + 3] = MAT(ms, r, 3);
    }

    memcpy(md, &res, sizeof(NJS_MATRIX));
}

void njTranslate(NJS_MATRIX *m, Float x, Float y, Float z)
{
    float out[3];

    m = Current(m);

    CalcRow(m, x, y, z, 1.0f, out);

    MAT(m, 3, 0) = out[0];
    MAT(m, 3, 1) = out[1];
    MAT(m, 3, 2) = out[2];
}

void njTranslateV(NJS_MATRIX *m, NJS_VECTOR *v)
{
    njTranslate(m, v->x, v->y, v->z);
}

/* Rotates rows a and b: a' = a*c + b*s, b' = b*c - a*s. */
static void RotateRows(NJS_MATRIX *m, int a, int b, float s, float c)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        float ra = MAT(m, a, i);
        float rb = MAT(m, b, i);

        MAT(m, a, i) = ra * c + rb * s;
        MAT(m, b, i) = rb * c - ra * s;
    }
}

void njRotateX(NJS_MATRIX *m, Angle ang)
{
    float fSin, fCos;

    njSinCos(ang & 0xFFFF, &fSin, &fCos);

    RotateRows(Current(m), 1, 2, fSin, fCos);
}

void njRotateY(NJS_MATRIX *m, Angle ang)
{
    float fSin, fCos;

    njSinCos(ang & 0xFFFF, &fSin, &fCos);

    RotateRows(Current(m), 0, 2, -fSin, fCos);
}

void njRotateZ(NJS_MATRIX *m, Angle ang)
{
    float fSin, fCos;

    njSinCos(ang & 0xFFFF, &fSin, &fCos);

    RotateRows(Current(m), 0, 1, fSin, fCos);
}

void njRotateXYZ(NJS_MATRIX *m, Angle angx, Angle angy, Angle angz)
{
    m = Current(m);

    njRotateZ(m, angz);
    njRotateY(m, angy);
    njRotateX(m, angx);
}

/* Note the order: X first, then Y, then Z (njRotateXYZ does Z, Y, X). */
void njRotXYZ(NJS_MATRIX* pMatrix, int lAngleX, int lAngleY, int lAngleZ)
{
    njRotateX(pMatrix, lAngleX);
    njRotateY(pMatrix, lAngleY);
    njRotateZ(pMatrix, lAngleZ);
}

/* Rotation of `ang` around the axis v, built from a quaternion. */
void njRotate(NJS_MATRIX *m, NJS_VECTOR *v, Angle ang)
{
    NJS_MATRIX rot;
    float fSin, fCos;
    float len2, inv;
    float x, y, z, w;
    float xx, yy, zz, ww;

    m = Current(m);

    ang &= 0xFFFF;
    ang /= 2;

    njSinCos(ang, &fSin, &fCos);

    len2 = v->x * v->x + v->y * v->y + v->z * v->z;
    inv = njInvertSqrt(len2) * fSin;

    x = v->x * inv;
    y = v->y * inv;
    z = v->z * inv;
    w = fCos;

    xx = x * x;
    yy = y * y;
    zz = z * z;
    ww = w * w;

    memset(&rot, 0, sizeof(rot));

    rot[0] = ww + xx - yy - zz;
    rot[1] = 2.0f * (x * y + z * w);
    rot[2] = 2.0f * (x * z - y * w);

    rot[4] = 2.0f * (x * y - z * w);
    rot[5] = ww - xx + yy - zz;
    rot[6] = 2.0f * (y * z + x * w);

    rot[8] = 2.0f * (x * z + y * w);
    rot[9] = 2.0f * (y * z - x * w);
    rot[10] = ww - xx - yy + zz;

    {
        NJS_MATRIX res;
        int r;

        memcpy(&res, m, sizeof(res));

        for (r = 0; r < 3; r++)
        {
            CalcRow(m, rot[r * 4], rot[r * 4 + 1], rot[r * 4 + 2], 0.0f, &res[r * 4]);
        }

        memcpy(m, &res, sizeof(res));
    }
}

void njScale(NJS_MATRIX *m, Float sx, Float sy, Float sz)
{
    int i;

    m = Current(m);

    for (i = 0; i < 3; i++)
    {
        MAT(m, 0, i) *= sx;
        MAT(m, 1, i) *= sy;
        MAT(m, 2, i) *= sz;
    }
}

void njScaleV(NJS_MATRIX *m, NJS_VECTOR *v)
{
    njScale(m, v->x, v->y, v->z);
}

/* Inverse of a rigid transform only (rotation + translation): the rotation
 * part is transposed and the translation rotated back. This is what the PS2
 * version does; it is not a general 4x4 inverse. */
Bool njInvertMatrix(NJS_MATRIX *m)
{
    NJS_MATRIX res;
    float t[3];
    int r, c;

    m = Current(m);

    for (r = 0; r < 3; r++)
    {
        for (c = 0; c < 3; c++)
        {
            res[r * 4 + c] = MAT(m, c, r);
        }

        res[r * 4 + 3] = 0.0f;
    }

    t[0] = MAT(m, 3, 0);
    t[1] = MAT(m, 3, 1);
    t[2] = MAT(m, 3, 2);

    for (c = 0; c < 3; c++)
    {
        res[12 + c] = -(t[0] * res[c] + t[1] * res[4 + c] + t[2] * res[8 + c]);
    }

    res[15] = MAT(m, 3, 3);

    memcpy(m, &res, sizeof(res));

    return TRUE;
}

void njTransposeMatrix(NJS_MATRIX *m)
{
    float tmp;
    int r, c;

    m = Current(m);

    for (r = 0; r < 3; r++)
    {
        for (c = r + 1; c < 3; c++)
        {
            tmp = MAT(m, r, c);

            MAT(m, r, c) = MAT(m, c, r);
            MAT(m, c, r) = tmp;
        }
    }
}

/* Reflection through the plane (point p, normal v): m = mirror * m. */
void njMirror(NJS_MATRIX *m, NJS_PLANE *pl)
{
    NJS_MATRIX mat;
    float nx, ny, nz, inv, d;

    m = Current(m);

    inv = njInvertSqrt(pl->vx * pl->vx + pl->vy * pl->vy + pl->vz * pl->vz);

    nx = pl->vx * inv;
    ny = pl->vy * inv;
    nz = pl->vz * inv;

    d = nx * pl->px + ny * pl->py + nz * pl->pz;

    memset(&mat, 0, sizeof(mat));

    mat[0] = 1.0f - 2.0f * nx * nx;
    mat[1] = -2.0f * nx * ny;
    mat[2] = -2.0f * nx * nz;

    mat[4] = -2.0f * ny * nx;
    mat[5] = 1.0f - 2.0f * ny * ny;
    mat[6] = -2.0f * ny * nz;

    mat[8] = -2.0f * nz * nx;
    mat[9] = -2.0f * nz * ny;
    mat[10] = 1.0f - 2.0f * nz * nz;

    mat[12] = 2.0f * d * nx;
    mat[13] = 2.0f * d * ny;
    mat[14] = 2.0f * d * nz;
    mat[15] = 1.0f;

    njMultiMatrix(m, &mat);
}

void njCalcPoint(NJS_MATRIX *m, NJS_POINT3 *ps, NJS_POINT3 *pd)
{
    float out[3];

    CalcRow(Current(m), ps->x, ps->y, ps->z, 1.0f, out);

    pd->x = out[0];
    pd->y = out[1];
    pd->z = out[2];
}

/* The w of the source point is ignored, as on PS2. */
void njCalcPoint4(NJS_MATRIX* pMatrix, NJS_POINT4* pSrcPoint, NJS_POINT4* pDstPoint)
{
    float out[3];

    CalcRow(Current(pMatrix), pSrcPoint->x, pSrcPoint->y, pSrcPoint->z, 1.0f, out);

    pDstPoint->x = out[0];
    pDstPoint->y = out[1];
    pDstPoint->z = out[2];
    pDstPoint->w = pSrcPoint->w;
}

void njCalcPointCN(NJS_POINT3* pSrcPoint, NJS_POINT3* pDstPoint)
{
    float out[3];

    CalcRow(&MatrixCN, pSrcPoint->x, pSrcPoint->y, pSrcPoint->z, 1.0f, out);

    pDstPoint->x = out[0];
    pDstPoint->y = out[1];
    pDstPoint->z = out[2];
}

void njAddVector(NJS_VECTOR *vd, NJS_VECTOR *vs)
{
    vd->x += vs->x;
    vd->y += vs->y;
    vd->z += vs->z;
}

void njSubVector(NJS_VECTOR *vd, NJS_VECTOR *vs)
{
    vd->x -= vs->x;
    vd->y -= vs->y;
    vd->z -= vs->z;
}

void njCalcVector(NJS_MATRIX *m, NJS_VECTOR *vs, NJS_VECTOR *vd)
{
    float out[3];

    CalcRow(Current(m), vs->x, vs->y, vs->z, 0.0f, out);

    vd->x = out[0];
    vd->y = out[1];
    vd->z = out[2];
}

/* Normalizes v and returns its original length. A zero vector stays zero
 * (VU0 arithmetic never produces NaN). */
Float njUnitVector(NJS_VECTOR *v)
{
    float len2 = v->x * v->x + v->y * v->y + v->z * v->z;
    float len;

    if (len2 == 0.0f)
    {
        return 0.0f;
    }

    len = sqrtf(len2);

    v->x /= len;
    v->y /= len;
    v->z /= len;

    return len;
}

Float njScalor(NJS_VECTOR *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

Float njScalor2(NJS_VECTOR *v)
{
    return v->x * v->x + v->y * v->y + v->z * v->z;
}

/* The point is transformed by (current matrix * screen matrix); the CN
 * matrix is left set to that product, as on PS2. */
void njProjectScreen(NJS_MATRIX *m, NJS_POINT3 *p3, NJS_POINT2 *p2)
{
    NJS_POINT3 Point;
    float q;

    njMulMatrixCN(&NaViewScreenMatrix, Current(m));
    njCalcPointCN(p3, &Point);

    q = _nj_screen_.dist / Point.z;

    p2->x = Point.x * q + fNaViwOffsetX;
    p2->y = Point.y * q + fNaViwOffsetY;
}

Float njOuterProduct(NJS_VECTOR *v1, NJS_VECTOR *v2, NJS_VECTOR *ov)
{
    NJS_VECTOR r;

    r.x = v1->y * v2->z - v1->z * v2->y;
    r.y = v1->z * v2->x - v1->x * v2->z;
    r.z = v1->x * v2->y - v1->y * v2->x;

    *ov = r;

    return njScalor(&r);
}

Float njInnerProduct(NJS_VECTOR *v1, NJS_VECTOR *v2)
{
    return v1->x * v2->x + v1->y * v2->y + v1->z * v2->z;
}

void njTranslateEx(NJS_VECTOR *v)
{
    njTranslate(pNaMatMatrixStuckPtr, v->x, v->y, v->z);
}

void njRotateEx(Angle *ang, Sint32 lv)
{
    if (lv != 0)
    {
        njRotateY(NULL, ang[1]);
        njRotateX(NULL, ang[0]);
        njRotateZ(NULL, ang[2]);
    }
    else
    {
        njRotateZ(NULL, ang[2]);
        njRotateY(NULL, ang[1]);
        njRotateX(NULL, ang[0]);
    }
}

/* Unlike njScale, this also scales the w column of rows 0-2. */
void njScaleEx(NJS_VECTOR *v)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        MAT(pNaMatMatrixStuckPtr, 0, i) *= v->x;
        MAT(pNaMatMatrixStuckPtr, 1, i) *= v->y;
        MAT(pNaMatMatrixStuckPtr, 2, i) *= v->z;
    }
}

Bool njPushMatrixEx(void)
{
    return njPushMatrix(NULL);
}

Bool njPopMatrixEx(void)
{
    return njPopMatrix(1);
}

/* Screen x/y, view z and 1/z of a point, plus its fog factor. */
void njRotTransPers(NJS_POINT3* pPoint, NJS_SCRVECTOR* pScreen)
{
    njMulMatrixCN(&NaViewScreenMatrix, NULL);
    njCalcPointCN(pPoint, (NJS_POINT3*)&pScreen->x);

    njPers(pScreen);

    pScreen->fog = njCalcFogPower(pScreen->z);
}

void njRotTrans(NJS_POINT3* pPoint, NJS_POINT3* pOut)
{
    njMulMatrixCN(&NaViewScreenMatrix, NULL);
    njCalcPointCN(pPoint, pOut);
}

void njPers(NJS_SCRVECTOR* pScreen)
{
    float iz = 1.0f / pScreen->z;
    float q = _nj_screen_.dist * iz;

    pScreen->x = pScreen->x * q + fNaViwOffsetX;
    pScreen->y = pScreen->y * q + fNaViwOffsetY;
    pScreen->iz = iz;
}

void njCopyMatrix(NJS_MATRIX* pDstMat, NJS_MATRIX* pSrcMat)
{
    memmove(pDstMat, pSrcMat, sizeof(NJS_MATRIX));
}

/* CN = mat2 * mat1, all four columns. */
void njMulMatrixCN(NJS_MATRIX* pSrcMat1, NJS_MATRIX* pSrcMat2)
{
    NJS_MATRIX res;
    int r, c;

    pSrcMat2 = Current(pSrcMat2);

    for (r = 0; r < 4; r++)
    {
        float x = MAT(pSrcMat2, r, 0);
        float y = MAT(pSrcMat2, r, 1);
        float z = MAT(pSrcMat2, r, 2);
        float w = (r == 3) ? 1.0f : 0.0f;

        for (c = 0; c < 4; c++)
        {
            res[r * 4 + c] = x * MAT(pSrcMat1, 0, c) + y * MAT(pSrcMat1, 1, c) +
                             z * MAT(pSrcMat1, 2, c) + w * MAT(pSrcMat1, 3, c);
        }
    }

    memcpy(&MatrixCN, &res, sizeof(NJS_MATRIX));
}
