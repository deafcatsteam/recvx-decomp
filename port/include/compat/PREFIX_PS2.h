/* compat: MSVC-safe replacement for MWCC PREFIX_PS2.h */
#ifndef __PREFIX_PS2__
#define __PREFIX_PS2__
#define mips 1
#define _mips 1
#define __mips__ 1
#define MIPSEL 1
#define _MIPSEL 1
#define __MIPSEL__ 1
#define R5900 1
#define _R5900 1
#define __R5900__ 1
#define __ee__ 1
#define __mips_single_float 1
#ifndef LANGUAGE_C
# define LANGUAGE_C 1
# define _LANGUAGE_C 1
# define __LANGUAGE_C 1
#endif
/* long128 / u_long128 are never instantiated in game code, but headers may
 * mention them. Provide opaque placeholders. */
typedef struct { unsigned long long lo, hi; } long128;
typedef struct { unsigned long long lo, hi; } u_long128;
#endif
