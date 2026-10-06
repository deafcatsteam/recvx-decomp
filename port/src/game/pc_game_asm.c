/*
 * PC versions of game functions written in EE/VU0 assembly.
 *
 * The originals stay in the game sources under #ifndef PLATFORM_PC. Functions
 * that only compute data are translated to C here; the 3D drawing ones are in
 * pc_render3d.c. The model deformations that still need translating are
 * placeholders; each says what it is waiting for.
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

/* ---- Facial animation ----------------------------------------------------- */

/*
 * The faces are type 51 vertex chunks: 8 floats per vertex (position, then
 * normal), starting 16 words into the vertex list. The VU0 code assumes this
 * layout in places, and so do these versions.
 *
 * p * m for a row-vector point (x, y, z, 1), as the VU0 multiply-add chains
 * compute it.
 */
static void face_point(const float* m, float x, float y, float z, float* out)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        out[i] = m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i];
    }
}

/* Moves the face vertices by their muscles, from the rest pose in src. */
void _fmCnkCalcMuscle(MASK_WORK* fm)
{
    int m, n, i;
    float* dvp;
    float* svp;
    LIST_WORK* con;
    VLIST_WORK* list;
    float* param;
    NJS_POINT3* mvec[32];
    float mrate[32];
    float sum, rate, para, rsum;
    TANG_WORK* jaw;

    dvp = (float*)&fm->dst->vlist[fm->vtop];
    svp = (float*)&fm->src->vlist[fm->vtop];

    /* The jaw vertices start again from the rest pose. */
    jaw = fm->jaw;
    for (n = fm->jnum; n > 0; n--, jaw++)
    {
        memcpy(&dvp[jaw->id * 8], &svp[jaw->id * 8], 16);
    }

    con = fm->list;
    list = fm->vlist;
    param = (float*)&fm->param;

    for (n = fm->lnum; n > 0; list++, n--)
    {
        sum = 0;
        m = 0;

        for (i = list->mnum; i > 0; i--, con++)
        {
            para = param[con->id];

            if (para != 0)
            {
                mvec[m] = &con->vec;
                rate = con->scal * ((para < 0) ? -para : para);
                sum += rate;
                mrate[m] = para * rate;
                m++;
            }
        }

        rsum = 1.0f / sum;

        dvp = (float*)&fm->dst->vlist[(list->id * 8) + 16];
        memcpy(dvp, &svp[list->id * 8], 16);

        /* The weighted sum of the muscle vectors, last one first. */
        while (m-- > 0)
        {
            float r = mrate[m] * rsum;

            dvp[0] += mvec[m]->x * r;
            dvp[1] += mvec[m]->y * r;
            dvp[2] += mvec[m]->z * r;
        }
    }
}

/* Opens the jaw: its vertices turn about the hinge set up by _fmCnkSetJaw. */
void _fmCnkCalcJaw(MASK_WORK* fm)
{
    int i, r, k;
    TANG_WORK* jaw;
    NJS_MATRIX mat, mat2;
    float c[16];
    float* dvp;
    float* dvp1;
    float* j1;
    float* m2;
    float out[3];
    float jawang, jawtrans;
    unsigned int vofs;

    if (fm->jnum == 0)
    {
        return;
    }

    jawang = -fm->param.jawang;
    jawtrans = fm->param.jawtrans;

    njSetMatrix(&mat, &fm->jmat2);
    njRotateX(&mat, (int)(182.04445f * jawang) & 0xFFFF);
    njTranslate(&mat, jawtrans, 0, 0);
    njMultiMatrix(&mat, &fm->jmat1);

    jaw = fm->jaw;
    vofs = fm->vofs;
    dvp = (float*)&fm->dst->vlist[fm->vtop];
    j1 = (float*)&fm->jmat1;

    for (i = fm->jnum; i > 0; i--, jaw++)
    {
        dvp1 = &dvp[jaw->id * vofs];

        if (jaw->rate == 1.0f)
        {
            face_point((float*)&mat, dvp1[0], dvp1[1], dvp1[2], out);
        }
        else
        {
            /* A vertex only partly on the jaw turns by part of the angle:
             * jmat1 * (jmat2 turned, then moved along x), as VU0 does it. */
            njCopyMatrix(&mat2, &fm->jmat2);
            njRotateX(&mat2, (int)(182.04445f * (jawang * jaw->rate)) & 0xFFFF);
            m2 = (float*)&mat2;

            for (r = 0; r < 3; r++)
            {
                m2[12 + r] = m2[r] * jawtrans + m2[12 + r];
            }
            for (r = 0; r < 4; r++)
            {
                for (k = 0; k < 3; k++)
                {
                    c[r * 4 + k] = j1[r * 4] * m2[k] + j1[r * 4 + 1] * m2[4 + k] + j1[r * 4 + 2] * m2[8 + k];
                    if (r == 3)
                    {
                        c[r * 4 + k] += m2[12 + k];
                    }
                }
            }
            face_point(c, dvp1[0], dvp1[1], dvp1[2], out);
        }

        dvp1[0] = out[0];
        dvp1[1] = out[1];
        dvp1[2] = out[2];
    }

    if (fm->toothsrc != NULL)
    {
        fm->toothsrc->ang[0] = (int)(182.04445f * jawang) & 0xFFFF;
    }

    if (fm->tangorg != NULL)
    {
        fm->tangorg->ang[0] = (int)(182.04445f * jawang) & 0xFFFF;
    }
}

