/*
 * The 3D drawing helpers that the PS2 runs with VU0: vertex colours, view
 * volume clipping, polygon scissoring and the packing of 3D primitives into
 * GS packets. The originals are EE assembly with VU0 macro instructions, plus
 * VU0 microprograms (vsm/ps2_vu0.vsm); the game keeps them under
 * #ifndef PLATFORM_PC and this file has their C versions.
 *
 * VU0 registers that kept state between calls on the PS2 are globals here:
 *   vf16/vf17/vf20..23  fVu1Offset*, fVu1Aspect*, vu1Diffuse/Specula/Ambient,
 *                       fVu1Projection and fVu1AlphaRatio (ps2_Vu1Strip.c);
 *   vf24..vf27          ClipMatrix2, the view to clip space matrix;
 *   vf15/vf18/vf19      the clip-space positions of the last three vertices;
 *   vi18                the clip flags of the last four CLIP judgements;
 *   VU0 data memory     the polygon scissoring work area.
 */
#include "ps2_dummy.h"
#include "ps2_NaDraw2D.h"
#include "ps2_NaFog.h"
#include "ps2_NaView.h"
#include "ps2_Vu1Scissor2.h"
#include "ps2_Vu1Strip.h"
#include "ps2_loadtim2.h"

/* ---- VU helpers ---------------------------------------------------------- */

/* vclipw.xyz: six bits, +x -x +y -y +z -z outside of |w|. */
static unsigned int clipw(const float* v)
{
    float w = fabsf(v[3]);
    unsigned int f = 0;

    if (v[0] > w) f |= 0x01;
    if (v[0] < -w) f |= 0x02;
    if (v[1] > w) f |= 0x04;
    if (v[1] < -w) f |= 0x08;
    if (v[2] > w) f |= 0x10;
    if (v[2] < -w) f |= 0x20;
    return f;
}

/* View space to clip space with ClipMatrix2 (vf24..vf27), w taken as 1. */
static void to_clip(const float* v, float* out)
{
    int j;

    for (j = 0; j < 4; j++)
    {
        out[j] = ClipMatrix2[0][j] * v[0] + ClipMatrix2[1][j] * v[1] +
                 ClipMatrix2[2][j] * v[2] + ClipMatrix2[3][j];
    }
}

/* vftoi4: float to 12.4 fixed point, truncated and saturated. */
static int ftoi4(float f)
{
    float v = f * 16.0f;

    if (v != v)
        return 0;
    if (v >= 2147483647.0f)
        return 0x7FFFFFFF;
    if (v <= -2147483648.0f)
        return (int)0x80000000;
    return (int)v;
}

/* vftoi0 */
static int ftoi0(float f)
{
    if (f != f)
        return 0;
    if (f >= 2147483647.0f)
        return 0x7FFFFFFF;
    if (f <= -2147483648.0f)
        return (int)0x80000000;
    return (int)f;
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* ---- Clip state (vf15/vf18/vf19 and the clip flag register) -------------- */

static float clip_hist[3][4];   /* oldest (vf19) to newest (vf15) */
static unsigned int clip_flags; /* vi18: 4 judgements of 6 bits, newest low */
static int clip_plane;          /* vi2 of VU0_CLIP_VIEW_VOLUME_ALL */

static void clip_judge(const float* v)
{
    clip_flags = ((clip_flags << 6) | clipw(v)) & 0xFFFFFF;
}

static void clip_reset(void)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        clip_hist[i][0] = clip_hist[i][1] = clip_hist[i][2] = 0.0f;
        clip_hist[i][3] = 1.0f;
    }
}

static void clip_shift(const float* vertex)
{
    memcpy(clip_hist[0], clip_hist[1], sizeof(clip_hist[0]));
    memcpy(clip_hist[1], clip_hist[2], sizeof(clip_hist[1]));
    to_clip(vertex, clip_hist[2]);
}

/* The CLIP of the three triangle vertices, oldest first, as
 * VU0_CLIP_VIEW_VOLUME does. */
static void clip_judge_triangle(void)
{
    clip_judge(clip_hist[0]);
    clip_judge(clip_hist[1]);
    clip_judge(clip_hist[2]);
}

/* VU0_CLIP_VIEW_VOLUME_ALL ends with FCOR tests meant to catch a triangle
 * outside one plane. The masks lack their top bits, so a test only passes
 * with impossible flag combinations and vi2 is 1 in practice: every crossing
 * triangle goes to the scissoring, which then finds it empty if needed. */
