/*
 * port_prefix.h - force-included before every translation unit of the PC port.
 *
 * The original game is built with Metrowerks CodeWarrior for the PS2 EE, where
 * `long` is 8 bytes and 128-bit integers exist. The PC port is built as a
 * 32-bit program (pointers stay 4 bytes so the game's data structures and the
 * pointers stored inside its data files keep their layout), so the wide types
 * are remapped here before any SDK or game header is seen.
 */
#ifndef PORT_PREFIX_H
#define PORT_PREFIX_H

#define PLATFORM_PC 1

/*
 * CodeWarrior's prefix file made the C library visible everywhere; several
 * game files call memcpy/atan2f/... without including anything. Without a
 * prototype, a float-returning function would be read back as an int.
 */
/* glibc's <sys/types.h> defines a 4-byte u_long; the EE's is 8 bytes. */
#define u_long port_libc_u_long
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#undef u_long

/*
 * `long` is 8 bytes for CodeWarrior on the EE but 4 bytes on 32-bit x86. The
 * game relies on the wide one (GS packets are built from 64-bit longs, struct
 * layouts follow it), and its 32-bit types already use `int`. So `long` is
 * widened for everything compiled after this point. System headers must all
 * be included above; port code spells 64-bit integers as int64_t/uint64_t.
 */
#define long long long

/* 128-bit "quadword": only ever used to move 16 bytes at once. */
typedef struct port_u128 {
    unsigned int w[4];
} __attribute__((aligned(16))) port_u128;

/* `__int128` is not available on 32-bit x86. */
#define __int128 port_u128

/* CRI's cri_xpts.h uses mode(TI) and `long` for 64-bit; predefine them. */
#define _TYPEDEF_Uint64
typedef uint64_t Uint64;
#define _TYPEDEF_Sint64
typedef int64_t Sint64;
#define _TYPEDEF_Uint128
typedef port_u128 Uint128;
#define _TYPEDEF_Sint128
typedef port_u128 Sint128;

/* CodeWarrior / EE-gcc specific keywords. */
#define __inline__ inline

#endif /* PORT_PREFIX_H */
