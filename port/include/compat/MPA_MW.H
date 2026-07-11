/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "MPA_MW.H"
 * with different case than the real file on disk (mpa_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "mpa_mw.h"
