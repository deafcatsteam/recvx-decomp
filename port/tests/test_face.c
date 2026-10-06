/*
 * Facial animation (the PC versions of _fmCnkCalc*) on a small made-up face:
 * muscles move vertices by a weighted mix of their vectors, the jaw turns
 * about its hinge, the tongue bends and stretches, the eyes look at a point.
 */
#include "face.h"
#include "face_bh.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)
/* Degrees: the game's sine table is a little coarse. */
#define NEAR_DEG(a, b) (fabsf((a) - (b)) < 0.05f)

/* Type 51 vertex lists: 16 header words, then 8 floats per vertex. */
#define VERTS 8
static int src_list[16 + VERTS * 8], dst_list[16 + VERTS * 8];
static int tsrc_list[16 + VERTS * 8], tdst_list[16 + VERTS * 8];
static NJS_CNK_MODEL src, dst, tsrc, tdst;
static MASK_WORK fm;

static float* vtx(int* list, int id)
{
    return (float*)&list[16 + id * 8];
}

static void set_vtx(int* list, int id, float x, float y, float z)
{
    float* v = vtx(list, id);

    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 7.0f; /* must stay as it is */
}

static float dist_to_hinge(const float* v)
{
    /* The hinge runs along x through (0, 5, 0). */
    return sqrtf((v[1] - 5.0f) * (v[1] - 5.0f) + v[2] * v[2]);
}

