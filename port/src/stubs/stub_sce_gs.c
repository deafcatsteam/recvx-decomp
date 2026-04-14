/*
 * sceGs* shim — GS (Graphics Synthesizer) hardware calls.
 * All no-ops in phase 0. Phase 1 wires the subset the Ninja backend needs
 * into the OpenGL pipeline (mainly vsync + framebuffer flip semantics).
 */

#include "recvx_port.h"

void sceGsResetPath(void)       {}
void sceGsSyncPath(int a, int b){ (void)a; (void)b; }
void sceGsSyncV(int mode)       { (void)mode; }
int  sceGsSetHalfOffset(void* o){ (void)o; return 0; }
int  sceGsPutIMR(unsigned long long v){ (void)v; return 0; }
unsigned long long sceGsGetIMR(void)  { return 0; }
