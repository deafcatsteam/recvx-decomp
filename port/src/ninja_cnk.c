/*
 * Sega Ninja chunked-model renderer (Phase 3 of Path B).
 *
 * The PS2 source (src/ps2/veronica/prog/ps2_NinjaCnk.c, ~3500 lines)
 * is full of VU1 microcode DMA dispatch and EE inline asm — none of
 * it compiles on MSVC x64. This file is a from-scratch port that
 * walks the same on-disk chunk format and emits triangles to the
 * port gfx layer via recvx_gfx_draw_tri3d.
 *
 * On-disk format reference (KATANA chunked-model spec, mirrored in
 * include/recvx-decomp-katana/KATANA/Include/ninjacnk.h):
 *
 *   CHUNK_HEAD { u8 type; u8 headBits; u16 sizeInWords; }
 *
 *   Vertex chunk (types 32..50):
 *     header + { u16 baseIndex; u16 count; <count> verts of stride S }
 *     where S depends on type (see vertex stride table below).
 *
 *   Polygon-list chunk stream:
 *     a sequence of chunks terminated by NJD_CE (type 255). Each chunk
 *     occupies `4 bytes header + sizeInWords * 2 bytes` of payload so
 *     unknown chunk types can be safely skipped using the size field.
 *
 *   Strip chunk (types 64..75):
 *     header + u16 { usNext:2 | usStrip:14 } + <usStrip> strips,
 *     where each strip is { i16 usMax; verts... }. Negative usMax means
 *     flipped winding. Per-vertex stride depends on type:
 *       NJD_CS         (64): 1 u16 (index)
 *       NJD_CS_UVN     (65): 3 u16 (index, u, v)   UV in [0,255]
 *       NJD_CS_UVH     (66): 3 u16 (index, u, v)   UV in [0,1023]
 *       NJD_CS_VN      (67): 1 u16 (same as CS)
 *       NJD_CS_D8      (70): 3 u16 (index, color_lo, color_hi)
 *       NJD_CS_UVN_D8  (71): 5 u16
 *       NJD_CS_UVH_D8  (72): 5 u16
 *     Plus usNext u16s of skip data per vertex starting at vertex 3.
 *
 * Phase 3a scope: positions + UV + per-vertex color. No lighting math
 * (use ambient + diffuse material as flat tint), no texture binding
 * yet (renders white-textured shape with vertex color). Phase 3b will
 * wire material chunks + texture lookup.
 */

#include "ninja.h"
#include "types.h"   /* CHUNK_HEAD layout */

#include <math.h>
#include <string.h>
#include <stdint.h>

#include "recvx_port.h"

/* Pulled from ninja_3d.c — top-of-stack matrix (model->world for whoever
 * called us; PS2 game pre-multiplies camera into this same stack). */
extern NJS_MATRIX*  pNaMatMatrixStuckPtr;

/* -------------------------------------------------------------------- */
/* Vertex working buffer                                                */
/* -------------------------------------------------------------------- */

/* Rooms carry far more verts than inventory items — rm_0130's mesh
 * alone tops 4096. 32768 matches the u16 index space upper half. */
#define CNK_VBUF_MAX 32768

typedef struct {
    float    x, y, z;
    float    nx, ny, nz;
    uint32_t color;       /* per-vertex tint; 0xFFFFFFFF when chunk type
                             doesn't carry vertex color */
} cnk_vert_t;

static cnk_vert_t g_cnk_vbuf[CNK_VBUF_MAX];

/* -------------------------------------------------------------------- */
/* Vertex chunk handlers                                                */
/* -------------------------------------------------------------------- */

/* Read u16 at offset i (relative to the chunk's first payload word). */
#define CHUNK_PAYLOAD(h)  ((const unsigned short*)((const CHUNK_HEAD*)(h) + 1))
/* Advance to next chunk: 2 header words + payload sizeInWords. */
#define CHUNK_NEXT(h)     ((const CHUNK_HEAD*)((const unsigned short*)(h) + 2 + (h)->usSize))

