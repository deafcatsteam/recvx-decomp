/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "PLA_MW.H"
 * with different case than the real file on disk (pla_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "pla_mw.h"
