/*
 * scePad* shim. Phase 0: all "disconnected" so the game boots past pad init.
 * Phase 2: SDL2 keyboard + gamepad fill in the button state.
 */

#include "recvx_port.h"

int scePadInit(int mode)                             { (void)mode; return 1; }
int scePadEnd(void)                                  { return 1; }
int scePadPortOpen(int port, int slot, void* addr)   { (void)port; (void)slot; (void)addr; return 1; }
int scePadPortClose(int port, int slot)              { (void)port; (void)slot; return 1; }
int scePadRead(int port, int slot, unsigned char* data) {
    (void)port; (void)slot;
    if (data) {
        /* 32 bytes of "no buttons pressed" */
        for (int i = 0; i < 32; ++i) data[i] = 0;
        data[2] = 0xFF; data[3] = 0xFF; /* PS2 idle mask */
    }
    return 32;
}
int scePadGetState(int port, int slot)               { (void)port; (void)slot; return 6; /* stable */ }
int scePadGetReqState(int port, int slot)            { (void)port; (void)slot; return 0; }
int scePadInfoMode(int port, int slot, int term, int off) { (void)port;(void)slot;(void)term;(void)off; return 0; }
int scePadSetMainMode(int port, int slot, int off, int lock) { (void)port;(void)slot;(void)off;(void)lock; return 1; }
int scePadSetActDirect(int port, int slot, const unsigned char* data) { (void)port;(void)slot;(void)data; return 1; }
