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
    "RDX_LNK.AFS",  /* partition 7 — holds all room files (rm_*.rdx) */
};

static recvx_afs_t* g_afs[8];
static char         g_gamedata[512];
static int          g_mounted;

void recvx_set_gamedata_dir(const char* dir) {
    if (!dir) { g_gamedata[0] = 0; return; }
    strncpy(g_gamedata, dir, sizeof g_gamedata - 1);
    g_gamedata[sizeof g_gamedata - 1] = 0;
}

const char* recvx_gamedata_dir(void) {
    return g_gamedata[0] ? g_gamedata : NULL;
}

/* types.h field offsets (these fields live BEFORE `void* typ_exp` @ 0x50 in
 * SYS_WORK, so the PS2 32-bit offsets survive the x64 ABI pointer growth).
 *   sys_partid @ 0x30, itm_partid @ 0x34, dor_partid @ 0x3C
 * Same pattern as main_pc.c's SYS_TK_FLG macro. */
extern void* sys;
#define SYS_U32_AT(off)  (*(uint32_t*)((char*)sys + (off)))

/* Called by adv.c ResetAdvSystem path (line 3737) and from main_pc.c
 * right after njUserInit so the AFS archives are open before the first
 * Adv_FirstWarningMessage tick.
 *
 * Resilient: keeps going on per-partition failures. The boot chain
 * (Warning / Ipl / Title) only reads from partition 6 (SYSTEM) and
 * partition 3 (ADV); the rest are used later for BGM / voice / item
 * menus and can fail to open without blocking progress. Partition IDs
 * are still set unconditionally so read attempts into missing archives
 * fall through to the -1 return in RequestReadInsideFile instead of
 * accidentally hitting a different archive (which happened when we
 * returned early and sys_partid stayed at its zero-init value,
 * steering Warning's SYSTEM.AFS reads into BGM1.AFS). */