static void cnk_decode_d8888(uint32_t in, uint32_t* argb) {
    /* NJD_D8888 = native ARGB8888; pass through. */
    *argb = in;
}

/* Vertex chunk: read base index + count from first two u16s of payload,
 * then `count` verts of stride-bytes each. Type number determines stride.
 * Returns the next chunk. The PS2 walkers (njCnkCvVn etc.) advance to the
 * END OF THE VERTEX DATA, not via usSize — usSize is unreliable for the
 * big room vertex chunks, so we do the same arithmetic. */
static const CHUNK_HEAD* cnk_handle_vert(const CHUNK_HEAD* h) {
    const unsigned short* sp = CHUNK_PAYLOAD(h);
    unsigned short base  = sp[0];
    unsigned short count = sp[1];
    unsigned int   orig_count = count;
    const float* fp = (const float*)(sp + 2);

    if (base + count > CNK_VBUF_MAX) {
        /* Refuse to overflow the buffer. Likely a corrupt model or
         * a chunk type we mis-decoded — silently clamp. */
        if (base >= CNK_VBUF_MAX) count = 0;
        else                      count = CNK_VBUF_MAX - base;
    }

    /* Per-type stride in float-words (4 bytes each).
     *   32 NJD_CV_SH      : x,y,z,1.0           (4f)
     *   33 NJD_CV_VN_SH   : x,y,z,1.0,nx,ny,nz,0 (8f)
     *   34 NJD_CV         : x,y,z              (3f)
     *   35 NJD_CV_D8      : x,y,z,d8888        (4f, last word is u32)
     *   38 NJD_CV_S5      : x,y,z,d565|s565    (4f, last word is u32 packed)
     *   41 NJD_CV_VN      : x,y,z,nx,ny,nz     (6f)
     *   42 NJD_CV_VN_D8   : x,y,z,nx,ny,nz,d8888 (7f)
     * Anything else for now: try base layout (3f xyz), color stays
     * white. Will iterate as we hit real models. */
    unsigned char type = h->ucType;
    unsigned int stride = 0;  /* in floats, for the end-of-data return */

    switch (type) {
    case 32: /* NJD_CV_SH */
        stride = 4;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 4;
            v->x = p[0]; v->y = p[1]; v->z = p[2];
            v->nx = 0; v->ny = 0; v->nz = 0;
            v->color = 0xFFFFFFFFu;
        }
        break;
    case 33: /* NJD_CV_VN_SH */
        stride = 8;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 8;
            v->x  = p[0]; v->y  = p[1]; v->z  = p[2];
            v->nx = p[4]; v->ny = p[5]; v->nz = p[6];
            v->color = 0xFFFFFFFFu;
        }
        break;
    case 34: /* NJD_CV */
        stride = 3;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 3;
            v->x = p[0]; v->y = p[1]; v->z = p[2];
            v->nx = 0; v->ny = 0; v->nz = 0;
            v->color = 0xFFFFFFFFu;
        }
        break;
    case 35: /* NJD_CV_D8 */
        stride = 4;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 4;
            v->x = p[0]; v->y = p[1]; v->z = p[2];
            v->nx = 0; v->ny = 0; v->nz = 0;
            cnk_decode_d8888(*(const uint32_t*)&p[3], &v->color);
        }
        break;
    case 41: /* NJD_CV_VN */
        stride = 6;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 6;
            v->x  = p[0]; v->y  = p[1]; v->z  = p[2];
            v->nx = p[3]; v->ny = p[4]; v->nz = p[5];
            v->color = 0xFFFFFFFFu;
        }
        break;
    case 42: /* NJD_CV_VN_D8 */
        stride = 7;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 7;
            v->x  = p[0]; v->y  = p[1]; v->z  = p[2];
            v->nx = p[3]; v->ny = p[4]; v->nz = p[5];
            cnk_decode_d8888(*(const uint32_t*)&p[6], &v->color);
        }
        break;
    case 51: /* Capcom PS2 vertex chunk (pCnkFuncTbl[51] = njCnkCvVnPs2).
              * Layout decoded from ps2_NinjaCnk.c:1280: after base/count
              * come 14 floats of VU0 clip data, then count verts of
              * { x,y,z,w, nx,ny,nz,w } (two quadwords, stride 8 floats).
              * This is what every room mesh uses. */
        fp += 14;
        stride = 8;
        for (unsigned i = 0; i < count; ++i) {
            cnk_vert_t* v = &g_cnk_vbuf[base + i];
            const float* p = fp + (size_t)i * 8;
            v->x  = p[0]; v->y  = p[1]; v->z  = p[2];
            v->nx = p[4]; v->ny = p[5]; v->nz = p[6];
            v->color = 0xFFFFFFFFu;
        }
        break;
    default:
        /* Unknown vertex chunk — leave buffer as-is and fall back to the
         * usSize skip below. */
        return CHUNK_NEXT(h);
    }

    /* End of vertex data = next chunk (PS2 semantics). */
    return (const CHUNK_HEAD*)(fp + (size_t)orig_count * stride);
}

