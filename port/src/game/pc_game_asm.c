/*
 * PC versions of game functions written in EE/VU0 assembly.
 *
 * The originals stay in the game sources under #ifndef PLATFORM_PC. Functions
 * that only compute data are translated to C here. The ones that feed the
 * PS2 renderer (VU1 packets, GS uploads) or depend on VU0 microprograms are
 * placeholders until the PC renderer exists; each says what it is waiting for.
 */
#include "expand.h"
#include "face.h"
#include "face_bh.h"
#include "njplus.h"
#include "ps2_NinjaCnk.h"
#include "ps2_Vu1Scissor2.h"
#include "ps2_Vu1Strip.h"
#include "ps2_dummy.h"
#include "ps2_loadtim2.h"

#include "../gs/gs.h"

/* ---- Data processing, translated ---------------------------------------- */

/*
 * LZ decompressor for the game's packed files. The descriptor bits are read
 * least significant first; the first descriptor byte has an extra bit, which
 * the data depends on. Based on the C version by Clownacy in expand.c.
 */
static int expand_bit(const unsigned char** src, unsigned int* field, int* remaining)
{
    int bit;

    if (--*remaining == 0)
    {
        *field = *(*src)++;
        *remaining = 8;
    }

    bit = *field & 1;
    *field >>= 1;
    return bit;
}

int Expand(register char* s, register unsigned char* d)
{
    const unsigned char* src = (const unsigned char*)s;
    unsigned char* dst = d;
    unsigned int field = *src++;
    int remaining = 9;

    for (;;)
    {
        unsigned int length;
        int offset;

        if (expand_bit(&src, &field, &remaining))
        {
            *dst++ = *src++;
            continue;
        }

        if (!expand_bit(&src, &field, &remaining))
        {
            /* Short match: 2 to 5 bytes within the last 256. */
            length = expand_bit(&src, &field, &remaining) * 2;
            length += expand_bit(&src, &field, &remaining);
            length += 2;
            offset = -0x100 + *src++;
        }
        else
        {
            /* Long match: 13-bit offset, 3-bit length or an extra byte. */
            unsigned int whole = src[0] | (src[1] << 8);

            src += 2;
            if (whole == 0)
            {
                break;
            }
            offset = -0x2000 + (int)(whole >> 3);
            length = whole & 7;
            if (length != 0)
            {
                length += 2;
            }
            else
            {
                length = *src++ + 1;
            }
        }

        while (length-- != 0)
        {
            *dst = dst[offset];
            dst++;
        }
    }

    return dst - d;
}

/* Bits needed to hold value - 1, i.e. log2 of a power-of-two texture size. */
unsigned int Ps2BitCount(register unsigned int value)
{
    int x = (int)(value - 1);

    if (x < 0)
    {
        x = ~x;
    }
    return x != 0 ? 32 - __builtin_clz((unsigned int)x) : 0;
}

/* ---- Model deformation (needs translating from VU0 code) ---------------- */

/* TODO: facial animation; the faces keep their rest pose for now. */
void _fmCnkCalcMuscle(MASK_WORK* fm) {}
void _fmCnkCalcJaw(MASK_WORK* fm) {}
void _fmCnkCalcEye(MASK_WORK* fm) {}
void _fmCnkCalcTang(MASK_WORK* fm) {}

/* TODO: vertex morphing between two models. */
void npTransform(NJS_CNK_OBJECT* srcobj, NJS_CNK_OBJECT* dstobj, register float no, int ono) {}

/* TODO: skinning; skinned models keep their bind pose for now. */
void npCalcSkin(void* pwp, int obj_n, int* sknp) {}
void npCalcSkinFM(void* pwp, int obj_n, int* sknp) {}

/* ---- Rendering (replaced by the PC renderer) ---------------------------- */

/* Chunk model drawing: skip the chunk without drawing it. */
CHUNK_HEAD* njCnkCvVnPs2(CHUNK_HEAD* pCnk)
{
    return (CHUNK_HEAD*)&((unsigned int*)(pCnk + 1))[pCnk->usSize];
}

CHUNK_HEAD* njCnkCsUvh(CHUNK_HEAD* pCnk)
{
    return (CHUNK_HEAD*)&((unsigned short*)(pCnk + 1))[pCnk->usSize];
}

CHUNK_HEAD* njCnkCsUvn(CHUNK_HEAD* pCnk)
{
    return (CHUNK_HEAD*)&((unsigned short*)(pCnk + 1))[pCnk->usSize];
}

void Ps2AddPrim3DMod(unsigned long prim, void* dp, unsigned int num) {}

/* GIF transfers complete immediately on PC: nothing to wait for. */
void SyncPath() {}
void D2_SyncTag() {}

/* Starts a GIF DMA chain (channel 2, source chain mode) at tags. */
void loadImage(void* tags)
{
    gs_dma_gif_chain((unsigned int)tags);
}

int _Clip_Screen(float* clip)
{
    return 0;
}

void vu1DrawTriangleStripTransDouble(unsigned long ulType, VU1_STRIP_BUF* pS,
                                     unsigned short usStripMax, unsigned short usMode) {}
void vu1DrawTriangleStripTransDoubleI(unsigned long ulType, VU1_STRIP_BUF* pS,
                                      unsigned short usStripMax, unsigned short usMode) {}

/* VU microprograms, referenced only by address when uploading them. */
int ps2_vu0sub0;
int ps2_vu1sub0;
int ps2_vu1sub1;

void Ps2AddPrim3DEx(unsigned long prim, void* dp, unsigned int num) {}
void Ps2AddPrim3DEx1P(unsigned long prim, void* dp, unsigned int num) {}

/* Polygon clipping and scissoring done with VU0 on the PS2. */
void InitNodeArraySet(SCISSOR* scissor) {}
void InitNodeArraySet2() {}
void ResetNodeArraySet(SCISSOR* scissor) {}
void PushTriangleNodeArray(SCISSOR* scissor, NODE* nod) {}
void ScissorTriangle(SCISSOR* scissor, SCISSOR_PLANE* plane_set) {}
void DrawScissorPolygonOpaque2(int count, unsigned long ulType) {}
void DrawScissorPolygonTrans1P(SCISSOR* scissor, unsigned long ulType) {}
void _Check_ClipViewAll(NJS_POINT4* vec) {}
int _ClipInter(int mask1, int mask2, int xyzflg, float sin, int work0, int work1, int count) { return 0; }
int _Clip_ViewVolume(float* clip, float local_clip[4], float* vertex) { return 0; }
unsigned int _Clip_ViewVolume2(NJS_POINT4* vec) { return 0; }
unsigned int _Get_ClipViewVolume2() { return 0; }
int _Get_ClipVolumePlane() { return 0; }
void _Set_NodeArray(VU1_STRIP_BUF* pS, VU1_PRIM_BUF* pP) {}

/* Vertex lighting done with VU0 on the PS2. */
void vu1GetVertexColor(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorCM(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorIgnore(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDif(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifAmb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe1(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe2(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe3(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe1Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe2Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
void vu1GetVertexColorDifSpe3Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) {}
