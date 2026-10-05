/*
 * Checks the PC versions of Ps2AddPrim3D and Ps2AddOT: a semi-transparent
 * sprite goes into the ordering table and is drawn by the game's own
 * Ps2DrawOTag, through the DMA chain it builds, into the software GS.
 */
#include "ps2_dummy.h"
#include "ps2_NaDraw2D.h"

#include "../src/gs/gs.h"
#include "../src/gs/gs_mem.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static unsigned int fb(int x, int y)
{
    return gs_read_pixel(GS_PSMCT32, 0, 10, x, y) & 0xFFFFFF;
}

/* One vertex as Ps2AddPrim3D reads it: ST (with Q, used for depth), RGBAQ
 * and XYZW, as floats except the colour. */
static void vertex(float* v, float x, float y, int r, int g, int b, int a)
{
    int* c = (int*)&v[4];

    memset(v, 0, 48);
    v[2] = 1.0f;
    c[0] = r;
    c[1] = g;
    c[2] = b;
    c[3] = a;
    v[8] = x;
    v[9] = y;
    v[10] = 1000.0f; /* inside the depth guard band */
}

int main(void)
{
    static float verts[2][12] __attribute__((aligned(16)));

    gs_reset();
    gs_write_reg(0x4c, 0 | (10ull << 16));                              /* FRAME_1 */
    gs_write_reg(0x18, (2048ull << 4) | ((2048ull << 4) << 32));        /* XYOFFSET_1 */
    gs_write_reg(0x40, 639ull << 16 | (479ull << 48));                  /* SCISSOR_1 */

    Ps2_zbuff_a = -100.0f;
    Ps2_zbuff_b = -1.0f;
    Ps2_gs_save.ALPHA = 0; /* (Cs - Cs) * As + Cs: the source colour */

    Ps2ClearOT();
    vertex(verts[0], 2048 + 100, 2048 + 100, 200, 100, 50, 128);
    vertex(verts[1], 2048 + 110, 2048 + 110, 200, 100, 50, 128);
    /* Sprite with alpha blending: blended primitives are sorted. */
    Ps2AddPrim3D((unsigned long)(6 | (1 << 6)) << 47, verts, 2);
    CHECK(fb(105, 105) == 0); /* not drawn before the table is */

    Ps2DrawOTag();
    CHECK(fb(100, 100) == 0x3264C8);
    CHECK(fb(109, 109) == 0x3264C8);
    CHECK(fb(110, 110) == 0);
    CHECK(fb(99, 105) == 0);

    if (failures == 0)
        printf("test_ot: all checks passed\n");
    return failures != 0;
}
