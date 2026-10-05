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

/* ---- Facial animation (needs translating from VU0 code) ----------------- */

/* TODO: facial animation; the faces keep their rest pose for now. */
void _fmCnkCalcMuscle(MASK_WORK* fm) {}
void _fmCnkCalcJaw(MASK_WORK* fm) {}
void _fmCnkCalcEye(MASK_WORK* fm) {}
void _fmCnkCalcTang(MASK_WORK* fm) {}


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