static void clip_plane_test(void)
{
    static const unsigned int masks[6] = { 0x0DF7DF, 0x0EFBEF, 0x007DF7, 0x00BEFB, 0x00DF7D, 0x00EFBE };
    int i;

    clip_plane = 1;
    for (i = 0; i < 6; i++)
    {
        if ((clip_flags | masks[i]) == 0xFFFFFF)
        {
            clip_plane = 0;
            return;
        }
    }
}

void InitNodeArraySet2()
{
    clip_reset();
}

unsigned int _Clip_ViewVolume2(NJS_POINT4* vec)
{
    clip_shift(&vec->x);
    clip_judge_triangle();
    return 0;
}

unsigned int _Get_ClipViewVolume2()
{
    return clip_flags;
}

int _Get_ClipVolumePlane()
{
    return clip_plane;
}

void _Check_ClipViewAll(NJS_POINT4* vec)
{
    clip_shift(&vec->x);
    clip_judge_triangle();
    clip_plane_test();
}

int _CVV(float* v0)
{
    float c[4];

    to_clip(v0, c);
    clip_judge(c);
    return clip_flags;
}

int _Clip_ViewVolume(float* clip, float local_clip[4], float* vertex)
{
    to_clip(vertex, clip);
    clip_judge(clip);
    return clip_flags;
}

int _Clip_Screen(float* clip)
{
    float v[4];

    v[0] = (clip[0] - 2048.0f) * 0.75f;
    v[1] = (clip[1] - 2048.0f) * 1.0f;
    v[2] = 0.0f;
    v[3] = 240.0f;
    clip_judge(v);
    return clip_flags;
}

/* ---- Scissoring in VU0 memory (ps2_Vu1Scissor2.c) ------------------------ */

/* A node is 4 qwords: clip-space position, view position and fog, texture
 * UV, colour. The two work lists start at qword 80 and 128. */
static float vu0_mem[256][4];

static void store_node(int addr, const float (*node)[4])
{
    memcpy(vu0_mem[addr], node, 4 * 16);
}

void _Set_NodeArray(VU1_STRIP_BUF* pS, VU1_PRIM_BUF* pP)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        VU1_STRIP_BUF* s = pS - 2 + i;
        VU1_PRIM_BUF* p = pP - 2 + i;

        memcpy(vu0_mem[80 + i * 4 + 0], clip_hist[i], 16);
        memcpy(vu0_mem[80 + i * 4 + 1], &s->fVx, 16);
        memcpy(vu0_mem[80 + i * 4 + 2], &s->fU, 16);
        memcpy(vu0_mem[80 + i * 4 + 3], &p->fR, 16);
    }
    memcpy(vu0_mem[92], vu0_mem[80], 4 * 16);
}

/* Point between a (inside) and b (outside) on the plane comp = sgn * w. */
static void intersect(const float (*a)[4], const float (*b)[4], int comp, float sgn, float (*out)[4])
{
    float d0 = a[0][comp] - sgn * a[0][3];
    float d1 = b[0][comp] - sgn * b[0][3];
    float t = fabsf(d0 / (d1 - d0));
    int q, j;

    for (q = 0; q < 4; q++)
    {
        for (j = 0; j < 4; j++)
        {
            out[q][j] = a[q][j] + (b[q][j] - a[q][j]) * t;
        }
    }
    if (out[1][2] < 1.0f)
    {
        out[1][2] = 1.0f;
    }
}

/* One plane of the Sutherland-Hodgman clipping: mask1/mask2 are the plane's
 * flag for the current and the next node, the result goes to work1 and is
 * closed with a copy of its first node. */
int _ClipInter(int mask1, int mask2, int xyzflg, float sin, int work0, int work1, int count)
{
    int out = work1;
    int n = 0;
    int e;
    float inter[4][4];

    for (e = 0; e < count; e++)
    {
        const float (*a)[4] = (const float (*)[4])vu0_mem[work0 + e * 4];
        const float (*b)[4] = (const float (*)[4])vu0_mem[work0 + e * 4 + 4];
        unsigned int cf = (clipw(a[0]) << 6) | clipw(b[0]);

        if (cf & mask1)
        {
            if (!(cf & mask2))
            {
                intersect(a, b, xyzflg, sin, inter);
                store_node(out, (const float (*)[4])inter);
                out += 4;
                n++;
            }
        }
        else
        {
            store_node(out, a);
            out += 4;
            n++;
            if (cf & mask2)
            {
                intersect(a, b, xyzflg, sin, inter);
                store_node(out, (const float (*)[4])inter);
                out += 4;
                n++;
            }
        }
    }

    memcpy(vu0_mem[out], vu0_mem[work1], 4 * 16);
    return n;
}

