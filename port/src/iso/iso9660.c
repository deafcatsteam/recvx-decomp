/*
 * Minimal ISO9660 reader for the RECVX disc image.
 *
 * Phase 3: full PVD parse + recursive directory walk so sceCdSearchFile
 * can resolve names like "\MOVIE\OP.SFD;1" against the real ISO.
 */

#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ISO_SECTOR_SIZE 2048
#define ISO_PVD_LBA     16

struct recvx_iso {
    FILE*    fp;
    long     size_bytes;
    uint32_t root_lba;
    uint32_t root_size;
};

static recvx_iso_t* g_iso;

static uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_sector(recvx_iso_t* iso, uint32_t lba, void* buf) {
    long ofs = (long)lba * ISO_SECTOR_SIZE;
    if (fseek(iso->fp, ofs, SEEK_SET) != 0) return -1;
    return (fread(buf, ISO_SECTOR_SIZE, 1, iso->fp) == 1) ? 0 : -1;
}

/* Compare a directory-record name (not NUL-terminated) against a
 * caller-supplied component. ISO9660 stores files as "NAME.EXT;1" and
 * the game passes names with the ";1" included, so we can do a byte
 * match with an ASCII-case-insensitive pass. */
static int name_match(const uint8_t* rec_name, int rec_len,
                      const char* want) {
    int wlen = (int)strlen(want);
    if (wlen != rec_len) return 0;
    for (int i = 0; i < rec_len; ++i) {
        int a = toupper((unsigned char)rec_name[i]);
        int b = toupper((unsigned char)want[i]);
        if (a != b) return 0;
    }
    return 1;
}

/* Search one directory extent (lba + byte size) for `name`. If found,
 * out_lba/out_size receive the file's extent and bit 1 of flags. */
static int dir_search(recvx_iso_t* iso, uint32_t dir_lba, uint32_t dir_size,
                      const char* name,
                      uint32_t* out_lba, uint32_t* out_size, int* out_is_dir) {
    uint8_t sector[ISO_SECTOR_SIZE];
    uint32_t sectors = (dir_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
    for (uint32_t s = 0; s < sectors; ++s) {
        if (read_sector(iso, dir_lba + s, sector) != 0) return -1;
        uint32_t off = 0;
        while (off < ISO_SECTOR_SIZE) {
            uint8_t rec_len = sector[off];
            if (rec_len == 0) break; /* padded to end-of-sector */
            uint32_t lba   = rd_u32_le(sector + off + 2);
            uint32_t size  = rd_u32_le(sector + off + 10);
            uint8_t  flags = sector[off + 25];
            uint8_t  nlen  = sector[off + 32];
            const uint8_t* nm = sector + off + 33;
            if (name_match(nm, nlen, name)) {
                if (out_lba)    *out_lba  = lba;
                if (out_size)   *out_size = size;
                if (out_is_dir) *out_is_dir = (flags & 0x02) ? 1 : 0;
                return 0;
            }
            off += rec_len;
        }
    }
    return -1;
}

recvx_iso_t* recvx_iso_open(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    recvx_iso_t* iso = (recvx_iso_t*)calloc(1, sizeof(*iso));
    iso->fp = fp;
    fseek(fp, 0, SEEK_END);
    iso->size_bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t pvd[ISO_SECTOR_SIZE];
    if (read_sector(iso, ISO_PVD_LBA, pvd) != 0 || pvd[0] != 1 ||
        memcmp(pvd + 1, "CD001", 5) != 0) {
        RX_LOG("iso", "WARN: no valid ISO9660 PVD at LBA 16 in %s", path);
    } else {
        /* Root directory record lives at offset 156 of the PVD. */
        iso->root_lba  = rd_u32_le(pvd + 156 + 2);
        iso->root_size = rd_u32_le(pvd + 156 + 10);
    }

    RX_LOG("iso", "opened %s (%ld bytes, root LBA %u size %u)",
           path, iso->size_bytes, iso->root_lba, iso->root_size);
    return iso;
}

void recvx_iso_close(recvx_iso_t* iso) {
    if (!iso) return;
    if (iso->fp) fclose(iso->fp);
    if (g_iso == iso) g_iso = NULL;
    free(iso);
}

int recvx_iso_read_sectors(recvx_iso_t* iso, uint32_t lba, uint32_t count, void* buf) {
    if (!iso || !iso->fp) return -1;
    long ofs = (long)lba * ISO_SECTOR_SIZE;
    if (fseek(iso->fp, ofs, SEEK_SET) != 0) return -1;
    size_t n = fread(buf, ISO_SECTOR_SIZE, count, iso->fp);
    return (n == count) ? 0 : -1;
}

/* Walk a path like "\MOVIE\OP.SFD;1" from the root directory. Each
 * component except the last must resolve to a directory; the last must
 * resolve to a file. Game code also passes names without a leading '\'
 * (see ps2_sg_gd.c's per-file case-normalized forms), so we accept both. */
int recvx_iso_find(recvx_iso_t* iso, const char* path,
                   uint32_t* out_lba, uint32_t* out_size) {
    if (!iso || !path) return -1;

    /* Strip leading separators. */
    while (*path == '\\' || *path == '/') ++path;

    uint32_t cur_lba  = iso->root_lba;
    uint32_t cur_size = iso->root_size;

    char comp[256];
    while (*path) {
        /* Extract one component. */
        int ci = 0;
        while (*path && *path != '\\' && *path != '/' && ci < 255) {
            comp[ci++] = *path++;
        }
        comp[ci] = 0;
        int is_last = (*path == 0);
        if (*path) ++path;

        uint32_t found_lba = 0, found_size = 0;
        int found_is_dir = 0;

        /* Game-supplied names sometimes lack ";1" for files, sometimes
         * include it. Try exact first, then with ";1" appended. */
        if (dir_search(iso, cur_lba, cur_size, comp,
                       &found_lba, &found_size, &found_is_dir) != 0) {
            char with_ver[260];
            snprintf(with_ver, sizeof(with_ver), "%s;1", comp);
            if (dir_search(iso, cur_lba, cur_size, with_ver,
                           &found_lba, &found_size, &found_is_dir) != 0) {
                return -1;
            }
        }

        if (is_last) {
            if (found_is_dir) return -1;
            if (out_lba)  *out_lba  = found_lba;
            if (out_size) *out_size = found_size;
            return 0;
        }
        if (!found_is_dir) return -1;
        cur_lba  = found_lba;
        cur_size = found_size;
    }
    return -1;
}

void         recvx_iso_set_global(recvx_iso_t* iso) { g_iso = iso; }
recvx_iso_t* recvx_iso_global(void)                  { return g_iso; }
