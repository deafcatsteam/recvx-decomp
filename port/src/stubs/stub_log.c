#include "recvx_port.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* "cnk" fires per-model-per-frame (hundreds of lines/frame under normal
 * room rendering); "nj"/"tim2"/"ps2" fire several lines per texture during
 * room texture loads (hundreds of textures per room). The fflush below
 * turns all of these into a serious throughput killer under headless/
 * software-GL test runs. Off by default, opt back in with RECVX_LOG_CNK=1
 * (or the matching RECVX_LOG_NJ/RECVX_LOG_TIM2/RECVX_LOG_PS2) when actually
 * debugging that subsystem. */
static int tag_log_enabled(const char* env_name) {
    const char* v = getenv(env_name);
    return (v && v[0] != '0' && v[0] != '\0') ? 1 : 0;
}

static int noisy_tag_enabled(const char* tag) {
    if (strcmp(tag, "cnk") == 0)  return tag_log_enabled("RECVX_LOG_CNK");
    if (strcmp(tag, "nj") == 0)   return tag_log_enabled("RECVX_LOG_NJ");
    if (strcmp(tag, "tim2") == 0) return tag_log_enabled("RECVX_LOG_TIM2");
    if (strcmp(tag, "ps2") == 0)  return tag_log_enabled("RECVX_LOG_PS2");
    return 1;
}

void recvx_log(const char* tag, const char* fmt, ...) {
    if (tag && !noisy_tag_enabled(tag)) return;
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
