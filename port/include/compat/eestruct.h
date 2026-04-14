/* compat: EE register struct shadows. Layout-opaque placeholders —
 * the game uses these in struct definitions only; we never talk to GS. */
#ifndef RECVX_COMPAT_EESTRUCT_H
#define RECVX_COMPAT_EESTRUCT_H
#include <eetypes.h>

typedef struct { unsigned long long _raw; } sceGsTex0;
typedef struct { unsigned long long _raw; } sceGsTex1;
typedef struct { unsigned long long _raw; } sceGsTex2;
typedef struct { unsigned long long _raw; } sceGsTexa;
typedef struct { unsigned long long _raw; } sceGsTest;
typedef struct { int _pad; }               sceGsDBuffDc;
typedef struct { int _pad; }               sceGsZBuffDc;
typedef struct { int _pad; }               sceGsDrawEnv1;
typedef struct { int _pad; }               sceGsDrawEnv2;

#endif
