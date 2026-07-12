/*
 * Sega Ninja (nj*) shim. Phase 0 provides the tiny subset that njUserInit()
 * in src/ps2/veronica/prog/main.c touches, all as no-ops, so the decomp
 * links. Phase 1 plugs these into the GL backend.
 *
 * The real ninja.h from include/recvx-decomp-katana declares the signatures
 * we're implementing here; keep them in sync when swapping in real impls.
 */

#include "recvx_port.h"

void njSetBorderColor(unsigned int c)                 { (void)c; }
void njInitTextureBuffer(signed char* p, int size)    { (void)p; (void)size; }
void njSetVertexBuffer(unsigned int* p, int size)     { (void)p; (void)size; }
void njInitVertexBuffer(int a,int b,int c,int d,int e){ (void)a;(void)b;(void)c;(void)d;(void)e; }
void njInit3D(void* buf, int size)                    { (void)buf; (void)size; }
/* njInitMatrix now lives in port/src/ninja_3d.c with real impl. */
void njInitPrint(void* p, int a, int b)               { (void)p; (void)a; (void)b; }
void njPolygonCullingMode(int mode)                   { (void)mode; }
void njTextureShadingMode(int mode)                   { (void)mode; }
void njInitView(void* v)                              { (void)v; }
void njSetView(void* v)                               { (void)v; }
/* njGetMatrix now lives in port/src/ninja_3d.c with real impl. */
/* njInitTexture moved to port/src/game_texture_stubs.c — real impl sets
 * Ps2_tex_info so bhCopyMainmem2Texmem (ps2_texture.c, called every
 * movie-to-room texture handoff) doesn't dereference a NULL pool head. */
void njExitTexture(void)                              {}
void njExitPrint(void)                                {}
void njSetPaletteMode(int m)                          { (void)m; }
void njPrintSize(int s)                               { (void)s; }
void njSetAspect(float x, float y)                    { (void)x; (void)y; }
void njClipZ(float near, float far)                   { (void)near; (void)far; }
void njSetBackColor(unsigned int a,unsigned int b,unsigned int c){ (void)a;(void)b;(void)c; }
int  njCalcVtxBuffer(int a,int b,int c)               { (void)a;(void)b;(void)c; return a+b+c; }

/* Matrix stack + transformation funcs (njPushMatrix, njPopMatrix,
 * njSetMatrix, njMultiMatrix, njClearMatrix, njUnitMatrix,
 * njTransposeMatrix, njTranslate, njRotateX/Y/Z, njScale, njScalor,
 * njCalcPoint, njGetMatrix, njCalcPoints, njGetTranslation,
 * njSinCos, njSin, njCos, njInitMatrix) moved to ninja_3d.c with
 * real C implementations as Phase 1 of the 3D renderer. */
void njDrawPolygon3D(void* p, int n, int t)           { (void)p;(void)n;(void)t; }

/* Chunked-model draw + lighting (njCnk*). Real impls walk a chunk
 * tree and send polys to the PS2 GS / our GL3D backend. Stubbed for
 * now — itemview's 3D inventory item won't be visible until we
 * implement these. */
/* njCnkEasyMultiDrawModel and njCnkEasyMultiDrawObjectI moved to
 * port/src/ninja_cnk.c (Phase 3 of Path B) — real chunk walkers that
 * route triangles through recvx_gfx_draw_tri3d. */
void njCnkSetEasyMultiAmbient(unsigned int c)         { (void)c; }
void njCnkSetEasyMultiLight(int n, void* v)           { (void)n;(void)v; }
void njCnkSetEasyMultiLightColor(int n, unsigned int c){ (void)n;(void)c; }
void njCnkSetEasyMultiLightMatrices(int n, void* m)   { (void)n;(void)m; }
void njCnkSetEasyMultiLightPoint(int n, void* p)      { (void)n;(void)p; }
void njCnkSetEasyMultiLightRange(int n, float r)      { (void)n;(void)r; }

/* npGetWHDSize is a model bounding-box helper — defined for real in
 * playpch.c which we haven't compiled. No-op for now.
 * MdlAction00/01/02/MdlDirChk now provided by itemview.c (compiled). */
void npGetWHDSize(void* mlw, float* whd)              { (void)mlw;(void)whd; }

/* bhMlbBinRealize now provided by real binfunc.c (Phase 1 of 3D).
 * bhKeepObjWork moved to ninja_3d.c (needs ML_WORK type which this
 * stub library doesn't include). */

/* ----------------------------------------------------------------------
 * Ninja 3D state stubs needed by sub1.c. Real impls bind GS register
 * state on PS2; on PC a real GL3D backend would translate these into
 * glEnable/glBlendFunc/glFogf calls. No-op for now since sub1's 3D
 * paths (item-view rotating model) aren't reachable without matching
 * game flow yet.
 * ---------------------------------------------------------------------- */
void njControl3D(int mode)                            { (void)mode; }
/* njDrawSprite2D moved to port/src/game_texture_stubs.c — needs real
 * NJS_SPRITE / NJS_TEXANIM struct layout from ninjastr.h, which the
 * stubs target intentionally doesn't include. */