/* -------------------------------------------------------------------- */
/* Strip chunk handlers                                                 */
/* -------------------------------------------------------------------- */

/* Emit triangles via recvx_gfx_draw_tri3d. The TRIANGLE_STRIP pattern
 * generates a flipped winding on every other triangle — we handle that
 * by swapping i1/i2 instead of relying on GL_TRIANGLE_STRIP (we use
 * GL_TRIANGLES so each draw call is independent and skipped/clipped
 * triangles don't tangle neighbors). */
static void cnk_emit_strip_tri(int i0, int i1, int i2,
                               float u0, float v0,
                               float u1, float v1,
                               float u2, float v2,
                               uint32_t base_color,
                               int trans)
{
    recvx_gfx_vtx3d tri[3];
    const cnk_vert_t* a = &g_cnk_vbuf[i0];
    const cnk_vert_t* b = &g_cnk_vbuf[i1];
    const cnk_vert_t* c = &g_cnk_vbuf[i2];

    tri[0].x = a->x; tri[0].y = a->y; tri[0].z = a->z;
    tri[0].u = u0;   tri[0].v = v0;
    tri[0].color = a->color & base_color;
    tri[1].x = b->x; tri[1].y = b->y; tri[1].z = b->z;
    tri[1].u = u1;   tri[1].v = v1;
    tri[1].color = b->color & base_color;
    tri[2].x = c->x; tri[2].y = c->y; tri[2].z = c->z;
    tri[2].u = u2;   tri[2].v = v2;
    tri[2].color = c->color & base_color;

    recvx_gfx_draw_tri3d(-1, tri, 3, trans);
    extern long g_cnk_tris_dbg;
    g_cnk_tris_dbg++;
}
long g_cnk_tris_dbg = 0;

/* Generic strip walker. `stride_words` is per-vertex stride in u16s.
 * For NJD_CS that's 1 (just index). For UV variants it's 3 (idx, u, v).
 * `uv_scale` converts integer UV to [0,1] (1/256 for UVN, 1/1024 for
 * UVH, 0 for non-UV strips). */
