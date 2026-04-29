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
void njInitMatrix(void* buf, int size, int z)         { (void)buf; (void)size; (void)z; }
void njInitPrint(void* p, int a, int b)               { (void)p; (void)a; (void)b; }
void njPolygonCullingMode(int mode)                   { (void)mode; }
void njTextureShadingMode(int mode)                   { (void)mode; }
void njInitView(void* v)                              { (void)v; }
void njSetView(void* v)                               { (void)v; }
void njGetMatrix(void* m)                             { (void)m; }
void njInitTexture(void* buf, int count)              { (void)buf; (void)count; }
void njExitTexture(void)                              {}
void njExitPrint(void)                                {}
void njSetPaletteMode(int m)                          { (void)m; }
void njPrintSize(int s)                               { (void)s; }
void njSetAspect(float x, float y)                    { (void)x; (void)y; }
void njClipZ(float near, float far)                   { (void)near; (void)far; }
void njSetBackColor(unsigned int a,unsigned int b,unsigned int c){ (void)a;(void)b;(void)c; }
int  njCalcVtxBuffer(int a,int b,int c)               { (void)a;(void)b;(void)c; return a+b+c; }

/* ----------------------------------------------------------------------
 * Matrix stack + transform stubs needed by itemview.c.
 * Real impls would track a NJS_MATRIX stack and apply rotation/scaling
 * for the in-game 3D item-view model. For headless / port-without-GL3D
 * builds these are no-ops; the model rendering would be invisible
 * anyway since njCnkEasyMultiDrawModel below is also stubbed.
 * ---------------------------------------------------------------------- */
void njPushMatrix(void* m)                            { (void)m; }
void njPopMatrix(int n)                               { (void)n; }
void njSetMatrix(void* m)                             { (void)m; }
void njMultiMatrix(void* m)                           { (void)m; }
void njClearMatrix(void* m)                           { (void)m; }
void njUnitMatrix(void* m)                            { (void)m; }
void njTransposeMatrix(void* dst, void* src)          { (void)dst;(void)src; }
void njTranslate(void* m, float x, float y, float z)  { (void)m;(void)x;(void)y;(void)z; }
void njRotateX(void* m, int a)                        { (void)m;(void)a; }
void njRotateY(void* m, int a)                        { (void)m;(void)a; }
void njRotateZ(void* m, int a)                        { (void)m;(void)a; }
void njScale  (void* m, float x, float y, float z)    { (void)m;(void)x;(void)y;(void)z; }
void njScalor (float* dst, float* src, float s)       { (void)dst;(void)src;(void)s; }
void njCalcPoint(void* m, void* in, void* out)        { (void)m;(void)in;(void)out; }
void njDrawPolygon3D(void* p, int n, int t)           { (void)p;(void)n;(void)t; }

/* Chunked-model draw + lighting (njCnk*). Real impls walk a chunk
 * tree and send polys to the PS2 GS / our GL3D backend. Stubbed for
 * now — itemview's 3D inventory item won't be visible until we
 * implement these. */
void njCnkEasyMultiDrawModel(void* p)                 { (void)p; }
void njCnkEasyMultiDrawObjectI(void* p, int idx)      { (void)p;(void)idx; }
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

/* Model-binary / object-work helpers — bring in real impls when
 * binfunc.c or playpch.c gets compiled. For now no-ops. */
int  bhMlbBinRealize(void* dat, void* mlw)            { (void)dat;(void)mlw; return 0; }
void bhKeepObjWork(void* obj, void* dst)              { (void)obj;(void)dst; }

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
void njSetConstantMaterial(void* m)                   { (void)m; }
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
