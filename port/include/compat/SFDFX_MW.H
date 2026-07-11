/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "SFDFX_MW.H"
 * with different case than the real file on disk (sfdfx_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "sfdfx_mw.h"