/* Projects the scissored polygon at qword 80 and draws it as a fan. */
void DrawScissorPolygonOpaque2(int count, unsigned long ulType)
{
    VU1_PRIM_BUF* pP = vu1ScessorBuf;
    int i;

    for (i = 0; i < count; i++, pP++)
    {
        const float* view = vu0_mem[80 + i * 4 + 1];
        const float* uv = vu0_mem[80 + i * 4 + 2];
        float q = 1.0f / view[2];
        float s = fVu1Projection * q;

        pP->fS = uv[0] * q;
        pP->fT = uv[1] * q;
        pP->fQ = q;
        pP->ulKick = 0;
        memcpy(&pP->fR, vu0_mem[80 + i * 4 + 3], 16);
        pP->fX = view[0] * fVu1AspectW * s + fVu1OffsetX;
        pP->fY = view[1] * fVu1AspectH * s + fVu1OffsetY;
        pP->fZ = view[2];
        pP->fF = view[3];
    }

    ulType = (ulType & ~0x2000000000000) | 0x6800000000000;
    Ps2AddPrim3DEx(ulType, vu1ScessorBuf, count);
}

/* ---- Scissoring with a SCISSOR structure (ps2_Vu1Strip.c) ---------------- */

void InitNodeArraySet(SCISSOR* scissor)
{
    scissor->rotflag = 0;
    scissor->flipflag = 0;
    scissor->in = &scissor->narray[0];
    scissor->out = &scissor->narray[1];
    scissor->narray[0].nodeNum = 0;
    scissor->narray[1].nodeNum = 0;
    scissor->triangle.nodeNum = 3;
}

void ResetNodeArraySet(SCISSOR* scissor)
{
    scissor->flipflag = 0;
    scissor->in = &scissor->narray[0];
    scissor->out = &scissor->narray[1];
    scissor->narray[0].nodeNum = 0;
    scissor->narray[1].nodeNum = 0;
}

/* The last three vertices are kept in a ring, which keeps their order. */
void PushTriangleNodeArray(SCISSOR* scissor, NODE* nod)
{
    scissor->triangle.node[scissor->rotflag] = *nod;
    scissor->rotflag = (scissor->rotflag + 1) % 3;
}

/* Point between a (inside) and b (outside) on the plane comp = sgn * w. */
static void intersect_node(const NODE* a, const NODE* b, int comp, float sgn, NODE* out)
{
    float d0 = fabsf(a->clipV[comp] - sgn * a->clipV[3]);
    float d1 = fabsf(b->clipV[comp] - sgn * b->clipV[3]);
    float t = fabsf(d0 / (d1 + d0));
    int j;

    for (j = 0; j < 4; j++)
    {
        out->vertex[j] = a->vertex[j] + (b->vertex[j] - a->vertex[j]) * t;
        out->color[j] = a->color[j] + (b->color[j] - a->color[j]) * t;
        out->texUV[j] = a->texUV[j] + (b->texUV[j] - a->texUV[j]) * t;
        out->clipV[j] = a->clipV[j] + (b->clipV[j] - a->clipV[j]) * t;
    }
    if (out->vertex[2] < 1.0f)
    {
        out->vertex[2] = 1.0f;
    }
}

/* Clips scissor->triangle against the planes; the polygon ends in
 * scissor->in. */
