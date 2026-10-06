/*
 * What the software GS (gs.c) and the GPU renderer (gs_gpu.c) share: a
 * vertex as the GS registers give it, the drawing settings decoded from
 * them, and the calls between the two files.
 */
#ifndef GS_PRIV_H
#define GS_PRIV_H

#include <stdint.h>

typedef struct {
    int x, y;       /* window coordinates, 12.4 fixed point (offset applied) */
    uint32_t z;
    int f;          /* fog coefficient */
    int r, g, b, a;
    float s, t, q;
    int u, v;       /* 10.4 fixed point texel coordinates */
} Vertex;

/* Register fields decoded once per primitive. */
typedef struct {
    int ctx;
    int tme, fge, abe, fst, iip;
    int fpsm, zpsm;
    uint32_t fbp, fbw, zbp, fbmsk, fbmsk16;
    int zmsk;
    int scax0, scax1, scay0, scay1;
    int tfx, tcc;
    int fba, colclamp, pabe;
    int fr, fg, fb_; /* fog colour */

    /* Tests */
    int ate, atst, aref, afail;
    int date, datm;
    int zte, ztst;
    uint32_t zmax;

    /* Blending: (A - B) * C >> 7 + D */
    int sa, sb, sc, sd, fix;
    int blend_mix; /* (Cs - Cd) * As + Cd */

    /* Texture */
    int tpsm, cpsm, csa, bilinear;
    uint32_t tbp, tbw;
    int tw, th;
    int wms, wmt, minu, maxu, minv, maxv;
    int ta0, ta1, aem;

    int feedback; /* the texture overlaps the frame buffer */
    int key;      /* K_* flags of the specialised pixel loop to use */

    struct TexEntry *tex;  /* decoded texture (NULL: read GS memory) */
    const uint32_t *clut;  /* CLUT of indexed textures */
    uint64_t clut_hash;    /* its contents, for the GPU's texture cache */
} DrawState;

/* ---- gs.c, for the GPU renderer ---- */

/* Decodes the w x h texels at the top left of the texture of s into RGBA
 * (R in the low byte), with its CLUT and TEXA's alpha rules. */
void gs_decode_texture(const DrawState *s, uint32_t *dst, int w, int h);

/* The frame buffer the display shows and its size. */
void gs_display_area(uint32_t *fbp, uint32_t *fbw, int *psm, int *w, int *h);

/* ---- gs_gpu.c, for gs.c (only called while gs_gpu_on is set) ---- */

/* Draws a primitive: type as in PRIM (0 point ... 6 sprite) and its 1 to 3
 * vertices, with the settings of s. */
void gs_gpu_prim(const DrawState *s, int type, const Vertex *v);

/* Makes GS memory up to date for that rectangle (of format psm), before
 * the CPU reads or writes it: what the GPU drew there is copied back. */
void gs_gpu_sync(int psm, uint32_t bp, uint32_t bw, int x0, int y0, int x1, int y1);
void gs_gpu_sync_all(void);

/* GS memory was replaced as a whole (reset, frame dump): the GPU's copies
 * are forgotten. */
void gs_gpu_reset(void);

#endif