int main(void)
{
    static VLIST_WORK vlist[2] = { { 0, 2, 0 }, { 1, 1, 0 } };
    static LIST_WORK con[3] = {
        { 0, { 1.0f, 0.0f, 0.0f }, 1.0f },
        { 1, { 0.0f, 2.0f, 0.0f }, 3.0f },
        { 2, { 5.0f, 5.0f, 5.0f }, 1.0f },
    };
    static TANG_WORK jaw[2] = { { 4, 1.0f }, { 5, 0.5f } };
    static TANG_WORK tang[2] = { { 0, 1.0f }, { 1, 1.0f } };
    static NJS_CNK_OBJECT eye;
    float* v;
    float full[3];

    _Make_SinTable(); /* done at start-up by the game */

    src.vlist = src_list;
    dst.vlist = dst_list;
    tsrc.vlist = tsrc_list;
    tdst.vlist = tdst_list;

    set_vtx(src_list, 0, 1, 2, 3);
    set_vtx(src_list, 1, 4, 5, 6);
    set_vtx(src_list, 2, 0, 5, 0);  /* hinge */
    set_vtx(src_list, 3, 2, 5, 0);  /* hinge */
    set_vtx(src_list, 4, 1, 5, 3);  /* chin */
    set_vtx(src_list, 5, 1, 5, 3);  /* half on the jaw */
    set_vtx(src_list, 6, 1, 5, 3);
    memcpy(dst_list, src_list, sizeof(src_list));

    fm.src = &src;
    fm.dst = &dst;
    fm.vtype = 51;
    fm.vtop = 16;
    fm.ntop = 20;
    fm.vofs = 8;
    fm.mode = 1;
    fm.vlist = vlist;
    fm.list = con;
    fm.lnum = 2;
    fm.jaw = jaw;
    fm.jnum = 2;
    fm.tang = tang;
    fm.tnum = 2;
    fm.tangsrc = &tsrc;
    fm.tangdst = &tdst;
    fm.eyesrc[0] = &eye;
    njUnitMatrix(&fm.eyemat[0]);
    _fmCnkSetJaw(&fm, 2, 3);

    fm.param.tangz = 1.0f; /* the face data's rest pose */
    set_vtx(tsrc_list, 0, 1, 2, 3);
    set_vtx(tsrc_list, 1, 0, 1, 0);
    memcpy(tdst_list, tsrc_list, sizeof(tsrc_list));

    /* At rest nothing moves. */
    fmCnkCalcFace(&fm);
    CHECK(memcmp(dst_list, src_list, sizeof(src_list)) == 0);
    CHECK(memcmp(tdst_list, tsrc_list, sizeof(tsrc_list)) == 0);

    /* Muscles: weights 0.5 and 3, moves 0.25 * (1,0,0) - 3 * (0,2,0). */
    fm.param.muscle[0] = 0.5f;
    fm.param.muscle[1] = -1.0f;
    fmCnkCalcFace(&fm);
    v = vtx(dst_list, 0);
    CHECK(NEAR(v[0], 1.0f + 0.25f / 3.5f));
    CHECK(NEAR(v[1], 2.0f - 6.0f / 3.5f));
    CHECK(NEAR(v[2], 3.0f));
    CHECK(v[3] == 7.0f);
    /* Its only muscle is at rest: back to the rest pose. */
    CHECK(memcmp(vtx(dst_list, 1), vtx(src_list, 1), 16) == 0);

    /* Jaw: the chin turns about the hinge, the half one by half as much. */
    fm.param.muscle[0] = 0;
    fm.param.muscle[1] = 0;
    fm.param.jawang = 30.0f;
    fmCnkCalcFace(&fm);
    v = vtx(dst_list, 4);
    CHECK(NEAR(v[0], 1.0f));
    CHECK(NEAR(dist_to_hinge(v), 3.0f));
    CHECK(NEAR_DEG(fabsf(atan2f(v[1] - 5.0f, v[2])) * 180.0f / 3.14159265f, 30.0f));
    CHECK(v[3] == 7.0f);
    memcpy(full, v, sizeof(full));
    v = vtx(dst_list, 5);
    CHECK(NEAR(v[0], 1.0f));
    CHECK(NEAR(dist_to_hinge(v), 3.0f));
    CHECK(NEAR_DEG(fabsf(atan2f(v[1] - 5.0f, v[2])) * 180.0f / 3.14159265f, 15.0f));
    CHECK((v[1] - 5.0f) * (full[1] - 5.0f) > 0); /* same way */
    CHECK(memcmp(vtx(dst_list, 6), vtx(src_list, 6), 16) == 0); /* not on the jaw */

    /* Both ways of turning agree when the share is (almost) all of it. */
    jaw[1].rate = 0.9999999f;
    fm.param.jawtrans = 0.5f;
    fmCnkCalcFace(&fm);
    CHECK(NEAR(vtx(dst_list, 4)[0], 1.5f));
    CHECK(NEAR(vtx(dst_list, 5)[0], vtx(dst_list, 4)[0]));
    CHECK(NEAR(vtx(dst_list, 5)[1], vtx(dst_list, 4)[1]));
    CHECK(NEAR(vtx(dst_list, 5)[2], vtx(dst_list, 4)[2]));

    /* The chin is re-made from the rest pose each frame, not turned again. */
    fmCnkCalcFace(&fm);
    CHECK(NEAR(dist_to_hinge(vtx(dst_list, 4)), 3.0f));
    CHECK(NEAR(vtx(dst_list, 4)[0], 1.5f));

    /* Tongue: stretched 2x along z, then turned 90 degrees about x. */
    fm.param.tangz = 2.0f;
    fmCnkCalcFace(&fm);
    v = vtx(tdst_list, 0);
    CHECK(NEAR(v[0], 1.0f) && NEAR(v[1], 2.0f) && NEAR(v[2], 6.0f));
    CHECK(v[3] == 7.0f);
    fm.param.tangz = 1.0f;
    fm.param.tangx = 90.0f;
    fmCnkCalcFace(&fm);
    v = vtx(tdst_list, 1);
    CHECK(NEAR(v[0], 0.0f) && NEAR(v[1], 0.0f) && NEAR(fabsf(v[2]), 1.0f));

    /* Eyes: straight ahead, then 45 degrees aside, then 45 degrees up. */
    fm.param.eye.x = 0;
    fm.param.eye.y = 0;
    fm.param.eye.z = -10.0f;
    fmCnkCalcFace(&fm);
    CHECK(eye.ang[0] == 0 && eye.ang[1] == 0 && eye.ang[2] == 0);
    fm.param.eye.x = 10.0f;
    fmCnkCalcFace(&fm);
    CHECK(eye.ang[0] == 0 && (eye.ang[1] == -8192 || eye.ang[1] == -8191));
    fm.param.eye.x = 0;
    fm.param.eye.y = 10.0f;
    fmCnkCalcFace(&fm);
    CHECK((eye.ang[0] == 8192 || eye.ang[0] == 8191) && eye.ang[1] == 0);

    if (failures == 0)
        printf("face: all checks passed\n");
    return failures != 0;
}
