#ifndef _BINFUNC_H_
#define _BINFUNC_H_

#include "types.h"

#ifdef RECVX_PC_PORT
#include <stdint.h>
/* dat_off carries a base pointer for relocation. On PS2 (32-bit) the
 * original `unsigned int` declaration is fine; on x64 it would
 * truncate the upper 32 bits of every pointer it carries. Widen to
 * uintptr_t under the PC port so the relocated pointers survive. */
#define BH_DATOFF_T  uintptr_t
#else
#define BH_DATOFF_T  unsigned int
#endif


int bhMlbBinRealize(void* bin_datP, ML_WORK* mlwP);
int bhBscBinRealize(NJS_MODEL* mdlP, BH_DATOFF_T dat_off);
int bhCnkBinRealize(NJS_CNK_MODEL* mdlP, BH_DATOFF_T dat_off);
int bhMnbBinRealize(void* bin_datP, MN_WORK* mnwP);

#endif
