/*
 * Sanity tests for the PC versions of the Ninja math and matrix functions.
 * Built by CMake as `test_math`; run it after changing port/src/ninja.
 */
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"

/* The view module is not part of the test; provide what the matrix code uses. */
NJS_MATRIX NaViwViewMatrix;
NJS_MATRIX NaViewScreenMatrix;
float fNaViwOffsetX;
float fNaViwOffsetY;
NJS_SCREEN _nj_screen_;
float njCalcFogPower(float fVSZ) { return 0.0f; }

static int failures;

static void check(const char *what, float got, float want)
{
    if (fabsf(got - want) > 1e-3f)
    {
        printf("FAIL %s: got %f, want %f\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    NJS_MATRIX stack[4];
    NJS_MATRIX m;
    NJS_POINT3 p, q;
    NJS_VECTOR axis;
    float s, c;

    _Make_SinTable();

    check("sin 0", njSin(0), 0.0f);
    check("sin 90", njSin(0x4000), 1.0f);
    check("sin 180", njSin(0x8000), 0.0f);
    check("sin 270", njSin(0xC000), -1.0f);
    check("sin -90", njSin(-0x4000), -1.0f);
    check("cos 0", njCos(0), 1.0f);
    check("cos 90", njCos(0x4000), 0.0f);
    check("cos 180", njCos(0x8000), -1.0f);
    check("sin 30", njSin(0x10000 / 12), 0.5f);
    check("cos 60", njCos(0x10000 / 6), 0.5f);
    njSinCos(0x2000, &s, &c);
    check("sincos 45 sin", s, 0.70710678f);
    check("sincos 45 cos", c, 0.70710678f);

    njInitMatrix(stack, 4, 0);
    njUnitMatrix(NULL);

    /* Rotating (1, 0, 0) by 90 degrees around Y gives (0, 0, -1) in Ninja's
     * right-handed system. */
    njRotateY(NULL, 0x4000);
    p.x = 1; p.y = 0; p.z = 0;
    njCalcPoint(NULL, &p, &q);
    check("rotY x", q.x, 0.0f);
    check("rotY z", q.z, -1.0f);

    /* Translation is applied in the rotated frame. */
    njUnitMatrix(NULL);
    njTranslate(NULL, 10, 20, 30);
    njRotateZ(NULL, 0x4000);
    p.x = 1; p.y = 0; p.z = 0;
    njCalcPoint(NULL, &p, &q);
    check("trans+rotZ x", q.x, 10.0f);
    check("trans+rotZ y", q.y, 21.0f);
    check("trans+rotZ z", q.z, 30.0f);

    /* The inverse brings the point back. */
    njGetMatrix(&m);
    njInvertMatrix(&m);
    njCalcPoint(&m, &q, &p);
    check("invert x", p.x, 1.0f);
    check("invert y", p.y, 0.0f);
    check("invert z", p.z, 0.0f);

    /* njRotate around Y must match njRotateY. */
    njUnitMatrix(NULL);
    axis.x = 0; axis.y = 2; axis.z = 0;
    njRotate(NULL, &axis, 0x4000);
    p.x = 1; p.y = 0; p.z = 0;
    njCalcPoint(NULL, &p, &q);
    check("rotate axis x", q.x, 0.0f);
    check("rotate axis z", q.z, -1.0f);

    /* Push/pop keep the parent matrix. */
    njUnitMatrix(NULL);
    njPushMatrix(NULL);
    njTranslate(NULL, 5, 0, 0);
    njPopMatrix(1);
    njGetTranslation(NULL, &q);
    check("push/pop", q.x, 0.0f);

    p.x = 3; p.y = 4; p.z = 0;
    check("scalor", njScalor(&p), 5.0f);
    check("unit len", njUnitVector(&p), 5.0f);
    check("unit x", p.x, 0.6f);
    q.x = 0; q.y = 0; q.z = 0;
    check("unit zero", njUnitVector(&q), 0.0f);
    check("unit zero x", q.x, 0.0f);

    if (failures == 0)
    {
        printf("test_math: all checks passed\n");
    }

    return failures != 0;
}
