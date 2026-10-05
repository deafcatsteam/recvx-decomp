/*
 * Checks the disc image reader, the ADXF file layer and the Expand
 * decompressor against a synthetic ISO made by make_test_iso.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cri_adxf.h"
#include "libcdvd.h"

#include "../src/platform/pc_disc.h"

int Expand(char* s, unsigned char* d);

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void test_find(void)
{
    unsigned int lsn, size;

    CHECK(pc_disc_find("\\CF_ROM.TXT;1", &lsn, &size) && size == 20);
    CHECK(pc_disc_find("\\cf_rom.txt", &lsn, &size));
    CHECK(pc_disc_find("/MOVIE/OPEN.SFD", &lsn, &size) && size == 5000);
    CHECK(pc_disc_find("\\movie\\open.sfd;1", &lsn, &size));
    CHECK(!pc_disc_find("\\MISSING.BIN", &lsn, &size));
    CHECK(!pc_disc_find("\\MOVIE", &lsn, &size));
}

static void test_cdvd(void)
{
    sceCdlFILE fp;
    char buf[PC_DISC_SECTOR];

    CHECK(sceCdSearchFile(&fp, "\\CF_ROM.TXT;1") == 1);
    CHECK(sceCdRead(fp.lsn, 1, buf, NULL) == 1 && sceCdGetError() == 0);
    CHECK(memcmp(buf, "plain file contents\n", 20) == 0);
}

static void test_adxf(void)
{
    static char buf[4 * PC_DISC_SECTOR];
    ADXF f;

    CHECK(ADXF_LoadPartitionNw(17, "\\RDX_LNK.AFS", NULL, NULL) == 0);
    CHECK(ADXF_GetPtStat(17) == ADXF_STAT_READEND);
    CHECK(ADXF_GetPtStat(3) == ADXF_STAT_ERROR);

    f = ADXF_OpenAfs(17, 1);
    CHECK(f != NULL);
    CHECK(ADXF_GetFsizeByte(f) == 3000);
    CHECK(ADXF_GetFsizeSct(f) == 2);
    memset(buf, 0, sizeof(buf));
    ADXF_ReadNw32(f, ADXF_GetFsizeSct(f), buf);
    CHECK(ADXF_GetStat(f) == ADXF_STAT_READEND);
    CHECK(buf[0] == 'B' && buf[2999] == 'B' && buf[3000] == 0);
    ADXF_Close(f);

    f = ADXF_OpenAfs(17, 0);
    ADXF_ReadNw32(f, 1, buf);
    CHECK(memcmp(buf, "hello afs file 0", 16) == 0);
    ADXF_Close(f);

    CHECK(ADXF_OpenAfs(17, 3) == NULL);

    f = ADXF_Open("\\CF_ROM.TXT", NULL);
    CHECK(f != NULL && ADXF_GetFsizeByte(f) == 20);
    ADXF_Close(f);
}

static void test_expand(void)
{
    /* "ABC" as literals, a short match copying 3 bytes from 3 back, then the
     * end marker: descriptor bits 1,1,1 0,0,0,1 0 | 1. */
    static char packed[] = { 0x47, 'A', 'B', 'C', (char)0xfd, 0x01, 0x00, 0x00 };
    unsigned char out[16] = { 0 };

    CHECK(Expand(packed, out) == 6);
    CHECK(memcmp(out, "ABCABC", 6) == 0);
}

int main(void)
{
    if (!pc_disc_open()) {
        printf("FAIL: cannot open the test image\n");
        return 1;
    }
    test_find();
    test_cdvd();
    test_adxf();
    test_expand();
    if (failures == 0)
        printf("test_disc: all checks passed\n");
    return failures != 0;
}
