/*
 * PC replacement for src/ps2/veronica/prog/ps2_NaMath.c.
 *
 * The PS2 versions are VU0/FPU assembly. These are straight C translations
 * that keep the PS2 results, including the quirks the game was tuned with:
 *  - sine/cosine come from a quarter-wave table and mirror it with
 *    `16383 - i` (one step off from the exact symmetry);
 *  - VU0 square roots work on |x| and a division by zero gives the largest
 *    float instead of infinity.
 */
#include "ps2_NaMath.h"
#include <float.h>

static float SinTable[16384];

void _Make_SinTable()
{
    int i;

    for (i = 0; i < 16384; i++)
    {
        SinTable[i] = sinf(0.0000958738f * i);
    }
}

/* Angle units: 0x10000 per turn. Bit 0x4000 = second/fourth quadrant,
 * bit 0x8000 = negative half-turn. */
Float njSin(Angle n)
{
    int i = n & 0x3FFF;
    float v = (n & 0x4000) ? SinTable[16383 - i] : SinTable[i];

    return (n & 0x8000) ? -v : v;
}

Float njCos(Angle n)
{
    int i = n & 0x3FFF;

    if (n & 0x4000)
    {
        return (n & 0x8000) ? SinTable[i] : -SinTable[i];
    }

    return (n & 0x8000) ? -SinTable[16383 - i] : SinTable[16383 - i];
}

void njSinCos(int lAngle, float* sin, float* cos)
{
    *sin = njSin(lAngle);
    *cos = njCos(lAngle);
}

Float njFraction(Float n)
{
    return n - floorf(n);
}

Float njSqrt(Float n)
{
    return sqrtf(fabsf(n));
}

Float njInvertSqrt(Float n)
{
    if (n == 0.0f)
    {
        return FLT_MAX;
    }

    return 1.0f / sqrtf(fabsf(n));
}

void njLinear(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame)
{
    odata[0] = idata[0] + (idata[3] - idata[0]) * frame;
    odata[1] = idata[1] + (idata[4] - idata[1]) * frame;
    odata[2] = idata[2] + (idata[5] - idata[2]) * frame;
}

/* Catmull-Rom spline through idata[3..5] -> idata[6..8], using the
 * neighbouring keys idata[0..2] and idata[9..11]. */
void njOverhauserSpline(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame)
{
    float t = frame;
    float t2 = t * t;
    float t3 = t2 * t;
    float c0 = -t3 + 2.0f * t2 - t;
    float c1 = 3.0f * t3 - 5.0f * t2 + 2.0f;
    float c2 = -3.0f * t3 + 4.0f * t2 + t;
    float c3 = t3 - t2;
    int i;

    for (i = 0; i < 3; i++)
    {
        odata[i] = 0.5f * (c0 * idata[i] + c1 * idata[3 + i] +
                           c2 * idata[6 + i] + c3 * idata[9 + i]);
    }
}

void njBezierSpline(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame)
{
    unsigned int ulCnt;
    unsigned int ulMax;
    float fFactMax;
    float fResult;

    odata[0] = 0;
    odata[1] = 0;
    odata[2] = 0;

    ulMax = *attr->iparam - 1;

    fResult = njFactorial(ulMax);

    for (ulCnt = 0; ulCnt <= ulMax; ulCnt++)
    {
        fFactMax = (powf(frame, ulCnt) * (fResult / (njFactorial(ulCnt) * njFactorial(ulMax - ulCnt)))) * powf(1.0f - frame, ulMax - ulCnt);

        odata[0] += fFactMax * *idata++;
        odata[1] += fFactMax * *idata++;
        odata[2] += fFactMax * *idata++;
    }
}

unsigned int njFactorial(unsigned int ulN)
{
    unsigned int ulResult;

    ulResult = 1;

    for ( ; ulN != 0; ulN--)
    {
        ulResult *= ulN;
    }

    return ulResult;
}
