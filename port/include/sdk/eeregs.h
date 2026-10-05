/*
 * PC port shim for the PS2 SDK <eeregs.h>.
 *
 * Hardware register addresses. They are kept so the code compiles, but any
 * access through them on PC is a bug: such code belongs to the platform layer
 * and must be replaced.
 */
#ifndef _eeregs_h_
#define _eeregs_h_

#define D0_CHCR ((volatile u_int *)0x10008000)
#define D1_CHCR ((volatile u_int *)0x10009000)
#define D1_MADR ((volatile u_int *)0x10009010)
#define D1_QWC  ((volatile u_int *)0x10009020)
#define D1_TADR ((volatile u_int *)0x10009030)
#define D2_CHCR ((volatile u_int *)0x1000a000)
#define D3_CHCR ((volatile u_int *)0x1000b000)
#define D3_MADR ((volatile u_int *)0x1000b010)
#define D3_QWC  ((volatile u_int *)0x1000b020)
#define D4_CHCR ((volatile u_int *)0x1000b400)
#define D4_MADR ((volatile u_int *)0x1000b410)
#define D4_QWC  ((volatile u_int *)0x1000b420)
#define D4_TADR ((volatile u_int *)0x1000b430)
#define D_STAT  ((volatile u_int *)0x1000e010)
#define D_PCR   ((volatile u_int *)0x1000e020)

#define GS_CSR  ((volatile u_long *)0x12001000)

#endif
