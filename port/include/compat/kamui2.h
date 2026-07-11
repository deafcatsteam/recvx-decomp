/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "kamui2.h"
 * with different case than the real file on disk (KAMUI2.H). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "KAMUI2.H"
