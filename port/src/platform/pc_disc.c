/* ISO 9660 reader backing the game's disc accesses (see pc_disc.h). */
#include "pc_disc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *disc;
static unsigned int root_lsn, root_size;

static unsigned int le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

int pc_disc_read_bytes(uint64_t offset, unsigned int size, void *buf)
{
    if (disc == NULL && !pc_disc_open())
        return 0;
    if (fseeko(disc, (off_t)offset, SEEK_SET) != 0)
        return 0;
    return fread(buf, 1, size, disc) == size;
}

int pc_disc_read(unsigned int lsn, unsigned int nsct, void *buf)
{
    return pc_disc_read_bytes((uint64_t)lsn * PC_DISC_SECTOR,
                              nsct * PC_DISC_SECTOR, buf);
}

int pc_disc_open(void)
{
    unsigned char pvd[PC_DISC_SECTOR];
    const char *path;

    if (disc != NULL)
        return 1;

    path = getenv("CVX_ISO");
    if (path == NULL)
        path = "cvx.iso";

    disc = fopen(path, "rb");
    if (disc == NULL) {
        fprintf(stderr, "pc_disc: cannot open the game image '%s' "
                        "(set CVX_ISO to your own dump of the disc)\n", path);
        return 0;
    }

    if (!pc_disc_read(16, 1, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        fprintf(stderr, "pc_disc: '%s' is not an ISO 9660 image\n", path);
        fclose(disc);
        disc = NULL;
        return 0;
    }

    /* Root directory record of the primary volume descriptor. */
    root_lsn = le32(pvd + 156 + 2);
    root_size = le32(pvd + 156 + 10);
    return 1;
}

/* Case-insensitive compare of a disc name (which may end in ";1") with one
 * path component of length len. */
static int name_matches(const unsigned char *rec_name, int rec_len, const char *want, int len)
{
    const unsigned char *semi = memchr(rec_name, ';', rec_len);

    if (semi != NULL)
        rec_len = (int)(semi - rec_name);
    if (rec_len > 0 && rec_name[rec_len - 1] == '.')
        rec_len--; /* "NAME." is how files without an extension are stored */

    if (rec_len != len)
        return 0;
    for (int i = 0; i < len; i++) {
        if (toupper(rec_name[i]) != toupper((unsigned char)want[i]))
            return 0;
    }
    return 1;
}

static int find_in_dir(unsigned int dir_lsn, unsigned int dir_size, const char *want, int len,
                       unsigned int *lsn, unsigned int *size, int *is_dir)
{
    unsigned char sector[PC_DISC_SECTOR];
    unsigned int s, pos;

    for (s = 0; s < (dir_size + PC_DISC_SECTOR - 1) / PC_DISC_SECTOR; s++) {
        if (!pc_disc_read(dir_lsn + s, 1, sector))
            return 0;
        for (pos = 0; pos < PC_DISC_SECTOR; ) {
            unsigned char *rec = sector + pos;
            if (rec[0] == 0)
                break; /* records never span sectors; the rest is padding */
            if (name_matches(rec + 33, rec[32], want, len)) {
                *lsn = le32(rec + 2);
                *size = le32(rec + 10);
                *is_dir = (rec[25] & 2) != 0;
                return 1;
            }
            pos += rec[0];
        }
    }
    return 0;
}

int pc_disc_find(const char *name, unsigned int *lsn, unsigned int *size)
{
    unsigned int cur_lsn, cur_size;
    int is_dir = 1;

    if (!pc_disc_open())
        return 0;

    cur_lsn = root_lsn;
    cur_size = root_size;

    while (*name != '\0') {
        const char *end;
        int len;

        while (*name == '\\' || *name == '/')
            name++;
        end = name;
        while (*end != '\0' && *end != '\\' && *end != '/' && *end != ';')
            end++;
        len = (int)(end - name);
        if (len == 0)
            break;
        if (!is_dir || !find_in_dir(cur_lsn, cur_size, name, len, &cur_lsn, &cur_size, &is_dir))
            return 0;
        name = end;
        if (*name == ';')
            break;
    }

    if (is_dir)
        return 0;
    *lsn = cur_lsn;
    *size = cur_size;
    return 1;
}
