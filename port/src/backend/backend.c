/* Backend dispatch helpers. */
#include "recvx_port.h"

/* Global "currently live" backend pointer. Set once by main_pc after
 * backend->init succeeds, read by adx_player / future mixers that need
 * to push PCM without threading the backend handle through every call. */
static const recvx_backend* g_current;

void recvx_backend_set_current(const recvx_backend* b) { g_current = b; }
const recvx_backend* recvx_backend_current(void)       { return g_current; }
