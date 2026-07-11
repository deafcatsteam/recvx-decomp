/* Case-sensitivity shim: an upstream KATANA/MWCC header includes "UART.h"
 * with different case than the real file on disk (uart.h). Works on
 * MSVC's case-insensitive filesystem, not on Linux. Forwards through. */
#include "uart.h"