static const CHUNK_HEAD* cnk_walk_strip_generic(const CHUNK_HEAD* h,
                                                int stride_words,
                                                float uv_scale,
                                                int trans)
{
    const unsigned short* sp = CHUNK_PAYLOAD(h);

    unsigned usNext = (sp[0] >> 14) & 0x3;
    unsigned usStrip = sp[0] & 0x3FFF;
    ++sp;

    while (usStrip--) {
        int clip = (sp[0] & 0x8000) ? 1 : 0;
        int usMax = clip ? (int)((unsigned short)(~sp[0] + 1)) : (int)sp[0];
        ++sp;

        if (usMax < 3) {
            /* Skip degenerate strips. Advance past their vertex data. */
            sp += stride_words * usMax;
            continue;
        }

        /* First two verts have no per-vertex skip; subsequent verts skip
         * an extra usNext u16s after their stride. Pattern from
         * ps2_NinjaCnk.c:njCnkCs lines 1786-1819. */
        int idx[3]; float uv_u[3], uv_v[3];

        idx[0] = sp[0];
        uv_u[0] = stride_words >= 3 ? sp[1] * uv_scale : 0.0f;
        uv_v[0] = stride_words >= 3 ? sp[2] * uv_scale : 0.0f;
        sp += stride_words;

        idx[1] = sp[0];
        uv_u[1] = stride_words >= 3 ? sp[1] * uv_scale : 0.0f;
        uv_v[1] = stride_words >= 3 ? sp[2] * uv_scale : 0.0f;
        sp += stride_words;

        int parity = 0;  /* flips winding every other triangle */
        for (int i = 2; i < usMax; ++i) {
            idx[2] = sp[0];
            uv_u[2] = stride_words >= 3 ? sp[1] * uv_scale : 0.0f;
            uv_v[2] = stride_words >= 3 ? sp[2] * uv_scale : 0.0f;
            sp += stride_words;
            sp += usNext;  /* per-vertex user-flag skip */

            /* Build the triangle in the correct winding. */
            int a = idx[0], b = idx[1], c = idx[2];
            float au = uv_u[0], av = uv_v[0];
            float bu = uv_u[1], bv = uv_v[1];
            float cu = uv_u[2], cv = uv_v[2];

            if (parity ^ clip) {
                int ti = b; b = c; c = ti;
                float tu = bu; bu = cu; cu = tu;
                float tv = bv; bv = cv; cv = tv;
            }

            /* Skip triangles whose indices look bad (out-of-buffer). */
            if ((unsigned)a < CNK_VBUF_MAX &&
                (unsigned)b < CNK_VBUF_MAX &&
                (unsigned)c < CNK_VBUF_MAX)
            {
                cnk_emit_strip_tri(a, b, c, au, av, bu, bv, cu, cv,
                                   0xFFFFFFFFu, trans);
            }

            /* Slide window: i1<-i2, i2<-(next iteration's read). */
            idx[0] = idx[1]; uv_u[0] = uv_u[1]; uv_v[0] = uv_v[1];
            idx[1] = idx[2]; uv_u[1] = uv_u[2]; uv_v[1] = uv_v[2];
            parity ^= 1;
        }
    }

    /* Round up to chunk's declared end (matches sizeInWords). */
    return CHUNK_NEXT(h);
}

/* -------------------------------------------------------------------- */
/* Polygon-list walker                                                  */
/* -------------------------------------------------------------------- */

static const CHUNK_HEAD* cnk_handle_polygon_chunk(const CHUNK_HEAD* h) {
    unsigned char type = h->ucType;
    int trans = (h->ucHeadBits & 0x8) ? 1 : 0;  /* alpha-blend bit */

    /* Short chunks have NO size field (ninjacnk.h taxonomy):
     *   0..7   NULL + bits chunks (NJD_CB_BA/DA/EXP/CP/DP) — 2 bytes total
     *   8..15  tiny chunks (NJD_CT_TID texture id)         — 4 bytes total
     * Reading a u16 "size" out of them desyncs the walk into garbage —
     * that was the 3.7M-triangle white-noise room draw. */
    if (type <= 7)
        return (const CHUNK_HEAD*)((const unsigned short*)h + 1);
    if (type <= 15)
        return (const CHUNK_HEAD*)((const unsigned short*)h + 2);

    /* Vertex chunks shouldn't appear in the polygon list normally, but
     * if they do (some models pack vlist+plist), handle them. */
    if (type >= 32 && type <= 51) {
        return cnk_handle_vert(h);
    }

    /* Strip chunks. */
    switch (type) {
    case 64: /* NJD_CS — plain triangle strip */
        return cnk_walk_strip_generic(h, 1, 0.0f, trans);
    case 65: /* NJD_CS_UVN — UV 0..255 */
        return cnk_walk_strip_generic(h, 3, 1.0f / 256.0f, trans);
    case 66: /* NJD_CS_UVH — UV 0..1023 */
        return cnk_walk_strip_generic(h, 3, 1.0f / 1024.0f, trans);
    case 67: /* NJD_CS_VN — plain w/ normals (we ignore normals) */
        return cnk_walk_strip_generic(h, 1, 0.0f, trans);
    default:
        /* Unknown sized chunk (materials 16..31, volumes 56..63, other
         * strip variants) — skip safely via header size. */
        return CHUNK_NEXT(h);
    }
}

