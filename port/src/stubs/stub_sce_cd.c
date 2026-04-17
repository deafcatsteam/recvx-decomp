/*
 * sceCd* shim backed by the ISO9660 reader.
 *
 * The decomp's CD surface has two call patterns:
 *   1. sceCdSearchFile + sceCdRead (whole-file into RAM).
 *      See ps2_sg_gd.c.
 *   2. sceCdStInit + sceCdStStart + sceCdStRead (streaming, used
 *      by FMV and CRI Sofdec). See ps2_MovieFunc.c, ps2_sfd_mw.c.
 *
 * Both resolve filenames against the global recvx_iso and do synchronous
 * reads on the PC side — `Sync` always returns idle. Actual async is
 * pointless when we're reading a local file backed by the OS cache.
 */

#include "recvx_port.h"

#include <stdio.h>
#include <string.h>

/* Mirror of the struct the game's types.h/libcdvd.h presents. Kept local
 * so this file doesn't need the game include path. Layout only matters
 * for lsn/size — everything else is opaque scratch the game fills in. */
typedef struct {
    unsigned int lsn;
    unsigned int size;
    char         name[16];
    unsigned char date[8];
    unsigned int fp;
} sceCdlFILE;

/* --- search / whole-file read ------------------------------------------ */

int  sceCdInit(int mode)      { (void)mode; return 1; }
int  sceCdSync(int mode)      { (void)mode; return 0; }  /* always idle */
int  sceCdPause(void)         { return 1; }
int  sceCdDiskReady(int mode) { (void)mode; return 2; }  /* ready */

int sceCdSearchFile(sceCdlFILE* fp, const char* name) {
    if (!fp || !name) return 0;
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) {
        RX_LOG("sceCd", "SearchFile(%s) — no global ISO", name);
        return 0;
    }
    uint32_t lba = 0, sz = 0;
    if (recvx_iso_find(iso, name, &lba, &sz) != 0) {
        RX_LOG("sceCd", "SearchFile(%s) — not found", name);
        return 0;
    }
    memset(fp, 0, sizeof(*fp));
    fp->lsn  = lba;
    fp->size = sz;
    /* Copy the leaf component into fp->name for completeness. */
    const char* leaf = name;
    for (const char* p = name; *p; ++p) {
        if (*p == '\\' || *p == '/') leaf = p + 1;
    }
    strncpy(fp->name, leaf, sizeof(fp->name) - 1);
    RX_LOG("sceCd", "SearchFile(%s) -> lsn=%u size=%u", name, lba, sz);
    return 1;
}

int sceCdRead(unsigned int lsn, unsigned int sectors, void* buf, void* mode) {
    (void)mode;
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) return 0;
    if (recvx_iso_read_sectors(iso, lsn, sectors, buf) != 0) return 0;
    return 1;
}

/* --- streaming ---------------------------------------------------------- */

static struct {
    int          active;
    unsigned int cursor_lsn;   /* next sector to read */
    unsigned int bufmax;
    unsigned int bsize;
} g_st;

int sceCdStInit(unsigned int bufmax, unsigned int bsize, void* bufaddr) {
    (void)bufaddr;
    g_st.bufmax = bufmax;
    g_st.bsize  = bsize;
    g_st.active = 0;
    g_st.cursor_lsn = 0;
    return 1;
}

int sceCdStStart(unsigned int lsn, void* mode) {
    (void)mode;
    g_st.cursor_lsn = lsn;
    g_st.active     = 1;
    return 1;
}

int sceCdStStop(void) {
    g_st.active = 0;
    return 1;
}

int sceCdStRead(unsigned int sectors, void* buf, unsigned int mode, unsigned int* err) {
    (void)mode;
    if (err) *err = 0;
    if (!g_st.active) return 0;
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) { if (err) *err = 1; return 0; }
    if (recvx_iso_read_sectors(iso, g_st.cursor_lsn, sectors, buf) != 0) {
        if (err) *err = 1;
        return 0;
    }
    g_st.cursor_lsn += sectors;
    return (int)sectors;
}
