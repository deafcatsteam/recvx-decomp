/*
 * 60 images a second (fps60 = yes in cvx.ini).
 *
 * The game draws 30 frames a second, each shown for two V-blanks. Here, in
 * game, a picture halfway between each frame and the one before is shown
 * for the first V-blank, and the frame for the second:
 *
 * - while a frame is drawn, the GS records what it receives (gs_rec_start);
 *   the 3D strips of the game's models (pc_render3d.c, pack_prim3d) give
 *   their vertices to pc_interp_vertex, grouped by model and by how many
 *   times that model was drawn before in the frame (pc_interp_model), and
 *   the GS finds them again in what it records by their contents;
 * - at the end of the frame (pc_interp_frame), each model drawn with as
 *   many vertices as in the frame before gets its vertices halfway between
 *   the two frames (screen position, depth, texture perspective, fog,
 *   colour), and the GS draws the recorded frame again with them
 *   (gs_rec_replay): that is the picture shown first.
 *
 * What is not a model (particles, 2D, text) is drawn as in the new frame.
 * Nothing is made up when the camera cuts to another view, a model moved
 * too far, the frame took too long, or outside the game itself (menus,
 * movies): the frame is then shown for both V-blanks, as before.
 */
#include "ps2_dummy.h"
#include "main.h"

#include "../gs/gs.h"
#include "../host/pc_host.h"
#include "../platform/pc_platform.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int pc_widescreen_on(unsigned int tk_flg, unsigned int ts_flg);

typedef struct {
    const void *model;
    uint32_t occurrence; /* this model drawn that many times before in the frame */
    uint32_t first, count;
} Instance;

typedef struct {
    Instance *inst;
    uint32_t ninst, cap_inst;
    uint32_t (*vert)[12];
    uint32_t *vert_inst; /* instance of each vertex */
    uint32_t nvert, cap_vert, cap_vert_inst;
    float cam[16];       /* camera matrix */
    float cam_pos[3];
    int cut;             /* camera cut number */
    int valid;
} Frame;

#define MAX_VERTICES (1 << 17)
#define HASH_SIZE (1 << 19)
#define MODEL_SLOTS 4096

static Frame frames[2];
static int cur;           /* frames[cur] is being drawn, frames[cur ^ 1] is the one before */
static int enabled = -1;
static int active;        /* this frame is recorded */
static int instance = -1; /* of the strips drawn now */
static uint32_t stat_frames, stat_between; /* for the log */

/* vertex contents -> vertex, for this frame (gen tells which entries are) */
static struct {
    uint64_t key;
    uint32_t gen;
    int32_t vertex;
} table[HASH_SIZE];
static uint32_t table_gen;

/* model -> times drawn in this frame */
static struct {
    const void *model;
    uint32_t gen, count;
} models[MODEL_SLOTS];

/* for each instance of this frame: the first vertex of the same one in the
 * frame before, or -1 */
static int32_t *match;
static uint32_t cap_match;

static uint64_t hash_words(const uint32_t *w)
{
    uint64_t h = 1469598103934665603ull;

    for (int i = 0; i < 12; i++) {
        h ^= w[i];
        h *= 1099511628211ull;
        h ^= h >> 29;
    }
    return h | 1; /* never 0 */
}

static int grow(void **p, uint32_t *cap, uint32_t need, size_t size)
{
    uint32_t c = *cap ? *cap : 1024;
    void *q;

    if (need <= *cap)
        return 1;
    while (c < need)
        c *= 2;
    q = realloc(*p, c * size);
    if (q == NULL)
        return 0;
    *p = q;
    *cap = c;
    return 1;
}

static int vertex_id(const uint32_t words[12])
{
    uint64_t key = hash_words(words);
    uint32_t i = (uint32_t)key & (HASH_SIZE - 1);

    for (int probe = 0; probe < 64; probe++, i = (i + 1) & (HASH_SIZE - 1)) {
        if (table[i].gen != table_gen)
            return -1;
        if (table[i].key == key)
            return table[i].vertex;
    }
    return -1;
}

