/*
 * C versions of the libvu0 vector helpers the game uses (VU0 macro mode on
 * the PS2). Only x, y, z take part in dot/cross products, like on the VU.
 */
#include <libvu0.h>
#include <math.h>

void sceVu0AddVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    for (int i = 0; i < 4; i++)
        v0[i] = v1[i] + v2[i];
}

void sceVu0SubVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    for (int i = 0; i < 4; i++)
        v0[i] = v1[i] - v2[i];
}

void sceVu0ScaleVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s)
{
    for (int i = 0; i < 4; i++)
        v0[i] = v1[i] * s;
}

float sceVu0InnerProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    return v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
}

void sceVu0OuterProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    float x = v1[1] * v2[2] - v2[1] * v1[2];
    float y = v1[2] * v2[0] - v2[2] * v1[0];
    float z = v1[0] * v2[1] - v2[0] * v1[1];

    v0[0] = x;
    v0[1] = y;
    v0[2] = z;
    v0[3] = 0.0f;
}

void sceVu0Normalize(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    float len = sqrtf(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);
    float inv = len != 0.0f ? 1.0f / len : 0.0f;

    v0[0] = v1[0] * inv;
    v0[1] = v1[1] * inv;
    v0[2] = v1[2] * inv;
    v0[3] = v1[3];
}

/* VU ftoi/itof: conversions to and from fixed point with 0, 4 or 12
 * fractional bits; float to int truncates toward zero. */
void sceVu0FTOI0Vector(sceVu0IVECTOR v0, sceVu0FVECTOR v1)
{
    for (int i = 0; i < 4; i++)
        v0[i] = (int)v1[i];
}

void sceVu0FTOI4Vector(sceVu0IVECTOR v0, sceVu0FVECTOR v1)
{
    for (int i = 0; i < 4; i++)
        v0[i] = (int)(v1[i] * 16.0f);
}

void sceVu0ITOF12Vector(sceVu0FVECTOR v0, sceVu0IVECTOR v1)
{
    for (int i = 0; i < 4; i++)
        v0[i] = (float)v1[i] / 4096.0f;
}

void sceVu0UnitMatrix(sceVu0FMATRIX m0)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m0[i][j] = i == j ? 1.0f : 0.0f;
}
