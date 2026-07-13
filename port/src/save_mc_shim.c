/*
 * Save/load hardware shim — replaces two pieces of PS2 hardware that
 * ps2_MemoryCard..c / ps2_sg_bup.c / ps2_McSaveFile.c call directly and
 * that don't exist on PC:
 *
 *   1. sceMc* (KATANA libmc.h) — the memory-card driver. ps2_MemoryCard..c
 *      is "100% matching!" decomp (a real async state machine: a sceMcXxx
 *      kicks a request, sceMcSync polls for completion), but every one of
 *      its ~20 call sites already retry-loops on sceMcSync before doing
 *      anything with the result. Same async-collapse as afs_mount.c: our
 *      sceMcXxx calls do the real file I/O synchronously against a
 *      loose-file "virtual card" directory, and sceMcSync always reports
 *      "done" with that result on the very next call.
 *
 *   2. gdFsOpen/gdFsGetFileSize/gdFsRead/gdFsClose (KATANA sg_gd.h) — the
 *      GD-ROM sector reader. ps2_McSaveFile.c's mcReadIconData uses it for
 *      exactly one file, "bio_cv.ico" (the memory-card icon template),
 *      which ships as a loose file at the gamedata root (BIO_CV.ICO). This
 *      is a real, on-the-critical-path read (ExecuteStateSysSaveWriteSysData
 *      case 2 treats a failed read as a card error) — not a stubbable gap.
 */

#include "types.h"
#include "ps2_sg_gd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Not #include "recvx_port.h": it pulls <stdbool.h>, which collides with
 * KATANA's own `enum bool { false, true };` in sg_xpt.h (same reason
 * game_texture_stubs.c forward-declares these instead of including the
 * header). */
extern const char* recvx_gamedata_dir(void);
extern void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)

/* recvx_game_prelude.h's own `#include <string.h>`/`<stdio.h>` ("real CRT
 * comes first") resolves to KATANA's SH-series copies here, not glibc's —
 * the KATANA include dir is on the -I search path for the whole
 * translation unit, ahead of the system default, and the guard defines
 * that are supposed to mask KATANA's copies (_STRING_SHC etc.) aren't set
 * until *after* that include. Two fallouts, both otherwise invisible
 * because no previously-compiled file happened to call these entry
 * points directly:
 *   - snprintf isn't declared at all (this SH-era header predates C99).
 *   - strcpy/strcmp get macro'd to _builtin_strcpy/_builtin_strcmp (the
 *     original Hitachi SH-C compiler's intrinsic names, single
 *     underscore — meaningless to GCC, which uses __builtin_strcpy).
 * ps2_MemoryCard..c/ps2_McSaveFile.c call plain strcpy/strcmp (they're
 * "100% matching!" decomp, not ours to touch), so both gaps are patched
 * here rather than in the shared prelude. */
extern int snprintf(char* str, size_t size, const char* format, ...);
/* Written as manual loops, not strcpy(dst,src)/strcmp(a,b): those would
 * macro-expand right back into _builtin_strcpy/_builtin_strcmp per the
 * comment above, i.e. infinite self-recursion. */
char* _builtin_strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++) != 0) { }
    return dst;
}
int _builtin_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#ifdef _WIN32
# include <windows.h>
# include <direct.h>
# define port_mkdir(p) _mkdir(p)
#else
# include <sys/stat.h>
# include <dirent.h>
# define port_mkdir(p) mkdir(p, 0777)
#endif

/* ------------------------------------------------------------------ */
/* Virtual memory card: a real directory on disk, one file per card
 * "file" (icon.sys / bio_cv.ico / BASLUS-20184/SAVEDATA-NN / config).
 * Single-port, single concurrent open file — matches the decomp's own
 * usage, which never sets MEMORYCARDSTATE.lOpenFileNumber away from 0.
 * ------------------------------------------------------------------ */

static char g_mc_root[512];
static int  g_mc_root_ready;

static const char* mc_root_dir(void) {
    if (!g_mc_root_ready) {
        const char* gd = recvx_gamedata_dir();
        if (gd) snprintf(g_mc_root, sizeof g_mc_root, "%s/SAVE", gd);
        else    snprintf(g_mc_root, sizeof g_mc_root, "./SAVE");
        port_mkdir(g_mc_root);
        g_mc_root_ready = 1;
    }
    return g_mc_root;
}