/* -------------------------------------------------------------------- */
/* Public entry point                                                   */
/* -------------------------------------------------------------------- */

/* Core: walk one model's vertex + polygon chunks using the CURRENT
 * Ninja matrix stack as the model transform. Does NOT bracket
 * begin_3d/end_3d — the caller owns the 3D pass so multiple models in
 * an object tree share one projection/depth setup. */
static void cnk_draw_model_local(NJS_CNK_MODEL* model) {
    if (!model) return;
    {
        extern void recvx_log(const char* tag, const char* fmt, ...);
        static int dbg_m = 0;
        if (dbg_m < 24) {
            const CHUNK_HEAD* vh = (const CHUNK_HEAD*)model->vlist;
            const CHUNK_HEAD* ph = (const CHUNK_HEAD*)model->plist;
            recvx_log("cnk", "  draw_model_local model=%p vlist=%p(type=%d) plist=%p(type=%d) r=%.2f",
                      (void*)model,
                      (void*)model->vlist, vh ? vh->ucType : -1,
                      (void*)model->plist, ph ? ph->ucType : -1,
                      model->r);
            dbg_m++;
        }
    }

    /* Snapshot the current top-of-stack matrix as the gfx model
     * matrix. itemview pre-multiplies camera/rotation into this stack
     * before calling us, and the object-tree walk pushes per-node
     * pos/ang/scl, so this single snapshot captures the full
     * model-view transform for this node. */
    if (pNaMatMatrixStuckPtr) {
        recvx_gfx_set_model_matrix((const float*)pNaMatMatrixStuckPtr);
    }

    if (model->vlist) {
        /* The vlist is a CHAIN of vertex chunks terminated by NJD_CE
         * (255) — a room mesh ships several (different base indices).
         * Reading only the first left most of the vertex buffer stale. */
        const CHUNK_HEAD* vh = (const CHUNK_HEAD*)model->vlist;
        int vguard = 0;
        while (vh->ucType != 255 && vguard++ < 1024) {
            if (vh->ucType >= 32 && vh->ucType <= 51) {
                vh = cnk_handle_vert(vh);
            } else if (vh->ucType <= 7) {
                vh = (const CHUNK_HEAD*)((const unsigned short*)vh + 1);
            } else if (vh->ucType <= 15) {
                vh = (const CHUNK_HEAD*)((const unsigned short*)vh + 2);
            } else {
                vh = CHUNK_NEXT(vh);
            }
        }
    }

    if (model->plist) {
        const CHUNK_HEAD* ph = (const CHUNK_HEAD*)model->plist;
        int guard = 0;
        while (ph->ucType != 255 && guard++ < 4096) {
            ph = cnk_handle_polygon_chunk(ph);
            if (!ph) break;
        }
    }
}

/* njCnkEasyMultiDrawModel — direct single-model draw. itemview's
 * rdid==139 (FILE document) path calls this directly per page after
 * setting up the matrix stack itself. Self-brackets the 3D pass. */
