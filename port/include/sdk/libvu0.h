/* PC port shim for the PS2 SDK <libvu0.h> (VU0 macro-mode vector math). */
#ifndef _libvu0_h_
#define _libvu0_h_

#include <eetypes.h>

typedef int sceVu0IVECTOR[4] __attribute__((aligned(16)));
typedef float sceVu0FVECTOR[4] __attribute__((aligned(16)));
typedef float sceVu0FMATRIX[4][4] __attribute__((aligned(16)));
typedef int sceVu0IMATRIX[4][4] __attribute__((aligned(16)));

void sceVu0AddVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);
void sceVu0SubVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);
void sceVu0ScaleVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s);
float sceVu0InnerProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
void sceVu0OuterProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);
void sceVu0Normalize(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
void sceVu0UnitMatrix(sceVu0FMATRIX m0);
void sceVu0FTOI0Vector(sceVu0IVECTOR v0, sceVu0FVECTOR v1);
void sceVu0FTOI4Vector(sceVu0IVECTOR v0, sceVu0FVECTOR v1);
void sceVu0ITOF4Vector(sceVu0FVECTOR v0, sceVu0IVECTOR v1);
void sceVu0ITOF12Vector(sceVu0FVECTOR v0, sceVu0IVECTOR v1);
void sceVpu0Reset(void);

#endif