/* Card paths look like "/BASLUS-20184" or "/BASLUS-20184/SAVEDATA-00".
 * Strip the leading slash and join onto the root; create any missing
 * intermediate directory (the game's own sceMcMkdir calls cover the
 * "/BASLUS-20184" level, but keeping this resilient costs nothing). */
static void mc_translate(const char* cardpath, char* out, size_t outsz) {
    if (cardpath && cardpath[0] == '/') cardpath++;
    snprintf(out, outsz, "%s/%s", mc_root_dir(), cardpath ? cardpath : "");
}

static int g_mc_result;     /* stashed outcome of the last kicked op */
static FILE* g_mc_fp;        /* the one concurrently-open card file */

int sceMcInit(void) {
    mc_root_dir();
    return 0; /* 0 = success */
}

int sceMcGetInfo(int port, int slot, int* type, int* free_, int* format) {
    (void)port; (void)slot;
    if (type)   *type   = 2;    /* PS2-formatted card present */
    if (free_)  *free_  = 8192; /* plenty of free blocks */
    if (format) *format = 0;    /* formatted */
    g_mc_result = 0;
    return 0;
}

int sceMcSync(int mode, int* cmd, int* result) {
    (void)mode;
    if (cmd)    *cmd = 0;
    if (result) *result = g_mc_result;
    return 1; /* always "complete" — the op above already ran */
}

int sceMcOpen(int port, int slot, char* name, int mode) {
    (void)port; (void)slot;
    if (g_mc_fp) { fclose(g_mc_fp); g_mc_fp = NULL; }
    char path[600];
    mc_translate(name, path, sizeof path);
    /* mode bit 0x1 (SCE_STM_R) with no write bit == read-only open of an
     * existing file; anything else (write, or write|create) opens for
     * read+write, creating the file if it doesn't exist yet. */
    const char* fmode = ((mode & 0x3) == 0x1) ? "rb" : "w+b";
    g_mc_fp = fopen(path, fmode);
    g_mc_result = g_mc_fp ? 0 : -1;
    return 0; /* kicked ok; real result lands in sceMcSync */
}

int sceMcClose(int fd) {
    (void)fd;
    if (g_mc_fp) { fclose(g_mc_fp); g_mc_fp = NULL; }
    g_mc_result = 0;
    return 0;
}

int sceMcRead(int fd, void* buf, int size) {
    (void)fd;
    size_t got = g_mc_fp ? fread(buf, 1, (size_t)size, g_mc_fp) : 0;
    g_mc_result = (int)got;
    return 0;
}

int sceMcWrite(int fd, void* buf, int size) {
    (void)fd;
    size_t put = g_mc_fp ? fwrite(buf, 1, (size_t)size, g_mc_fp) : 0;
    g_mc_result = (int)put;
    return 0;
}

int sceMcMkdir(int port, int slot, char* name) {
    (void)port; (void)slot;
    char path[600];
    mc_translate(name, path, sizeof path);
    port_mkdir(path);
    g_mc_result = 0;
    return 0;
}

int sceMcChdir(int port, int slot, char* name, char* buf) {
    (void)port; (void)slot; (void)name; (void)buf;
    /* Advisory only: every caller in this codebase rebuilds a full card
     * path from scratch before each open/read/getdir rather than relying
     * on a stateful "current directory", so there's no real state to
     * track here. */
    g_mc_result = 0;
    return 0;
}

int sceMcGetDir(int port, int slot, char* name, unsigned flag, int maxent, sceMcTblGetDir* buf) {
    (void)port; (void)slot; (void)flag;
    char dirpath[600];
    /* "/BASLUS-20184/*" (wildcard) lists a directory; anything else is a
     * single-entry existence check. */
    size_t len = name ? strlen(name) : 0;
    int wildcard = (len >= 2 && name[len - 1] == '*' && name[len - 2] == '/');
    char trimmed[600];
    if (wildcard) {
        snprintf(trimmed, sizeof trimmed, "%.*s", (int)(len - 2), name);
        mc_translate(trimmed, dirpath, sizeof dirpath);
    } else {
        mc_translate(name, dirpath, sizeof dirpath);
    }

    int count = 0;
    if (wildcard) {
#ifdef _WIN32
        char glob[640];
        snprintf(glob, sizeof glob, "%s/*", dirpath);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(glob, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (count >= maxent) break;
                memset(&buf[count], 0, sizeof buf[count]);
                strncpy((char*)buf[count].EntryName, fd.cFileName, sizeof buf[count].EntryName - 1);
                buf[count].FileSizeByte = fd.nFileSizeLow;
                count++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        DIR* d = opendir(dirpath);
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                if (count >= maxent) break;
                char entpath[700];
                snprintf(entpath, sizeof entpath, "%s/%s", dirpath, ent->d_name);
                FILE* f = fopen(entpath, "rb");
                long sz = 0;
                if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
                memset(&buf[count], 0, sizeof buf[count]);
                strncpy((char*)buf[count].EntryName, ent->d_name, sizeof buf[count].EntryName - 1);
                buf[count].FileSizeByte = (unsigned)sz;
                count++;
            }
            closedir(d);
        }
#endif
    } else {
        FILE* f = fopen(dirpath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            if (maxent > 0) {
                memset(&buf[0], 0, sizeof buf[0]);
                const char* base = strrchr(name ? name : "", '/');
                base = base ? base + 1 : name;
                strncpy((char*)buf[0].EntryName, base ? base : "", sizeof buf[0].EntryName - 1);
                buf[0].FileSizeByte = (unsigned)sz;
            }
            count = 1;
        }
    }

    g_mc_result = count;
    return 0;
}

