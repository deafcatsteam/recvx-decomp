/*
 * PC port shim for the PS2 SDK <eeregs.h>.
 *
 * The PS2 hardware registers live at fixed addresses (0x1000xxxx for the EE
 * peripherals, 0x1200xxxx for the GS). On PC they are redirected into a plain
 * memory block, port_hwregs, so code that still touches them reads zeros and
 * writes nowhere instead of crashing. Such code belongs to the platform layer
 * and is meant to be replaced.
 */
#ifndef _eeregs_h_
#define _eeregs_h_

#include <eetypes.h>

extern unsigned char port_hwregs[0x20000];

#define PORT_HWREG(type, addr) \
    ((volatile type *)(port_hwregs + ((addr) & 0xffff) + (((addr) >> 25) & 1) * 0x10000))

#define T0_COUNT PORT_HWREG(u_int, 0x10000000)
#define T0_MODE  PORT_HWREG(u_int, 0x10000010)
#define T0_COMP  PORT_HWREG(u_int, 0x10000020)
#define T1_COUNT PORT_HWREG(u_int, 0x10000800)
#define T1_MODE  PORT_HWREG(u_int, 0x10000810)

#define D0_CHCR PORT_HWREG(u_int, 0x10008000)
#define D1_CHCR PORT_HWREG(u_int, 0x10009000)
#define D1_MADR PORT_HWREG(u_int, 0x10009010)
#define D1_QWC  PORT_HWREG(u_int, 0x10009020)
#define D1_TADR PORT_HWREG(u_int, 0x10009030)
#define D2_CHCR PORT_HWREG(u_int, 0x1000a000)
#define D2_MADR PORT_HWREG(u_int, 0x1000a010)
#define D2_QWC  PORT_HWREG(u_int, 0x1000a020)
#define D2_TADR PORT_HWREG(u_int, 0x1000a030)
#define D3_CHCR PORT_HWREG(u_int, 0x1000b000)
#define D3_MADR PORT_HWREG(u_int, 0x1000b010)
#define D3_QWC  PORT_HWREG(u_int, 0x1000b020)
#define D4_CHCR PORT_HWREG(u_int, 0x1000b400)
#define D4_MADR PORT_HWREG(u_int, 0x1000b410)
#define D4_QWC  PORT_HWREG(u_int, 0x1000b420)
#define D4_TADR PORT_HWREG(u_int, 0x1000b430)
#define D_CTRL  PORT_HWREG(u_int, 0x1000e000)
#define D_STAT  PORT_HWREG(u_int, 0x1000e010)
#define D_PCR   PORT_HWREG(u_int, 0x1000e020)

#define IPU_CMD  PORT_HWREG(u_long, 0x10002000)
#define IPU_CTRL PORT_HWREG(u_int, 0x10002010)

#define GS_CSR  PORT_HWREG(u_long, 0x12001000)

#endif