int MountSoundAfs(void) {
    if (g_mounted) return 0;
    if (!g_gamedata[0]) {
        RX_LOG("afs", "MountSoundAfs: no --gamedata dir set");
        return -1;
    }
    int opened = 0;
    for (int i = 0; i < 8; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", g_gamedata, g_afs_name[i]);
        g_afs[i] = recvx_afs_open(path);
        if (g_afs[i]) ++opened;
        else          RX_LOG("afs", "skip partition %d (%s)", i, g_afs_name[i]);
    }
    /* sdfunc.c:435..442 — assign partition ids whether or not every
     * file opened. PatId[-1] (the default) is a valid "missing" marker
     * that downstream code treats as do-nothing; what we can't have is
     * sys_partid staying 0 and routing reads to the wrong archive. */
    PatId[0] = 0;   /* BGM1      */
    PatId[1] = 1;   /* VOICE1    */
    PatId[2] = 2;   /* MULTSPQ1  */
    PatId[3] = 3;   /* ADV       */
    SYS_U32_AT(0x34) = 4; /* itm_partid */
    SYS_U32_AT(0x3C) = 5; /* dor_partid */
    SYS_U32_AT(0x30) = 6; /* sys_partid */
    g_mounted = 1;
    /* Warning / Title rely on SYSTEM (6) + ADV (3). Fail only if those
     * two are missing. */
    int ok = (g_afs[6] != NULL) && (g_afs[3] != NULL);
    RX_LOG("afs", "mounted %d/7 partitions; sys_partid=6 itm_partid=4 dor_partid=5 (%s)",
           opened, ok ? "boot OK" : "boot BLOCKED — SYSTEM/ADV missing");
    return ok ? 0 : -1;
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

/* Real signature from sdfunc.c:3027 is `int RequestReadIsoFile(char* name,
 * void* dst)`. Game calls it with bare names like "sysmes.ald". We resolve
 * via recvx_iso_find against the open ISO, then read all sectors into dst.
 * Used by message.c bhSysCallMonitor case 5 (sysmes.ald), bup_00.c (item
 * descriptions), ranking.c, etc. */
/* Case-insensitive ASCII compare (just the chars the rdx_files[] table
 * uses — no locale or wide-char nonsense). */
static int port_strcasecmp_ascii(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        int ca = (*a >= 'a' && *a <= 'z') ? (*a - 32) : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? (*b - 32) : *b;
        if (ca != cb) return ca - cb;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Room files (RM_NNNN.RDX) aren't flat ISO entries — they live inside
 * RDX_LNK.AFS. Walk the rdx_files[205] index table from ps2_dvd_image.c
 * to find the AFS entry index, then read it from g_afs[7]. Returns 0
 * on hit (no flat-ISO fallback), -1 on miss (fall through to ISO). */
extern char* rdx_files[205];
extern int rdx_image_data_max;

static int port_try_rdx_lookup(const char* name, void* dst) {
    /* Quick prefix filter so we don't scan 205 entries for every read. */
    if (!(name[0] == 'r' || name[0] == 'R') ||
        !(name[1] == 'm' || name[1] == 'M') ||
        name[2] != '_') {
        return -1;
    }
    if (!g_afs[7]) {
        RX_LOG("iso", "rdx lookup miss: RDX_LNK.AFS not mounted (file=%s)", name);
        return -1;
    }
    for (int i = 0; i < rdx_image_data_max; ++i) {
        if (port_strcasecmp_ascii(rdx_files[i], name) == 0) {
            uint32_t got = recvx_afs_read(g_afs[7], (unsigned)i, dst);
            if (!got) {
                RX_LOG("iso", "rdx lookup FAIL: AFS read empty for %s (idx=%d)",
                       name, i);
                return -1;
            }
            RX_LOG("iso", "rdx lookup OK: %s -> idx=%d bytes=%u", name, i, got);
            g_last_status = 0;
            return 0;
        }
    }
    return -1;
}

int RequestReadIsoFile(const char* name, void* dst) {
    if (!name || !dst) { g_last_status = -1; return -1; }

    /* Route room files (RM_NNNN.RDX) through RDX_LNK.AFS. */
    if (port_try_rdx_lookup(name, dst) == 0) return 0;

    extern recvx_iso_t* recvx_iso_global(void);
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) {
        RX_LOG("iso", "RequestReadIsoFile FAIL: no ISO mounted (file=%s)", name);
        g_last_status = -1;
        return -1;
    }
    uint32_t lba = 0, sz = 0;
    if (recvx_iso_find(iso, name, &lba, &sz) != 0) {
        RX_LOG("iso", "RequestReadIsoFile FAIL: file not found (%s)", name);
        g_last_status = -1;
        return -1;
    }
    /* sz is byte count; sector size is 2048 so round up. */
    uint32_t nsec = (sz + 2047u) / 2048u;
    if (recvx_iso_read_sectors(iso, lba, nsec, dst) != 0) {
        RX_LOG("iso", "RequestReadIsoFile FAIL: read err lba=%u nsec=%u (%s)",
               lba, nsec, name);
        g_last_status = -1;
        return -1;
    }
    RX_LOG("iso", "RequestReadIsoFile OK: %s -> %u bytes (lba=%u)", name, sz, lba);
    g_last_status = 0;
    return 0;
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

/* Real signature: `int GetIsoFileSize(char* FileName)`. Returns 0 if the
 * file isn't on the ISO. Used to size buffers before RequestReadIsoFile. */
int GetIsoFileSize(const char* name) {
    if (!name) return 0;
    /* Room files live in RDX_LNK.AFS — same routing as RequestReadIsoFile. */
    if ((name[0] == 'r' || name[0] == 'R') &&
        (name[1] == 'm' || name[1] == 'M') &&
        name[2] == '_' && g_afs[7]) {
        for (int i = 0; i < rdx_image_data_max; ++i) {
            if (port_strcasecmp_ascii(rdx_files[i], name) == 0) {
                return (int)recvx_afs_entry_size(g_afs[7], (unsigned)i);
            }
        }
    }
    extern recvx_iso_t* recvx_iso_global(void);
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) return 0;
    uint32_t lba = 0, sz = 0;
    if (recvx_iso_find(iso, name, &lba, &sz) != 0) return 0;
    return (int)sz;
}

int GetInsideFileSize(unsigned int pat, unsigned int id) {
    if (pat >= 8 || !g_afs[pat]) return 0;
    return (int)recvx_afs_entry_size(g_afs[pat], id);
}

int GetReadFileStatus(void) { return g_last_status; }
