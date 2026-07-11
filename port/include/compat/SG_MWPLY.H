/* Case-sensitivity shim: sg_mw.h includes "SG_MWPLY.H" (uppercase), which
 * resolves fine on MSVC's case-insensitive filesystem but not on Linux,
 * where the real header ships lowercase as sg_mwply.h. This forwards to
 * the real KATANA header (found via the include path, same mechanism as
 * the other compat/ shims). */
#include "sg_mwply.h"
