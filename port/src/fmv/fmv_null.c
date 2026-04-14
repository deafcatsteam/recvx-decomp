/* FMV stub. Phase 4 swaps this for an FFmpeg-backed Sofdec replacement. */

#include "recvx_port.h"

struct recvx_fmv { int dummy; };

recvx_fmv_t* recvx_fmv_open(const char* iso_path) {
    RX_LOG("fmv", "TODO: open %s (FFmpeg backend not yet wired)", iso_path);
    return NULL;
}

void recvx_fmv_close(recvx_fmv_t* fmv) { (void)fmv; }
bool recvx_fmv_advance(recvx_fmv_t* fmv) { (void)fmv; return false; }
