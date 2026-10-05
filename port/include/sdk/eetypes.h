/* PC port shim for the PS2 SDK <eetypes.h>. */
#ifndef _eetypes_h_
#define _eetypes_h_

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
/* `long` is 64-bit on the EE. */
typedef unsigned long long u_long;
typedef port_u128 u_long128;
typedef port_u128 long128;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
