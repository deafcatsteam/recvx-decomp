/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "TM_MW.H"
 * with different case than the real file on disk (tm_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "tm_mw.h"