void ScissorTriangle(SCISSOR* scissor, SCISSOR_PLANE* plane_set)
{
    SCISSOR_NODE* in = &scissor->triangle;
    SCISSOR_NODE* out = &scissor->narray[1];
    unsigned int i;
    unsigned int j;

    for (i = 0; i < plane_set->planeNum; i++)
    {
        unsigned int mask = plane_set->plane[i].clipmask;
        int xyz = plane_set->plane[i].xyzflag & 0xF;
        float sgn = (plane_set->plane[i].xyzflag & 16) ? -1.0f : 1.0f;

        out->nodeNum = 0;
        for (j = 0; j < in->nodeNum; j++)
        {
            NODE* curr = &in->node[j];
            NODE* next = &in->node[(j + 1) < in->nodeNum ? j + 1 : 0];
            unsigned int clip = ((clipw(curr->clipV) << 6) | clipw(next->clipV)) & mask;

            if (out->nodeNum > 10)
            {
                break;
            }

            if (clip == 0)
            {
                out->node[out->nodeNum++] = *curr;
            }
            else if ((clip & 0x3F) && (clip & 0xFC0))
            {
            }
            else if (clip & 0x3F)
            {
                out->node[out->nodeNum++] = *curr;
                intersect_node(curr, next, xyz, sgn, &out->node[out->nodeNum++]);
            }
            else
            {
                intersect_node(next, curr, xyz, sgn, &out->node[out->nodeNum++]);
            }
        }

        in = out;
        out = (out == &scissor->narray[1]) ? &scissor->narray[0] : &scissor->narray[1];
    }

    scissor->in = in;
    scissor->out = out;
    out->nodeNum = 0;
}

static unsigned int project_scissor(SCISSOR* scissor)
{
    SCISSOR_NODE* in = scissor->in;
    VU1_PRIM_BUF* pP = vu1ScessorBuf;
    unsigned int i;

    for (i = 0; i < in->nodeNum && i < 16; i++, pP++)
    {
        NODE* n = &in->node[i];
        float q = 1.0f / n->vertex[2];
        float s = fVu1Projection * q;

        memcpy(&pP->fR, n->color, 16);
        pP->fS = n->texUV[0] * q;
        pP->fT = n->texUV[1] * q;
        pP->fQ = q;
        pP->ulKick = 0;
        pP->fX = n->vertex[0] * s * fVu1AspectW + fVu1OffsetX;
        pP->fY = n->vertex[1] * s * fVu1AspectH + fVu1OffsetY;
        pP->fZ = n->vertex[2];
        pP->fF = n->vertex[3];
    }
    return i;
}

void DrawScissorPolygonOpaque(SCISSOR* scissor, unsigned long ulType)
{
    unsigned int n = project_scissor(scissor);

    ulType = (ulType & ~0x2000000000000) | 0x6800000000000;
    Ps2AddPrim3DEx(ulType, vu1ScessorBuf, n);
}

void DrawScissorPolygonTrans1P(SCISSOR* scissor, unsigned long ulType)
{
    unsigned int n = project_scissor(scissor);

    ulType = (ulType & ~0x2000000000000) | 0x6800000000000;
    Ps2AddPrim3DEx1P(ulType, vu1ScessorBuf, n);
}

/* ---- Vertex colours (pColorCalcFuncTbl) ---------------------------------- */

void vu1GetVertexColor(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    pPrim->fR = pStrip->fIr * 128.0f;
    pPrim->fG = pStrip->fIg * 128.0f;
    pPrim->fB = pStrip->fIb * 128.0f;
    pPrim->fA = pStrip->fA * 256.0f;
}

void vu1GetVertexColorCM(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    pPrim->fR = vu1Diffuse.fR;
    pPrim->fG = vu1Diffuse.fG;
    pPrim->fB = vu1Diffuse.fB;
    pPrim->fA = fVu1AlphaRatio;
}

void vu1GetVertexColorIgnore(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    pPrim->fR = 128.0f;
    pPrim->fG = 128.0f;
    pPrim->fB = 128.0f;
    pPrim->fA = fVu1AlphaRatio;
}

/* clamp(intensity + ambient) * diffuse + clamp(specular) * specular colour */
static void colour(VU1_PRIM_BUF* pPrim, const float* dif, const float* spe)
{
    pPrim->fR = clamp01(dif[0]) * vu1Diffuse.fR;
    pPrim->fG = clamp01(dif[1]) * vu1Diffuse.fG;
    pPrim->fB = clamp01(dif[2]) * vu1Diffuse.fB;
    if (spe != NULL)
    {
        pPrim->fR += clamp01(spe[0]) * vu1Specula.fR;
        pPrim->fG += clamp01(spe[1]) * vu1Specula.fG;
        pPrim->fB += clamp01(spe[2]) * vu1Specula.fB;
    }
    pPrim->fA = fVu1AlphaRatio;
}

