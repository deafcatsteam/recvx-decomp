/* compat: PS2 EE basic types. */
#ifndef RECVX_COMPAT_EETYPES_H
#define RECVX_COMPAT_EETYPES_H

#include <stdint.h>

#ifndef _TYPEDEF_Sint8
#define _TYPEDEF_Sint8
typedef int8_t   Sint8;
#endif
#ifndef _TYPEDEF_Uint8
#define _TYPEDEF_Uint8
typedef uint8_t  Uint8;
#endif
#ifndef _TYPEDEF_Sint16
#define _TYPEDEF_Sint16
typedef int16_t  Sint16;
#endif
#ifndef _TYPEDEF_Uint16
#define _TYPEDEF_Uint16
typedef uint16_t Uint16;
#endif
#ifndef _TYPEDEF_Sint32
#define _TYPEDEF_Sint32
typedef int32_t  Sint32;
#endif
#ifndef _TYPEDEF_Uint32
#define _TYPEDEF_Uint32
typedef uint32_t Uint32;
#endif
#ifndef _TYPEDEF_Sint64
#define _TYPEDEF_Sint64
typedef int64_t  Sint64;
#endif
#ifndef _TYPEDEF_Uint64
#define _TYPEDEF_Uint64
typedef uint64_t Uint64;
#endif

typedef float    Float32;
typedef double   Float64;

#endif