void pc_interp_model(const void *model, int mod)
{
    Frame *f = &frames[cur];
    uint32_t slot, count = 0;

    if (!active)
        return;
    model = (const char *)model + mod; /* a shadow is another thing to find */
    slot = (uint32_t)(((uintptr_t)model >> 4) * 2654435761u) & (MODEL_SLOTS - 1);
    for (int probe = 0; probe < MODEL_SLOTS; probe++, slot = (slot + 1) & (MODEL_SLOTS - 1)) {
        if (models[slot].gen != table_gen) {
            models[slot].gen = table_gen;
            models[slot].model = model;
            models[slot].count = 0;
        }
        if (models[slot].model == model) {
            count = models[slot].count++;
            break;
        }
    }
    if (!grow((void **)&f->inst, &f->cap_inst, f->ninst + 1, sizeof(Instance))) {
        instance = -1;
        return;
    }
    instance = (int)f->ninst++;
    f->inst[instance].model = model;
    f->inst[instance].occurrence = count;
    f->inst[instance].first = f->nvert;
    f->inst[instance].count = 0;
}

void pc_interp_vertex(const unsigned int *words)
{
    Frame *f = &frames[cur];
    uint64_t key;
    uint32_t i;

    if (!active || instance < 0 || f->nvert >= MAX_VERTICES)
        return;
    if (!grow((void **)&f->vert, &f->cap_vert, f->nvert + 1, sizeof(f->vert[0])) ||
        !grow((void **)&f->vert_inst, &f->cap_vert_inst, f->nvert + 1, sizeof(uint32_t)))
        return;
    /* Only the first of identical vertices (the same point of a strip sent
     * twice) is kept for finding: it moves the same way. */
    key = hash_words(words);
    i = (uint32_t)key & (HASH_SIZE - 1);
    for (int probe = 0; probe < 64; probe++, i = (i + 1) & (HASH_SIZE - 1)) {
        if (table[i].gen != table_gen) {
            table[i].gen = table_gen;
            table[i].key = key;
            table[i].vertex = (int32_t)f->nvert;
            break;
        }
        if (table[i].key == key)
            break;
    }
    memcpy(f->vert[f->nvert], words, 48);
    f->vert_inst[f->nvert] = (uint32_t)instance;
    f->nvert++;
    f->inst[instance].count++;
}

static int lerp_int(int a, int b)
{
    return a + (b - a) / 2;
}

static void patch(int id, uint32_t w[12])
{
    const Frame *f = &frames[cur], *p = &frames[cur ^ 1];
    uint32_t inst, k;
    const uint32_t *o;
    float s1, t1, q1, q0, q;

    if (id < 0 || (uint32_t)id >= f->nvert)
        return;
    inst = f->vert_inst[id];
    if (match[inst] < 0)
        return;
    k = (uint32_t)id - f->inst[inst].first;
    o = p->vert[(uint32_t)match[inst] + k];

    /* texture: same (u, v), with Q (1/z) halfway */
    memcpy(&s1, &w[0], 4);
    memcpy(&t1, &w[1], 4);
    memcpy(&q1, &w[2], 4);
    memcpy(&q0, &o[2], 4);
    if (q1 != 0.0f && q0 == q0 && q1 == q1) {
        q = (q0 + q1) * 0.5f;
        s1 = s1 / q1 * q;
        t1 = t1 / q1 * q;
        memcpy(&w[0], &s1, 4);
        memcpy(&w[1], &t1, 4);
        memcpy(&w[2], &q, 4);
    }
    /* colour (alpha as now) */
    for (int c = 4; c < 7; c++)
        w[c] = (uint32_t)lerp_int((int)o[c], (int)w[c]);
    /* position and depth */
    w[8] = (uint32_t)lerp_int((int)o[8], (int)w[8]);
    w[9] = (uint32_t)lerp_int((int)o[9], (int)w[9]);
    w[10] = (uint32_t)lerp_int((int)o[10], (int)w[10]);
    /* fog, in bits 4-11, the flags as now */
    w[11] = (w[11] & ~0xff0u) |
            ((uint32_t)lerp_int((int)(o[11] >> 4 & 0xff), (int)(w[11] >> 4 & 0xff)) << 4);
}

/* Finds each instance of this frame in the frame before; returns how many
 * vertices will move. */
