#include "recvx_port.h"

#include <stdio.h>
#include <stdarg.h>

void recvx_log(const char* tag, const char* fmt, ...) {
    fprintf(stdout, "[%s] ", tag ? tag : "?");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

/* PS2 SDK scePrintf — used by sub1.c "UNIMPLEMENTED!" diagnostics.
 * Forward to recvx_log so it lands in the runtime.log alongside our
 * native [tag] lines. */
void scePrintf(const char* fmt, ...) {
    fprintf(stdout, "[sce] ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}