void vu1GetVertexColorDif(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    colour(pPrim, &pStrip->fIr, NULL);
}

void vu1GetVertexColorDifAmb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float i[3] = { pStrip->fIr + vu1Ambient.fR, pStrip->fIg + vu1Ambient.fG, pStrip->fIb + vu1Ambient.fB };

    colour(pPrim, i, NULL);
}

/* Specular 1: the light beyond 1. */
void vu1GetVertexColorDifSpe1(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float s[3] = { pStrip->fIr - 1.0f, pStrip->fIg - 1.0f, pStrip->fIb - 1.0f };

    colour(pPrim, &pStrip->fIr, s);
}

/* Specular 2: the light and ambient beyond 1. */
void vu1GetVertexColorDifSpe2(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float s[3] = { pStrip->fIr + vu1Ambient.fR - 1.0f, pStrip->fIg + vu1Ambient.fG - 1.0f,
                   pStrip->fIb + vu1Ambient.fB - 1.0f };

    colour(pPrim, &pStrip->fIr, s);
}

static float pow17(float v)
{
    float r = 1.0f;
    int i;

    for (i = 0; i < 17; i++)
    {
        r *= v;
    }
    return r;
}

/* Specular 3: the light to the power 17. */
void vu1GetVertexColorDifSpe3(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float s[3] = { pow17(pStrip->fIr), pow17(pStrip->fIg), pow17(pStrip->fIb) };

    colour(pPrim, &pStrip->fIr, s);
}

void vu1GetVertexColorDifSpe1Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float i[3] = { pStrip->fIr + vu1Ambient.fR, pStrip->fIg + vu1Ambient.fG, pStrip->fIb + vu1Ambient.fB };
    float s[3] = { pStrip->fIr - 1.0f, pStrip->fIg - 1.0f, pStrip->fIb - 1.0f };

    colour(pPrim, i, s);
}

void vu1GetVertexColorDifSpe2Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float i[3] = { pStrip->fIr + vu1Ambient.fR, pStrip->fIg + vu1Ambient.fG, pStrip->fIb + vu1Ambient.fB };
    float s[3] = { i[0] - 1.0f, i[1] - 1.0f, i[2] - 1.0f };

    colour(pPrim, i, s);
}

void vu1GetVertexColorDifSpe3Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    float i[3] = { pStrip->fIr + vu1Ambient.fR, pStrip->fIg + vu1Ambient.fG, pStrip->fIb + vu1Ambient.fB };
    float s[3] = { pow17(i[0]), pow17(i[1]), pow17(i[2]) };

    colour(pPrim, i, s);
}

/* ---- Strip drawing (ps2_Vu1Strip.c) -------------------------------------- */

extern void (*pColorCalcFuncTbl[11])(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim);

static void strip_vertex(VU1_STRIP_BUF* pS, VU1_PRIM_BUF* pP)
{
    float fIz = pS->fIz;

    pP->fS = pS->fU * fIz;
    pP->fT = pS->fV * fIz;
    pP->fQ = fIz;
    pP->ulKick = 0;
    pP->fX = fVu1OffsetX + pS->fSx;
    pP->fY = fVu1OffsetY + pS->fSy;
    pP->fZ = pS->fVz;
    pP->fF = pS->fFog;
}

/* Two-sided translucent strips: the PS2 computes the colours with VU0
 * (VU0_CALC_COLOR), with the same formulas as pColorCalcFuncTbl. */
void vu1DrawTriangleStripTransDouble(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    VU1_PRIM_BUF* pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*) = pColorCalcFuncTbl[usMode & 0xF];
    unsigned int flg = 0;
    unsigned short i;
    int count;

    clip_reset();

    for (i = 0; i < usStripMax; i++, pS++, pP++)
    {
        clip_shift(&pS->fVx);
        if (i < 2)
        {
            clip_judge(clip_hist[2]);
        }
        else
        {
            clip_judge_triangle();
            clip_plane_test();
        }

        pFunc(pS, pP);
        strip_vertex(pS, pP);

        if (i < 2)
        {
            flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX) << (2 - i);
            continue;
        }

        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);

        if (flg != 0 && (clip_flags & 0x3FFFF))
        {
            pP->ulKick = 32768;

            if (clip_plane != 0)
            {
                _Set_NodeArray(pS, pP);

                count = _Check_ScissorPlane();

                if (count != 0)
                {
                    DrawScissorPolygonOpaque2(count, ulType);
                }
            }
        }

        flg = (flg << 1) & 0x7;
    }

    Ps2AddPrim3DEx(ulType, Ps2_DRAW_TMP, usStripMax);
}