/* Turns each eyeball towards the point the face parameters look at. */
void _fmCnkCalcEye(MASK_WORK* fm)
{
    float dx, dy, dz;
    NJS_CNK_OBJECT* obj;
    float pos[3], r3[3];
    int i;

    for (i = 0; i < 9; i++)
    {
        obj = fm->eyesrc[i];

        if (obj != NULL)
        {
            const float* m = (const float*)&fm->eyemat[i];

            njPushMatrix(NULL);

            face_point(m, obj->pos[0], obj->pos[1], obj->pos[2], pos);
            face_point(m, 0, 0, -100.0f, r3);

            dx = (pos[0] + fm->param.eye.x) - pos[0];
            dy = (pos[1] + fm->param.eye.y) - pos[1];
            dz = (pos[2] + fm->param.eye.z) - pos[2];

            if (r3[2] < 0)
            {
                obj->ang[0] = (int)(10430.381f * atanf(dy * njInvertSqrt((dx * dx) + (dz * dz))));
            }
            else
            {
                obj->ang[0] = -(int)(10430.381f * atanf(dy * njInvertSqrt((dx * dx) + (dz * dz))));
            }

            if (dz < 0)
            {
                obj->ang[1] = (int)(10430.381f * atanf(dx / dz));
            }
            else
            {
                obj->ang[1] = (int)(10430.381f * atanf(dx / dz)) + (32767 + 1);
            }

            obj->ang[2] = 0;

            njPopMatrix(1);
        }
    }
}

/* Bends the tongue: each vertex turns by its share of the angles. */
void _fmCnkCalcTang(MASK_WORK* fm)
{
    int i;
    TANG_WORK* tang;
    NJS_MATRIX mat;
    float* m;
    float* svp;
    float* dvp;
    float* s;
    float* d;
    unsigned int vofs;
    float tangx, tangy, tangz;

    if (fm->tnum == 0)
    {
        return;
    }

    svp = (float*)&fm->tangsrc->vlist[fm->vtop];
    dvp = (float*)&fm->tangdst->vlist[fm->vtop];

    tangx = fm->param.tangx;
    tangy = fm->param.tangy;
    tangz = fm->param.tangz;

    vofs = fm->vofs;
    tang = fm->tang;
    m = (float*)&mat;

    for (i = fm->tnum; i > 0; i--, tang++)
    {
        njUnitMatrix(&mat);
        njRotateX(&mat, (int)(182.04445f * (tangx * tang->rate)) & 0xFFFF);
        njRotateY(&mat, (int)(182.04445f * (tangy * tang->rate)) & 0xFFFF);

        /* tangz stretches it along z. */
        m[8] *= tangz;
        m[9] *= tangz;
        m[10] *= tangz;

        s = &svp[tang->id * vofs];
        d = &dvp[tang->id * vofs];
        face_point(m, s[0], s[1], s[2], d);
    }
}


/* ---- Rendering ----------------------------------------------------------- */

/* The 3D drawing helpers are in pc_render3d.c. */

/* GIF transfers complete immediately on PC: nothing to wait for. */
void SyncPath() {}
void D2_SyncTag() {}

/* Starts a GIF DMA chain (channel 2, source chain mode) at tags. */
void loadImage(void* tags)
{
    gs_dma_gif_chain((unsigned int)tags);
}

/* VU microprograms, referenced only by address when uploading them. */
int ps2_vu0sub0;
int ps2_vu1sub0;
int ps2_vu1sub1;
