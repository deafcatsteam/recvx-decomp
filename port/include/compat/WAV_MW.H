/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "WAV_MW.H"
 * with different case than the real file on disk (wav_mw.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "wav_mw.h"
