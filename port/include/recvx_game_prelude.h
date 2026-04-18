/*
 * recvx_game_prelude.h
 *
 * Force-included (/FI or -include) on every decomp source file compiled
 * for the PC port. Purpose: pull in MSVC's real CRT types FIRST, then
 * mask KATANA's drop-in replacement stdlib headers so they don't
 * redefine size_t / ptrdiff_t / FILE / etc. to the wrong widths.
 *
 * KATANA ships a SH-series (Hitachi) CRT under
 *   include/recvx-decomp-katana/KATANA/Include/
 * with typedefs like `typedef unsigned long size_t;` — wrong on MSVC
 * x64 where `long` is 4 bytes but `size_t` is 8.
 *
 * Each KATANA stdlib header uses the guard `_<NAME>_SHC`. We predefine
 * those guards here, so when game source later does `#include <ninja.h>`
 * which cascades into `#include <stddef.h>` / `<stdio.h>` / etc., KATANA's
 * copies short-circuit and the real MSVC types stay visible.
 *
 * NOT force-included on port stubs — those never touch KATANA headers.
 */

#ifndef RECVX_GAME_PRELUDE_H
#define RECVX_GAME_PRELUDE_H

/* --- 1. Real MSVC CRT comes first ------------------------------------ */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>

/* --- 2. Mask KATANA's drop-in CRT replacements ----------------------- */
#define _STDDEF_SHC
#define _STDIO_SHC
#define _STDLIB_SHC
#define _STRING_SHC
#define _MATH_SHC
#define _FLOAT_SHC
#define _LIMITS_SHC

/* mathf.h is KATANA-specific (fast float math). Leave its guard alone
 * so the real KATANA mathf.h content loads — we WANT those inline fns. */

/* --- 3. EE basic types MWCC would provide ---------------------------- */
/* KATANA ninja.h and friends reference Sint32/Uint32/etc. On PS2 these
 * are `int` (MWCC `long` is 8 bytes so the decomp author overrides them;
 * see include/ps2/veronica/prog/override_katana.h). Match that here. */
#ifndef _TYPEDEF_Sint32
#define _TYPEDEF_Sint32
typedef signed int   Sint32;
#endif
#ifndef _TYPEDEF_Uint32
#define _TYPEDEF_Uint32
typedef unsigned int Uint32;
#endif

/* --- 4. MWCC-only attributes the decomp uses ------------------------- */
#if defined(_MSC_VER)
# define __attribute__(x) /* swallow GCC attribute syntax */
# define __restrict__ __restrict
  /* MSVC x64 doesn't support inline asm at all. The decomp has a few
   * `asm("nop")` placeholders that exist only for instruction-scheduling
   * parity with MWCC output — safe to drop on PC. */
# define asm(x) ((void)0)
#endif

/* --- 5. Port build flag --------------------------------------------- */
#define RECVX_PC_PORT 1

/* --- 6. Opaque types referenced by decomp headers without forward decl --
 * adxwrap.h uses ADX_FS* / ADX_TALK* but never includes the CRI headers
 * that define them (MWCC was apparently lenient). ps2_MemoryCard..h uses
 * sceMcTblGetDir* in a function parameter; libmc.h from PS2 SDK defines
 * the real struct but isn't on this header's include chain. Forward-
 * declaring here as empty structs is enough for pointer-only uses. */
struct ADX_FS;
typedef struct ADX_FS         ADX_FS;
struct ADX_TALK;
typedef struct ADX_TALK       ADX_TALK;
struct sceMcTblGetDir;
typedef struct sceMcTblGetDir sceMcTblGetDir;

#endif /* RECVX_GAME_PRELUDE_H */
