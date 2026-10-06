/*
 * The memory card through the game's own code (ps2_MemoryCard..c): finds the
 * card, makes the save folder and its 18 files the way the save screen does,
 * checks the list the load screen checks, then writes a save, reads it back
 * and checks it in the folder on the PC (CVX_SAVES).
 */
#include "ps2_MemoryCard..h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/host/pc_memcard.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static char *names[18] = {
    "icon.sys", "bio_cv.ico", "BASLUS-20184",
    "SAVEDATA-00", "SAVEDATA-01", "SAVEDATA-02", "SAVEDATA-03", "SAVEDATA-04",
    "SAVEDATA-05", "SAVEDATA-06", "SAVEDATA-07", "SAVEDATA-08", "SAVEDATA-09",
    "SAVEDATA-10", "SAVEDATA-11", "SAVEDATA-12", "SAVEDATA-13", "SAVEDATA-14",
};

static MEMORYCARDSTATE mc;

/* Runs the card's state machine until done (1) or failed (-1). */
static int run(int (*end)(MEMORYCARDSTATE *))
{
    int i, r = 0;

    for (i = 0; i < 100 && r == 0; i++) {
        ExecuteMemoryCard(&mc);
        r = end(&mc);
    }
    return r;
}

static int write_file(char *name, void *data, int size, int mode)
{
    if (WriteMemoryCard(&mc, data, size, name, mode) != 1)
        return 0;
    return run(RecoveryMemoryCardWriteEnd);
}

static int read_file(char *name, void *data, int size)
{
    if (ReadMemoryCard(&mc, data, size, name, 1) != 1)
        return 0;
    return run(RecoveryMemoryCardReadEnd);
}

static void empty_card(void)
{
    PcMcEntry list[64];
    char path[64];
    int n, i;

    n = pc_mc_getdir(0, "/BASLUS-20184/*", 64, list);
    for (i = 0; i < n; i++) {
        if (!list[i].is_dir) {
            snprintf(path, sizeof(path), "/BASLUS-20184/%s", list[i].name);
            pc_mc_delete(0, path);
        }
    }
    pc_mc_delete(0, "/BASLUS-20184");
}

int main(void)
{
    static unsigned char save[0x2000], back[0x2000];
    char host[512];
    FILE *f;
    int i, free_before, free_after, r;

    empty_card();
    CreateMemoryCard(&mc);

    /* The save screen first looks at both slots. */
    CHECK(AnalyzeMemoryCardAll(&mc) == 1);
    CHECK(run(RecoveryMemoryCardAnalyzeAllEnd) == 1);
    CHECK(GetMemoryCardSelectPortState(&mc, 0) == 2); /* a PS2 card */
    CHECK(GetMemoryCardSelectPortState(&mc, 1) == 0); /* nothing */
    CHECK(GetMemoryCardSelectPortFreeCapacity(&mc, 0) > 7000);
    CHECK(GetMemoryCardSelectPortFormatType(&mc, 0) == 1);

    /* Idle: the game checks whether a card was put in or taken out. */
    r = ExecuteMemoryCard(&mc);
    r = ExecuteMemoryCard(&mc);
    CHECK(r == 100); /* nothing changed */

    SetMemoryCardCurrentPort(&mc, 0);
    SetCheckMcFlag(&mc, 1);
    CHECK(CheckMemoryCardFormatStatus(&mc) == 2); /* formatted */
    CHECK(CheckMemoryCardExistSubDirectory(&mc) == -1);
    free_before = GetMemoryCardFreeCapacity(&mc);
    CHECK(free_before > 7000);

    /* A new save folder, with all its files. */
    CHECK(CreateMemoryCardSubDirectory(&mc) == 1);
    CHECK(CheckMemoryCardExistSubDirectory(&mc) == 1);
    CHECK(CheckMemoryCardExistFileList(&mc, names, 18) == -1); /* still empty */
    for (i = 0; i < 18; i++) {
        memset(save, i, sizeof(save));
        CHECK(write_file(names[i], save, i < 3 ? 964 : (int)sizeof(save), 514) == 1);
    }
    CHECK(CheckMemoryCardExistFileList(&mc, names, 18) == 1);
    free_after = GetMemoryCardFreeCapacity(&mc);
    CHECK(free_after < free_before - 15 * 8 && free_after > 7000);

    /* A save over an existing file, then read back. */
    for (i = 0; i < (int)sizeof(save); i++)
        save[i] = (unsigned char)(i * 7 + 3);
    CHECK(write_file(names[5], save, sizeof(save), 2) == 1);
    memset(back, 0, sizeof(back));
    CHECK(read_file(names[5], back, sizeof(back)) == 1);
    CHECK(memcmp(save, back, sizeof(save)) == 0);
    CHECK(read_file(names[4], back, sizeof(back)) == 1);
    CHECK(back[0] == 4 && back[sizeof(back) - 1] == 4);

    /* It is a plain file on the PC, without leftovers. */
    snprintf(host, sizeof(host), "%s/BASLUS-20184/SAVEDATA-02", pc_mc_root());
    f = fopen(host, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        memset(back, 0, sizeof(back));
        CHECK(fread(back, 1, sizeof(back), f) == sizeof(back));
        CHECK(memcmp(save, back, sizeof(save)) == 0);
        fclose(f);
    }
    snprintf(host, sizeof(host), "%s/BASLUS-20184/SAVEDATA-02.tmp", pc_mc_root());
    f = fopen(host, "rb");
    CHECK(f == NULL);
    if (f != NULL)
        fclose(f);

    /* A file that is not there: an error the game shows, not a hang. */
    CHECK(read_file("SAVEDATA-99", back, sizeof(back)) == -1);
    CHECK(GetMemoryCardError(&mc) == 3);
    RecoveryMemoryCardError(&mc);
    CHECK(read_file(names[2], back, 964) == 1);

    /* Paths stay inside the card: ".." stops at its root. */
    {
        PcMcEntry list[4];

        CHECK(pc_mc_open(0, "/../../BASLUS-20184/SAVEDATA-00", 1) == 0);
        CHECK(pc_mc_close(0) == 0);
        CHECK(pc_mc_getdir(0, "/../*", 4, list) == 1);
        CHECK(strcmp(list[0].name, "BASLUS-20184") == 0 && list[0].is_dir);
    }

    if (failures == 0)
        printf("memory card: all checks passed\n");
    return failures != 0;
}