void njSetConstantAttr(unsigned int a)                { (void)a; }

/* Pulse00/PulseHealAnim/PulsePoisonHealAnim (sub1.c) draw the inventory's
 * heart-rate / condition waveform as a series of textured 4-vertex
 * polygons. Per-vertex `bcol` is what gives the waveform its
 * characteristic fading-edge look (verts 0..1 = transparent black,
 * verts 2..3 = saturated green/yellow/red), so we route through the
 * vertex-colored polygon path and ignore the texture sample point.
 * Visually identical for purposes of the waveform line.
 *
 * NJS_TEXTUREH_VTX layout: { float x,y,z, u,v; uint32 bcol, ocol; }
 * Call sites use TL-BL-TR-BR zigzag order which is what
 * recvx_gfx_draw_polygon's GL_TRIANGLE_STRIP wants. */
struct _njs_textureh_vtx {
    float    x, y, z;
    float    u, v;
    unsigned int bcol;
    unsigned int ocol;
};

void njDrawTextureH(void* polygon, int count, int tex, int flag) {
    (void)tex; (void)flag;
    if (!polygon || count < 3) return;
    if (count > 32) count = 32;
    const struct _njs_textureh_vtx* p =
        (const struct _njs_textureh_vtx*)polygon;
    recvx_gfx_vtx v[32];
    for (int i = 0; i < count; ++i) {
        v[i].x = p[i].x;
        v[i].y = p[i].y;
        v[i].z = p[i].z;
        v[i].color = p[i].bcol;
    }
    recvx_gfx_draw_polygon(v, count, 1);
}

/* njDrawLine2D draws a line strip in 2D (NJS_POINT2COL with N verts).
 * BorderLineSet (sub1.c:6656) uses it for inventory border decorations.
 * Stub as no-op so build links; lines won't render visually. Same path
 * as njDrawPolygon2D could handle this if we wanted but lines aren't
 * critical to inventory layout. */
void njDrawLine2D(void* p2c, int n, float pri, unsigned int attr) {
    (void)p2c; (void)n; (void)pri; (void)attr;
}

/* njColorBlendingMode(channel, mode) sets per-channel blending factors
 * on PS2 GS (channel 0=src, 1=dst; mode 6=ZERO, 8=PASS, etc). For our
 * GL backend we already use a single GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA
 * mode which approximates what the inventory wants -- this is a no-op
 * shim to satisfy linkage from message.c bhDispFont. */
void njColorBlendingMode(int channel, int mode) { (void)channel; (void)mode; }

/* Latched per-sprite tint color set by sub1.c:3217 (and other 2D draw
 * sites). NJS_ARGB layout from ninjastr.h is `{float a,r,g,b;}` in 0..1
 * range. We pack to BGRA byte order matching NJS_COLOR.color and
 * recvx_gfx_draw_quad's color arg. njDrawSprite2D reads
 * recvx_get_constant_material_argb() instead of hardcoding 0xFFFFFFFF. */
static unsigned int g_constant_material_argb = 0xFFFFFFFFu;

unsigned int recvx_get_constant_material_argb(void) {
    return g_constant_material_argb;
}

void njSetConstantMaterial(void* m) {
    if (!m) { g_constant_material_argb = 0xFFFFFFFFu; return; }
    const float* f = (const float*)m;
    /* Clamp to [0,1] then quantize to bytes; NJS_ARGB ordering = a,r,g,b. */
    float a = f[0] < 0.0f ? 0.0f : (f[0] > 1.0f ? 1.0f : f[0]);
    float r = f[1] < 0.0f ? 0.0f : (f[1] > 1.0f ? 1.0f : f[1]);
    float g = f[2] < 0.0f ? 0.0f : (f[2] > 1.0f ? 1.0f : f[2]);
    float b = f[3] < 0.0f ? 0.0f : (f[3] > 1.0f ? 1.0f : f[3]);
    /* recvx_gfx_draw_quad expects ARGB byte order in the uint32_t arg
     * (top byte = alpha). Pack accordingly. */
    g_constant_material_argb =
        ((unsigned int)(a * 255.0f + 0.5f) << 24) |
        ((unsigned int)(r * 255.0f + 0.5f) << 16) |
        ((unsigned int)(g * 255.0f + 0.5f) <<  8) |
        ((unsigned int)(b * 255.0f + 0.5f));
}
void njSetFogColor(unsigned int c)                    { (void)c; }
void njSetPaletteBankNum(int n)                       { (void)n; }
void njUserClipping(void* p)                          { (void)p; }

/* sb* — SEGA basic (display mode / vsync) */
int  sbInitSystem(int mode, int frame, int count)     { (void)mode;(void)frame;(void)count; return 0; }
void sbExitSystem(void)                               {}

/* sy* — system helpers */
int  syCblCheck(void)                                 { return 0; /* 60Hz, 4:3 */ }
void* syMalloc(unsigned int sz)                       { extern void* malloc(size_t); return malloc(sz); }
void syBtExit(void)                                   {}