void njCnkEasyMultiDrawModel(NJS_CNK_MODEL* model) {
    {
        extern void recvx_log(const char* tag, const char* fmt, ...);
        recvx_log("cnk", "njCnkEasyMultiDrawModel: model=%p vlist=%p plist=%p",
                  (void*)model,
                  model ? (void*)model->vlist : (void*)0,
                  model ? (void*)model->plist : (void*)0);
    }
    if (!model) return;

    recvx_gfx_begin_3d(60.0f, 4.0f, 140.0f);
    cnk_draw_model_local(model);
    recvx_gfx_end_3d();
}

/* Recursive object-tree walk. Mirrors ps2_NinjaCnk.c:2736
 * njCnkEasyMultiDrawObjectI — for each node in the sibling chain:
 * push matrix, apply pos/ang/scl (gated by evalflags), draw the
 * node's model, recurse into child, pop matrix.
 *
 * evalflags bits (from ps2_NinjaCnk.c):
 *   0x01 skip translate   0x02 skip rotate   0x04 skip scale
 *   0x08 skip model draw   0x10 skip child recurse
 * The 0x20 (rotate order) and 0x100 (conditional-recurse) bits are
 * ignored for now — XYZ order covers the inventory item models. */
static void cnk_draw_object_tree(NJS_CNK_OBJECT* o) {
    for (; o != NULL; o = o->sibling) {
        unsigned int f = o->evalflags;

        njPushMatrix(NULL);

        if (!(f & 0x1)) njTranslate(NULL, o->pos[0], o->pos[1], o->pos[2]);
        if (!(f & 0x2)) {
            njRotateX(NULL, o->ang[0]);
            njRotateY(NULL, o->ang[1]);
            njRotateZ(NULL, o->ang[2]);
        }
        if (!(f & 0x4)) njScale(NULL, o->scl[0], o->scl[1], o->scl[2]);

        if (!(f & 0x8)) cnk_draw_model_local(o->model);

        if (!(f & 0x10)) cnk_draw_object_tree(o->child);

        njPopMatrix(1);
    }
}

void njCnkEasyMultiDrawObjectI(NJS_CNK_OBJECT* obj) {
    {
        extern void recvx_log(const char* tag, const char* fmt, ...);
        recvx_log("cnk", "njCnkEasyMultiDrawObjectI: obj=%p model=%p child=%p",
                  (void*)obj,
                  obj ? (void*)obj->model : (void*)0,
                  obj ? (void*)obj->child : (void*)0);
    }
    if (!obj) return;

    extern long g_cnk_tris_dbg;
    long before = g_cnk_tris_dbg;

    recvx_gfx_begin_3d(60.0f, 4.0f, 140.0f);
    cnk_draw_object_tree(obj);
    recvx_gfx_end_3d();

    {
        extern void recvx_log(const char* tag, const char* fmt, ...);
        static int dbg_n = 0;
        if (dbg_n < 8) {
            recvx_log("cnk", "object tree emitted %ld triangles total",
                      g_cnk_tris_dbg - before);
            dbg_n++;
        }
    }
}

/* Room/world variants used by game.c's bhAllDrawModel. Same tree walk,
 * but a far plane sized for RE rooms (thousands of units) instead of
 * the inventory-examine close-up. The view matrix (cam.mtx) is already
 * on the nj matrix stack — bhAllDrawModel does njSetMatrix(0, cam.mtx)
 * right before calling us. */
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT* obj) {
    if (!obj) return;
    extern long g_cnk_tris_dbg;
    long before = g_cnk_tris_dbg;

    recvx_gfx_begin_3d(60.0f, 4.0f, 30000.0f);
    cnk_draw_object_tree(obj);
    recvx_gfx_end_3d();
    {
        extern void recvx_log(const char* tag, const char* fmt, ...);
        static int dbg_n = 0;
        if (dbg_n < 8) {
            recvx_log("cnk", "room object tree emitted %ld tris",
                      g_cnk_tris_dbg - before);
            dbg_n++;
        }
    }
}

void njCnkEasyDrawObject(NJS_CNK_OBJECT* obj) {
    njCnkEasyMultiDrawObject(obj);
}