static SCISSOR scissor_i;
static SCISSOR_PLANE planes_i = {
    { { 0x12, 0x820 }, { 0x11, 0x208 }, { 0x1, 0x104 }, { 0x10, 0x82 }, { 0x0, 0x41 } }, 5
};

void vu1DrawTriangleStripTransDoubleI(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    VU1_PRIM_BUF* pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*) = pColorCalcFuncTbl[usMode & 0xF];
    unsigned int clipflag = 0;
    unsigned int flg = 0;
    unsigned short i;
    NODE node;

    InitNodeArraySet(&scissor_i);

    for (i = 0; i < usStripMax; i++, pS++, pP++)
    {
        pFunc(pS, pP);

        pP->fS = pS->fU * pS->fIz;
        pP->fT = pS->fV * pS->fIz;
        pP->fQ = pS->fIz;
        pP->ulKick = 0;
        pP->fX = pS->fSx + fVu1OffsetX;
        pP->fY = pS->fSy + fVu1OffsetY;
        pP->fZ = pS->fVz;
        pP->fF = pS->fFog;

        to_clip(&pS->fVx, node.clipV);
        clip_judge(node.clipV);
        clipflag |= clip_flags & 0x3F;

        memcpy(node.vertex, &pS->fVx, 16);
        memcpy(node.color, &pP->fR, 16);
        memcpy(node.texUV, &pS->fU, 16);
        PushTriangleNodeArray(&scissor_i, &node);

        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);

        if ((flg & 0x7) && (clipflag & 0x3FFFF) && i >= 2)
        {
            pP->ulKick = 32768;

            ScissorTriangle(&scissor_i, &planes_i);

            if (scissor_i.in->nodeNum != 0)
            {
                DrawScissorPolygonOpaque(&scissor_i, ulType);

                ResetNodeArraySet(&scissor_i);
            }
        }

        clipflag <<= 6;
        flg <<= 1;
    }

    Ps2AddPrim3DExI(ulType, Ps2_DRAW_TMP, usStripMax);
}

/* ---- 3D primitives to GS packets (ps2_dummy.c) --------------------------- */

#define PRIM_TME 0x8000000000000ULL  /* texture mapping, in the GIF tag */
#define PRIM_ABE 0x20000000000000ULL /* alpha blending: sorted in the OT */

/* Loads the texture of a textured primitive drawn right away when it is a
 * texture page (TpFlag). Returns 0 when there is no texture to draw with. */
static int prepare_texture(unsigned long* prim, int albinoid)
{
    TIM2_PICTUREHEADER_EX* timp;

    if (!(*prim & PRIM_TME))
    {
        return 1;
    }

    if (Ps2_now_tex == NULL)
    {
        return 0;
    }

    if ((*prim & PRIM_ABE) && Ps2_use_pt_flag != 0)
    {
        *prim &= ~SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(0, 0, 0, 0, 1, 0, 0, 0, 0), 0, 0);
    }

    if (!(*prim & PRIM_ABE))
    {
        timp = (TIM2_PICTUREHEADER_EX*)Ps2_now_tex->texinfo.texsurface.pSurface;

        if (timp->TpFlag != 0)
        {
            Ps2_tex_load_tp_cancel = 1;
            Ps2TexLoad(Ps2_now_tex);
            Ps2_tex_load_tp_cancel = 0;
        }
    }
    else if (albinoid && Ps2_albinoid_flag != 0)
    {
        Ps2_tex_load_tp_cancel = 1;
        Ps2TexLoad(Ps2_now_tex);
        Ps2_tex_load_tp_cancel = 0;
    }

    return 1;
}

/*
 * Builds the GS packet at WORKBASE: DMA tag, the TEST register, then a
 * REGLIST GIF tag with ST, RGBAQ and XYZF2 for each vertex. The VU0 loop of
 * the original, in C: the depth becomes the Z-buffer value, the position goes
 * to 12.4 fixed point, the colour to integers when int_colour is set, and the
 * vertex gets the ADC flag (no drawing kick) when one of the last three
 * vertices is outside the guard band. Returns the average Z-buffer value,
 * which is also stored after the vertices.
 */
