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
