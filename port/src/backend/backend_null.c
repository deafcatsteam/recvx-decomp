/* Headless backend — used when SDL2/GL aren't available. */

#include "recvx_port.h"

static int  null_init(const recvx_backend_config* cfg)  { (void)cfg; return 0; }
static void null_shutdown(void)                          {}
static void null_begin(void)                             {}
static void null_end(void)                               {}
static bool null_pump(void)                              { return true; }

static const recvx_backend g_null = {
    .name        = "null",
    .init        = null_init,
    .shutdown    = null_shutdown,
    .begin_frame = null_begin,
    .end_frame   = null_end,
    .pump_events = null_pump,
};

const recvx_backend* recvx_backend_null(void) { return &g_null; }