static uint32_t find_matches(void)
{
    const Frame *f = &frames[cur], *p = &frames[cur ^ 1];
    const int32_t limit = 96 * 16; /* a model that moved more on screen is left where it is */
    uint32_t moving = 0, far = 0;

    if (!grow((void **)&match, &cap_match, f->ninst + 1, sizeof(int32_t)))
        return 0;
    for (uint32_t i = 0; i < f->ninst; i++) {
        const Instance *a = &f->inst[i];
        match[i] = -1;
        if (a->count == 0)
            continue;
        /* usually at the same place in the list as before */
        for (uint32_t j0 = 0; j0 < p->ninst; j0++) {
            uint32_t j = (i + j0) % p->ninst;
            const Instance *b = &p->inst[j];
            if (b->model != a->model || b->occurrence != a->occurrence)
                continue;
            if (b->count == a->count) {
                int ok = 1;
                for (uint32_t k = 0; k < a->count && ok; k++) {
                    const uint32_t *n = f->vert[a->first + k], *o = p->vert[b->first + k];
                    int32_t dx = (int32_t)n[8] - (int32_t)o[8], dy = (int32_t)n[9] - (int32_t)o[9];
                    ok = dx < limit && dx > -limit && dy < limit && dy > -limit;
                }
                if (ok) {
                    match[i] = (int32_t)b->first;
                    moving += a->count;
                } else {
                    far += a->count;
                }
            }
            break;
        }
    }
    /* Most of the picture jumped: a new view, not a movement. */
    return far > moving ? 0 : moving;
}

static void camera_now(Frame *f)
{
    if (cam.mtx != NULL)
        memcpy(f->cam, cam.mtx, sizeof(f->cam));
    f->cam_pos[0] = cam.px;
    f->cam_pos[1] = cam.py;
    f->cam_pos[2] = cam.pz;
    f->cut = cam.ncut;
}

/* The camera changed view (a cut), rather than moved a little. */
static int camera_cut(const Frame *a, const Frame *b)
{
    float rot = 0.0f, dx = a->cam_pos[0] - b->cam_pos[0], dy = a->cam_pos[1] - b->cam_pos[1],
          dz = a->cam_pos[2] - b->cam_pos[2];

    if (a->cut != b->cut)
        return 1;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            rot += fabsf(a->cam[r * 4 + c] - b->cam[r * 4 + c]);
    return rot > 0.6f || dx * dx + dy * dy + dz * dz > 1500.0f * 1500.0f;
}

/* For the tests: on or off without cvx.ini. */
void pc_interp_enable(int on)
{
    enabled = on;
    gs_rec_vertex_id = vertex_id;
}

void pc_interp_begin(void)
{
    Frame *f;

    if (enabled < 0) {
        enabled = pc_config_yes("fps60", 0);
        gs_rec_vertex_id = vertex_id;
        if (enabled)
            printf("fps60: on, a picture between two frames in game\n");
    }
    active = enabled && pc_widescreen_on(sys->tk_flg, sys->ts_flg) && !pc_turbo;
    instance = -1;
    if (!active)
        return;
    cur ^= 1;
    f = &frames[cur];
    f->ninst = 0;
    f->nvert = 0;
    f->valid = 0;
    if (++table_gen == 0)
        table_gen = 1;
    gs_rec_start();
}

void pc_interp_frame(void)
{
    Frame *f = &frames[cur], *p = &frames[cur ^ 1];
    int show_mid;

    if (!enabled)
        return;
    gs_rec_stop();
    if (!active) {
        frames[0].valid = frames[1].valid = 0;
        return;
    }
    active = 0;
    instance = -1;
    camera_now(f);
    f->valid = 1;
    /* the frame just drawn, on screen from the next V-blank */
    pc_gs_show_dbuff(&Db, Ps2_dbuff);
    show_mid = p->valid && !camera_cut(f, p) && gs_rec_vertices() > 0 && find_matches() > 0;
    stat_frames++;
    if (stat_frames == 900) { /* every 30 seconds of game */
        printf("fps60: %u of the last %u frames had a picture before them\n", stat_between, stat_frames);
        stat_frames = stat_between = 0;
    }
    if (show_mid && gs_rec_replay(patch)) {
        stat_between++;
        /* first V-blank: the picture between the two frames */
        gs_show_held(1);
        pc_wait_vblank();
        gs_show_held(0);
    }
}
