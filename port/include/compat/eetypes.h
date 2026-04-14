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

/* BSD-style short names used throughout the decomp. */
typedef unsigned char      u_char;
typedef unsigned short     u_short;
typedef unsigned int       u_int;
typedef unsigned long      u_long;
typedef signed char        s_char;
typedef short              s_short;
typedef int                s_int;
typedef long               s_long;
typedef unsigned long long u_longlong;

typedef int8_t   int8;
typedef uint8_t  uint8;
typedef int16_t  int16;
typedef uint16_t uint16;
typedef int32_t  int32;
typedef uint32_t uint32;
typedef int64_t  int64;
typedef uint64_t uint64;
typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;

/* PS2 EE has __int128 (quadword). MSVC doesn't; use a 16-byte placeholder
 * so struct layouts parse. Runtime semantics don't matter — game code
 * only uses these as opaque storage for VU0 quadwords. */
#ifndef __int128
typedef struct { unsigned long long _hi, _lo; } __int128_t_compat;
#define __int128 __int128_t_compat
#endif
typedef __int128  s128;
typedef __int128  u128;

#endif