int sceMcFormat(int port, int slot) {
    (void)port; (void)slot;
    /* Ensures the card root exists; deliberately non-destructive. Real
     * hardware format only runs when sceMcGetInfo reports an unformatted
     * card, which ours never does, so this path is unreachable in normal
     * play — same "never actually hit" shape as bhEne03_Collision. */
    mc_root_dir();
    g_mc_result = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GD-ROM reader — only ever asked for "bio_cv.ico", a loose file at the
 * gamedata root (BIO_CV.ICO). Case-insensitive scan, same technique as
 * afs_mount.c's port_loose_find_path. */
/* ------------------------------------------------------------------ */

struct GDS_FS_HANDLE_shim { FILE* fp; };

/* Manual ASCII lowercasing, not tolower(): <ctype.h> has the same
 * KATANA-shadow problem as <string.h> above (its own SH-era copy sits on
 * the include path ahead of glibc's), and this only needs A-Z anyway. */
static int port_lower_ascii(int c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int port_strcasecmp_ascii(const char* a, const char* b) {
    while (*a && *b) {
        int ca = port_lower_ascii((unsigned char)*a), cb = port_lower_ascii((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

GDFS gdFsOpen(const char* fname, GDFS_DIRREC gf_dirrec) {
    (void)gf_dirrec;
    const char* gd = recvx_gamedata_dir();
    if (!gd || !fname) return NULL;

    char path[600];
    int found = 0;
#ifdef _WIN32
    char glob[640];
    snprintf(glob, sizeof glob, "%s/*", gd);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (port_strcasecmp_ascii(fd.cFileName, fname) == 0) {
                snprintf(path, sizeof path, "%s/%s", gd, fd.cFileName);
                found = 1;
                break;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(gd);
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            if (port_strcasecmp_ascii(ent->d_name, fname) == 0) {
                snprintf(path, sizeof path, "%s/%s", gd, ent->d_name);
                found = 1;
                break;
            }
        }
        closedir(d);
    }
#endif
    if (!found) {
        RX_LOG("save", "gdFsOpen: %s not found under gamedata dir", fname);
        return NULL;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    struct GDS_FS_HANDLE_shim* h2 = (struct GDS_FS_HANDLE_shim*)calloc(1, sizeof *h2);
    h2->fp = fp;
    return (GDFS)h2;
}

Bool gdFsGetFileSize(GDFS gdfs, Sint32* fsize) {
    struct GDS_FS_HANDLE_shim* h = (struct GDS_FS_HANDLE_shim*)gdfs;
    if (!h || !h->fp || !fsize) return FALSE;
    long cur = ftell(h->fp);
    fseek(h->fp, 0, SEEK_END);
    *fsize = (Sint32)ftell(h->fp);
    fseek(h->fp, cur, SEEK_SET);
    return TRUE;
}

Sint32 gdFsRead(GDFS gdfs, Sint32 nsct, void* buf) {
    struct GDS_FS_HANDLE_shim* h = (struct GDS_FS_HANDLE_shim*)gdfs;
    if (!h || !h->fp) return -1;
    size_t want = (size_t)nsct * 2048;
    size_t got = fread(buf, 1, want, h->fp);
    return (got == want || feof(h->fp)) ? 0 : -1;
}

void gdFsClose(GDFS gdfs) {
    struct GDS_FS_HANDLE_shim* h = (struct GDS_FS_HANDLE_shim*)gdfs;
    if (!h) return;
    if (h->fp) fclose(h->fp);
    free(h);
}