static float pack_prim3d(unsigned long prim, const void* dp, unsigned int num, int test2, int int_colour)
{
    unsigned long* p = (unsigned long*)WORKBASE;
    const float* src;
    unsigned int* dst;
    unsigned int i;
    unsigned int clip;
    float zsum;

    D2_SyncTag();

    *p++ = ((num * 3) + 3) | 0x70000000;
    *p++ = 0;

    *p++ = SCE_GIF_SET_TAG(1, 0, SCE_GIF_PACKED, 0, 0, 1);
    *p++ = SCE_GIF_PACKED_AD;

    if (test2)
    {
        *p++ = SCE_GS_SET_TEST_2(0, 0, 0, 0, 0, 0, 1, 2);
        *p++ = SCE_GS_TEST_2;
    }
    else
    {
        *p++ = Ps2_gs_save.TEST = SCE_GS_SET_TEST_1(1, SCE_GS_ALPHA_GREATER, 0, SCE_GS_AFAIL_KEEP, 0, 0, 1, SCE_GS_DEPTH_GEQUAL);
        *p++ = SCE_GS_TEST_1;
    }

    *p++ = (SCE_GIF_SET_TAG(0, 1, SCE_GIF_REGLIST, 0, 0, 3) | prim) | num;
    *p++ = GIF_REGLIST(SCE_GS_ST, SCE_GS_RGBAQ, SCE_GS_XYZF2);

    src = (const float*)dp;
    dst = (unsigned int*)p;
    clip = 0;
    zsum = 0;

    for (i = 0; i < num; i++, src += 12, dst += 12)
    {
        unsigned int flags;
        unsigned int cflags = 0;
        float cx = src[8] - 2048.0f;
        float cy = src[9] - 2048.0f;
        float cz = (src[10] - 2048.0625f) + (src[10] * 0.062501907f);
        float sz;
        int j;

        memcpy(&flags, &src[3], 4);
        flags &= 0xFFFF;

        /* vclipw.xyz against 2047 */
        if (cx > 2047.0f) cflags |= 0x1;
        if (cx < -2047.0f) cflags |= 0x2;
        if (cy > 2047.0f) cflags |= 0x4;
        if (cy < -2047.0f) cflags |= 0x8;
        if (cz > 2047.0f) cflags |= 0x10;
        if (cz < -2047.0f) cflags |= 0x20;
        clip = ((clip << 6) | cflags) & 0xFFFFFF;

        sz = -Ps2_zbuff_a + (src[2] * -Ps2_zbuff_b);

        if (sz < 0)
        {
            sz = 0;
        }

        if (sz > 65534.0f)
        {
            sz = 65534.0f;
        }

        zsum += sz;

        if ((clip & 0x3FFFF))
        {
            flags |= 0x8000;
        }

        memcpy(dst, src, 16);
        if (int_colour)
        {
            for (j = 4; j < 8; j++)
            {
                dst[j] = ftoi0(src[j]);
            }
        }
        else
        {
            memcpy(&dst[4], &src[4], 16);
        }
        dst[8] = ftoi4(src[8]);
        dst[9] = ftoi4(src[9]);
        dst[10] = ftoi4(sz);
        dst[11] = (int)(short)((ftoi4(src[11]) & 0xFFFF) | flags);
        /* 60 fps: the models' vertices, to find them in the next frame */
        if (int_colour)
        {
            pc_interp_vertex(dst);
        }
    }

    ((float*)dst)[3] = zsum * (1.0f / (float)num);
    return ((float*)dst)[3];
}

static void send_now(void)
{
    SyncPath();
    loadImage((void*)0xF0000000);
}

void Ps2AddPrim3D(unsigned long prim, void* dp, unsigned int num)
{
    float z;

    if (!prepare_texture(&prim, 0))
    {
        return;
    }

    z = pack_prim3d(prim, dp, num, 0, 0);

    if ((prim & PRIM_ABE))
    {
        Ps2AddOT((void*)WORKBASE, num, z, prim);
    }
    else
    {
        send_now();
    }
}

