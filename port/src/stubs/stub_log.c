#include "recvx_port.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* "cnk" fires per-model-per-frame (hundreds of lines/frame under normal
 * room rendering); the fflush below turns that into a serious frame-rate
 * killer under headless/software-GL test runs. Off by default, opt back
 * in with RECVX_LOG_CNK=1 when actually debugging ninja_cnk draw calls. */
static int cnk_log_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("RECVX_LOG_CNK");
        cached = (v && v[0] != '0' && v[0] != '\0') ? 1 : 0;
    }
    return cached;
}

void recvx_log(const char* tag, const char* fmt, ...) {
    if (tag && strcmp(tag, "cnk") == 0 && !cnk_log_enabled()) return;
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
