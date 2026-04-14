/*
 * sceCd* shim. Phase 0 provides no-op definitions so the decomp links.
 * Phase 3 wires these to the ISO9660 reader so AFS / MOVIE reads work.
 */

#include "recvx_port.h"

/* We deliberately avoid pulling in libcdvd.h here: the decomp side gets
 * the real declarations, and this file only needs to provide symbols.
 * We match the real signatures in Phase 3 when we swap in real impls. */

int  sceCdInit(int mode)                  { (void)mode; return 1; }
int  sceCdSync(int mode)                  { (void)mode; return 0; }
int  sceCdPause(void)                     { return 1; }
int  sceCdDiskReady(int mode)             { (void)mode; return 2; /* ready */ }

int  sceCdSearchFile(void* fp, const char* name) {
    (void)fp; (void)name;
    RX_LOG("sceCd", "stub sceCdSearchFile(%s)", name ? name : "(null)");
    return 0;
}

int  sceCdRead(unsigned int lsn, unsigned int sectors, void* buf, void* mode) {
    (void)lsn; (void)sectors; (void)buf; (void)mode;
    return 0;
}

int  sceCdStInit(unsigned int bufmax, unsigned int bsize, void* bufaddr) {
    (void)bufmax; (void)bsize; (void)bufaddr; return 1;
}
int  sceCdStStart(unsigned int lsn, void* mode) { (void)lsn; (void)mode; return 1; }
int  sceCdStStop(void)                          { return 1; }
int  sceCdStRead(unsigned int sectors, void* buf, unsigned int mode, unsigned int* err) {
    (void)sectors; (void)buf; (void)mode; (void)err; return 0;
}
