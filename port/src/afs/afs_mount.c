/*
 * afs_mount.c — port-side re-implementation of the handful of sdfunc.c
 * symbols the boot chain needs to load files from the game's AFS archives.
 *
 * On PS2 sdfunc.c drives CRI ADXF to manage "partitions" (one per AFS
 * file) and async reads. We don't compile sdfunc.c because it also
 * drags in ~3000 lines of sound / voice / vibration plumbing we don't
 * need yet. Instead we replicate the eight entries of SoundAfsPatDef
 * (sdfunc.c:92) and back each with a recvx_afs_t opened from a loose
 * file on disk, keyed by partition id.
 *
 * The async poll collapses: recvx_afs_read is synchronous, so
 * GetReadFileStatus flips 1 -> 0 within the same frame the game
 * issues RequestReadInsideFile.
 */

#include "recvx_afs.h"
#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Match sdfunc.c:91 exactly so adv.c (which sees `extern int PatId[4]`
 * via sdfunc.h) links against the same object. */
int PatId[4] = { -1, -1, -1, -1 };

/* Matches sdfunc.c:92 SoundAfsPatDef PartitionId values. Disc-1 selection
 * (BGM1 / VOICE1 / MULTSPQ1 / ITEM1); disc-swap work lives further out. */
static const char* g_afs_name[8] = {
    "BGM1.AFS",     /* partition 0 */
    "VOICE1.AFS",   /* partition 1 */
    "MULTSPQ1.AFS", /* partition 2 */
    "ADV.AFS",      /* partition 3 */
    "ITEM1.AFS",    /* partition 4 */
    "MRY.AFS",      /* partition 5 */
    "SYSTEM.AFS",   /* partition 6 */
    NULL,
};

static recvx_afs_t* g_afs[8];
static char         g_gamedata[512];
static int          g_mounted;

void recvx_set_gamedata_dir(const char* dir) {
    if (!dir) { g_gamedata[0] = 0; return; }
    strncpy(g_gamedata, dir, sizeof g_gamedata - 1);
    g_gamedata[sizeof g_gamedata - 1] = 0;
}

/* types.h field offsets (these fields live BEFORE `void* typ_exp` @ 0x50 in
 * SYS_WORK, so the PS2 32-bit offsets survive the x64 ABI pointer growth).
 *   sys_partid @ 0x30, itm_partid @ 0x34, dor_partid @ 0x3C
 * Same pattern as main_pc.c's SYS_TK_FLG macro. */
extern void* sys;
#define SYS_U32_AT(off)  (*(uint32_t*)((char*)sys + (off)))

/* Called by adv.c ResetAdvSystem path (line 3737) and from main_pc.c
 * right after njUserInit so the AFS archives are open before the first
 * Adv_FirstWarningMessage tick. */
int MountSoundAfs(void) {
    if (g_mounted) return 0;
    if (!g_gamedata[0]) {
        RX_LOG("afs", "MountSoundAfs: no --gamedata dir set");
        return -1;
    }
    for (int i = 0; g_afs_name[i]; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", g_gamedata, g_afs_name[i]);
        g_afs[i] = recvx_afs_open(path);
        if (!g_afs[i]) {
            RX_LOG("afs", "MountSoundAfs: failed to open partition %d (%s)",
                   i, g_afs_name[i]);
            return -1;
        }
    }
    /* sdfunc.c:435..442 — set partition ids on success. Matching the
     * table index is enough since we key lookups by id directly. */
    PatId[0] = 0;   /* BGM1      */
    PatId[1] = 1;   /* VOICE1    */
    PatId[2] = 2;   /* MULTSPQ1  */
    PatId[3] = 3;   /* ADV       */
    SYS_U32_AT(0x34) = 4; /* itm_partid */
    SYS_U32_AT(0x3C) = 5; /* dor_partid */
    SYS_U32_AT(0x30) = 6; /* sys_partid */
    g_mounted = 1;
    RX_LOG("afs", "mounted 7 partitions; sys_partid=6 itm_partid=4 dor_partid=5");
    return 0;
}

void UnmountSoundAfs(void) {
    if (!g_mounted) return;
    for (int i = 0; i < 8; ++i) {
        if (g_afs[i]) { recvx_afs_close(g_afs[i]); g_afs[i] = NULL; }
    }
    PatId[0] = PatId[1] = PatId[2] = PatId[3] = -1;
    g_mounted = 0;
}

/* Last request state — used so GetReadFileStatus can report 0 (done)
 * after a successful read, matching sdfunc.c's FileReadStatus flow. */
static int g_last_status = 0; /* 0 = idle/done, 1 = pending, -1 = error */

int RequestReadIsoFile(int a, int b, void* dst) {
    (void)a; (void)b; (void)dst;
    /* Not wired: boot chain uses RequestReadInsideFile. */
    g_last_status = -1;
    return -1;
}

int RequestReadInsideFile(unsigned int pat, unsigned int id, void* dst) {
    if (pat >= 8 || !g_afs[pat] || !dst) {
        RX_LOG("afs", "read FAIL: bad part=%u id=%u dst=%p",
               pat, id, dst);
        g_last_status = -1;
        return -1;
    }
    uint32_t got = recvx_afs_read(g_afs[pat], id, dst);
    if (!got) {
        RX_LOG("afs", "read FAIL: part=%u (%s) id=%u (empty entry?)",
               pat, g_afs_name[pat], id);
        g_last_status = -1;
        return -1;
    }
    RX_LOG("afs", "read OK: part=%u (%s) id=%u bytes=%u",
           pat, g_afs_name[pat], id, got);
    g_last_status = 0;
    return 0;
}

int GetIsoFileSize(int a) { (void)a; return 0; }

int GetInsideFileSize(unsigned int pat, unsigned int id) {
    if (pat >= 8 || !g_afs[pat]) return 0;
    return (int)recvx_afs_entry_size(g_afs[pat], id);
}

int GetReadFileStatus(void) { return g_last_status; }
