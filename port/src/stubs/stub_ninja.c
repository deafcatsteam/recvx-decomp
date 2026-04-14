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

/* sb* — SEGA basic (display mode / vsync) */
int  sbInitSystem(int mode, int frame, int count)     { (void)mode;(void)frame;(void)count; return 0; }
void sbExitSystem(void)                               {}

/* sy* — system helpers */
int  syCblCheck(void)                                 { return 0; /* 60Hz, 4:3 */ }
void* syMalloc(unsigned int sz)                       { extern void* malloc(size_t); return malloc(sz); }
void syBtExit(void)                                   {}
