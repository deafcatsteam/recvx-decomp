/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "SFD_MW.H"
 * with different case than the real file on disk (sfd_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "sfd_mw.h"
