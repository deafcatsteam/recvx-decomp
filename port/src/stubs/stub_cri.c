/*
 * CRI middleware shim. Phase 0 only satisfies the handful of init symbols
 * the boot path references. The decomp already contains decompiled libadxe
 * source — in later phases we compile that in and back it with our
 * ISO-reader-backed sceCd* shim so AFS reads Just Work.
 */

#include "recvx_port.h"

void InitFirstSofdec(void)             {}
void PS2_jikken(void)                  {}
int  bhCalcVtxBuffer(int a,int b,int c){ (void)a;(void)b;(void)c; return 0; }
void bhCheckSoftReset(void)            {}
