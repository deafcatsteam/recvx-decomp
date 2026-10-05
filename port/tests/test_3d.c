/*
 * Draws small chunk models through the game's own Ninja code (view setup,
 * njCnkDrawModelLocal, the chunk functions) and the PC versions of the VU0
 * code (vertex transform and lighting, strip colours, clipping and
 * scissoring, 3D primitive packing) into the software GS, and checks pixels.
 */
#include "ps2_NaDraw2D.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaView.h"
#include "ps2_NinjaCnk.h"
#include "ps2_dummy.h"

#include "../src/gs/gs.h"
#include "../src/gs/gs_mem.h"

extern CNK_LIGHT NaCnkLightSs;
extern VU1_COLOR NaCnkAmbientSs;
extern unsigned int ulNaCnkFlagModelClip;

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

static int count_drawn(void)
{
    int x, y, n = 0;

    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
            n += fb(x, y) != 0;
    return n;
}

/* CVX_TEST_DUMP=prefix writes the frames as PPM images, to look at them. */
static void dump(const char* name)
{
    const char* prefix = getenv("CVX_TEST_DUMP");
    char path[512];
    FILE* f;
    int x, y;

    if (prefix == NULL)
        return;
    snprintf(path, sizeof(path), "%s%s.ppm", prefix, name);
    f = fopen(path, "wb");
    if (f == NULL)
        return;
    fprintf(f, "P6 640 480 255\n");
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
        {
            unsigned int c = fb(x, y);
            fputc(c & 0xFF, f);
            fputc((c >> 8) & 0xFF, f);
            fputc((c >> 16) & 0xFF, f);
        }
    fclose(f);
}

static void clear(void)
{
    int x, y;

    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
        {
            gs_write_pixel(GS_PSMCT32, 0, 10, x, y, 0);
            gs_write_pixel(GS_PSMZ16S, 300 * 32, 10, x, y, 0);
        }
}

/* A model with a CvVnPs2 vertex chunk and one Cs strip chunk. */
static unsigned int vlist[256] __attribute__((aligned(16)));
static unsigned short plist[64];
static NJS_CNK_MODEL model;
static NJS_VERTEX_BUF vbuf[64] __attribute__((aligned(16)));
static NJS_MATRIX matrices[16];

static void make_model(const float (*pos)[3], int n)
{
    unsigned char* c = (unsigned char*)vlist;
    float* f;
    int i;

    memset(vlist, 0, sizeof(vlist));
    c[0] = 51; /* njCnkCvVnPs2 */
    ((unsigned short*)c)[1] = 0;
    ((unsigned short*)c)[2] = 0; /* first vertex buffer entry */
    ((unsigned short*)c)[3] = n; /* vertex count */
    f = (float*)(c + 64);
    for (i = 0; i < n; i++, f += 8)
    {
        f[0] = pos[i][0];
        f[1] = pos[i][1];
        f[2] = pos[i][2];
        f[4] = 0; /* normal towards the camera */
        f[5] = 0;
        f[6] = -1.0f;
    }

    i = 0;
    plist[i++] = 64;         /* njCnkCs, head bits 0 */
    plist[i++] = 0;          /* size (unused) */
    plist[i++] = 1;          /* one strip, no extra data per vertex */
    plist[i++] = n;          /* strip length */
    {
        int k;
        for (k = 0; k < n; k++)
            plist[i++] = k;
    }
    plist[i++] = 0xFF;       /* end */

    memset(&model, 0, sizeof(model));
    model.vlist = (Sint32*)vlist;
    model.plist = (Sint16*)plist;
}

static void draw(void)
{
    njUnitMatrix(NULL);
    njCnkDrawModelLocal(&model);
}

int main(void)
{
    static const float quad[4][3] = {
        { -10, -10, 100 }, { 10, -10, 100 }, { -10, 10, 100 }, { 10, 10, 100 }
    };
    /* Crosses the near plane: the vertex at z = -50 is behind the camera. */
    static const float near_tri[3][3] = {
        { -20, 10, 60 }, { 20, 10, 60 }, { 0, 10, -50 }
    };
    NJS_SCREEN scr = { 320.0f, 640.0f, 480.0f, 320.0f, 240.0f };
    int n;

    gs_reset();
    gs_write_reg(0x4c, 0 | (10ull << 16));                               /* FRAME_1 */
    gs_write_reg(0x4e, 300 | (2ull << 24));                              /* ZBUF_1: Z16S */
    gs_write_reg(0x18, (1728ull << 4) | ((1808ull << 4) << 32));         /* XYOFFSET_1 */
    gs_write_reg(0x40, 639ull << 16 | (479ull << 48));                   /* SCISSOR_1 */
    gs_write_reg(0x42, 0);                                               /* ALPHA_1 */
    clear();

    njInitMatrix(matrices, 16, 0);
    njSetScreen(&scr);
    njSetAspect(1.0f, 1.0f);
    njClipZ(-1.0f, -10000.0f);
    njInit3D(vbuf, 64);
    njCnkSetCurrentDrawMode(2);
    ulNaCnkFlagModelClip = 0;

    /* A white light along the view direction, no ambient. */
    njCnkSetSimpleLight(0, 0, 1.0f);
    njCnkSetSimpleLightColor(1.0f, 1.0f, 1.0f);
    njCnkSetSimpleLightIntensity(1.0f, 0.0f);

    /* Square of 20 units at 100 units: 64 pixels around the centre. */
    make_model(quad, 4);
    draw();

    dump("quad");
    n = count_drawn();
    CHECK(n > 60 * 60 && n < 66 * 66);
    CHECK(fb(320, 240) != 0);
    CHECK(fb(300, 225) != 0);
    CHECK(fb(250, 240) == 0);
    CHECK(fb(320, 300) == 0);
    /* Lit head-on by a white light: grey (diffuse 1.0 = 128), no tint. */
    CHECK((fb(320, 240) & 0xFF) >= 120 && (fb(320, 240) & 0xFF) <= 128);
    CHECK((fb(320, 240) >> 16) == (fb(320, 240) & 0xFF));
    printf("quad: %d pixels, centre %06X\n", n, fb(320, 240));

    /* The near-plane triangle is scissored, not drawn across the screen
     * with a vertex projected from behind the camera. */
    clear();
    make_model(near_tri, 3);
    draw();
    dump("near");
    n = count_drawn();
    printf("near triangle: %d pixels\n", n);
    CHECK(n > 0);
    /* Its visible part is below the centre line (y = 10 is below). */
    CHECK(fb(320, 100) == 0);
    CHECK(fb(320, 300) != 0);

    if (failures == 0)
        printf("test_3d: all checks passed\n");
    return failures != 0;
}
