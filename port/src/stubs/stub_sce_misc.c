/*
 * Remaining sce* shims: SIF, DMA, IPU, memcard, scf. Phase 0 no-ops.
 *
 * Many of these are called exactly once during boot from CRI / Ninja init
 * paths, so a no-op return is enough to get past them.
 */

#include "recvx_port.h"

/* SIF (sub-CPU interface) */
int  sceSifInitRpc(int mode) { (void)mode; return 0; }
int  sceSifExitRpc(void)      { return 0; }

/* DMA channels */
void sceDmaSend(int ch, void* addr)           { (void)ch; (void)addr; }
int  sceDmaSync(int ch, int mode, int to)     { (void)ch; (void)mode; (void)to; return 0; }

/* IPU (image processor, MPEG decode on PS2) — we replace the movie path
 * wholesale in phase 4 so these never actually get called for real FMVs. */
int  sceIpuCtrl(unsigned int cmd)             { (void)cmd; return 0; }
int  sceIpuWait(int ctrl)                     { (void)ctrl; return 0; }

/* Memory card */
int  sceMcInit(void)                          { return 0; }
int  sceMcGetInfo(int a,int b,int* c,int* d,int* e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}

/* System config */
int  sceScfGetLanguage(void)                  { return 1; /* English */ }
int  sceScfGetAspect(void)                    { return 0; /* 4:3 */ }