void Ps2AddPrim3DEx(unsigned long prim, void* dp, unsigned int num)
{
    float z;

    if (!prepare_texture(&prim, 0))
    {
        return;
    }

    z = pack_prim3d(prim, dp, num, 0, 1);

    if ((prim & PRIM_ABE))
    {
        Ps2AddOT((void*)WORKBASE, num, z, prim);
    }
    else
    {
        send_now();
    }
}

void Ps2AddPrim3DEx1P(unsigned long prim, void* dp, unsigned int num)
{
    if (!prepare_texture(&prim, 1))
    {
        return;
    }

    pack_prim3d(prim, dp, num, 0, 1);

    if (Ps2_albinoid_flag == 0 && (prim & PRIM_ABE))
    {
        Ps2AddOT((void*)WORKBASE, num, Ps2AddPrimPrio, prim);
    }
    else
    {
        send_now();
    }
}

/* Shadows: second drawing context, no texture, drawn right away. */
void Ps2AddPrim3DMod(unsigned long prim, void* dp, unsigned int num)
{
    prim |= SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(0, 0, 0, 0, 0, 0, 0, 1, 0), 0, 0);

    pack_prim3d(prim, dp, num, 1, 1);
    send_now();
}

/* Like Ps2AddPrim3DEx, but a translucent strip is sorted triangle by
 * triangle, at a depth chosen by ViewType. */
void Ps2AddPrim3DExI(unsigned long prim, void* dp, unsigned int num)
{
    float z;
    int j;

    if (!prepare_texture(&prim, 0))
    {
        return;
    }

    z = pack_prim3d(prim, dp, num, 0, 1);

    if (!(prim & PRIM_ABE))
    {
        send_now();
        return;
    }

    if (ViewType < 3 && num > 3 && (prim & 0x3800000000000) == 0x2000000000000 && ((num * 3) - 6) < 167)
    {
        unsigned long* pp = (unsigned long*)WORKBASE;
        port_u128* p128 = (port_u128*)(WORKBASE + 64);

        *pp++ = 0x70000000 | 12;
        *pp++ = 0;

        *pp++ = SCE_GIF_SET_TAG(1, 0, SCE_GIF_PACKED, 0, 0, 1);
        *pp++ = SCE_GIF_PACKED_AD;

        *pp++ = Ps2_gs_save.TEST = SCE_GS_SET_TEST_1(1, SCE_GS_ALPHA_GREATER, 0, SCE_GS_AFAIL_KEEP, 0, 0, 1, SCE_GS_DEPTH_GEQUAL);
        *pp++ = SCE_GS_TEST_1;

        prim &= ~SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(7, 0, 0, 0, 0, 0, 0, 0, 0), 0, 0);

        *pp++ = SCE_GIF_SET_TAG(3, 1, SCE_GIF_REGLIST, SCE_GS_SET_PRIM(SCE_GS_PRIM_TRI, 0, 0, 0, 0, 0, 0, 0, 0), 0, 3) | prim;
        *pp++ = GIF_REGLIST(SCE_GS_ST, SCE_GS_RGBAQ, SCE_GS_XYZF2);

        memmove(&p128[9], &p128[0], num * 3 * sizeof(port_u128));

        for (j = 0; j < (int)num - 2; j++)
        {
            float fz[3];
            int k;

            memcpy(&p128[0], &p128[3 * (j + 3)], 9 * sizeof(port_u128));

            for (k = 0; k < 3; k++)
            {
                fz[k] = (float)(int)p128[3 * k + 2].w[2] * (1.0f / 16.0f);
            }

            switch (ViewType)
            {
            case 0:
                z = fz[0] < fz[1] ? fz[0] : fz[1];
                if (fz[2] < z)
                {
                    z = fz[2];
                }
                Ps2AddOT((void*)WORKBASE, 3, z, prim);
                break;
            case 1:
                /* The original sums x, y and z of the first vertex. */
                z = (float)(int)p128[2].w[0] * (1.0f / 16.0f) + (float)(int)p128[2].w[1] * (1.0f / 16.0f) + fz[0];
                Ps2AddOT((void*)WORKBASE, 3, 0.33333334f * z, prim);
                break;
            case 2:
                z = fz[0] > fz[1] ? fz[0] : fz[1];
                if (fz[2] > z)
                {
                    z = fz[2];
                }
                Ps2AddOT((void*)WORKBASE, 3, z, prim);
                break;
            }
        }

        return;
    }

    Ps2AddOT((void*)WORKBASE, num, z, prim);
}
