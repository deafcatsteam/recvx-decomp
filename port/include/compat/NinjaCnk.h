/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "NinjaCnk.h"
 * with different case than the real file on disk (ninjacnk.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "ninjacnk.h"
