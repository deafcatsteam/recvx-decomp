/*
 * PC replacement for the CRI ADX middleware (libadxe).
 *
 * ADXF is CRI's file layer: the game loads all of its data through it, from
 * AFS archives ("partitions") and plain files on the disc. It is implemented
 * here on top of the disc image reader, synchronously.
 *
 * ADXT is the ADX audio stream player. It is silent for now; the handles it
 * returns are valid so the game's bookkeeping works.
 * TODO: decode ADX streams (vgmstream) and play them through SDL/OpenAL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cri_adxf.h"
#include "cri_adxt.h"

#include "../platform/pc_disc.h"

#define PC_ADXF_MAX_PT 256
#define PC_ADXF_MAX_HANDLES 64

typedef struct {
    unsigned int offset; /* bytes from the start of the archive */
    unsigned int size;
} PcAfsEntry;

typedef struct {
    int loaded;
    unsigned int lsn; /* first sector of the archive */
    unsigned int count;
    PcAfsEntry *entries;
} PcAfsPartition;

typedef struct {
    int used;
    uint64_t offset; /* bytes from the start of the disc */
    unsigned int size;
    Sint32 stat;
} PcAdxf;

static PcAfsPartition partitions[PC_ADXF_MAX_PT];
static PcAdxf handles[PC_ADXF_MAX_HANDLES];

static unsigned int le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* ---- ADXF: files and AFS partitions ---------------------------------- */

Sint32 ADXF_LoadPartitionNw(Sint32 ptid, Char8 *fname, void *dir, void *ptinfo)
{
    PcAfsPartition *pt;
    unsigned char head[8];
    unsigned char *table;
    unsigned int lsn, size, i;

    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT)
        return -1;
    pt = &partitions[ptid];
    free(pt->entries);
    memset(pt, 0, sizeof(*pt));

    if (!pc_disc_find(fname, &lsn, &size)) {
        fprintf(stderr, "ADXF: partition %d: '%s' not found on the disc\n", (int)ptid, fname);
        return -1;
    }
    /* CRI only checks the first three bytes: the fourth is not always 0. */
    if (!pc_disc_read_bytes((uint64_t)lsn * PC_DISC_SECTOR, 8, head)
        || memcmp(head, "AFS", 3) != 0) {
        fprintf(stderr, "ADXF: '%s' is not an AFS archive (starts with %02x %02x %02x %02x)\n",
                fname, head[0], head[1], head[2], head[3]);
        return -1;
    }

    pt->lsn = lsn;
    pt->count = le32(head + 4);
    pt->entries = calloc(pt->count ? pt->count : 1, sizeof(PcAfsEntry));
    table = malloc(pt->count * 8 + 1);
    if (!pc_disc_read_bytes((uint64_t)lsn * PC_DISC_SECTOR + 8, pt->count * 8, table)) {
        free(table);
        return -1;
    }
    for (i = 0; i < pt->count; i++) {
        pt->entries[i].offset = le32(table + i * 8);
        pt->entries[i].size = le32(table + i * 8 + 4);
    }
    free(table);
    pt->loaded = 1;
    return 0;
}

Sint32 ADXF_GetPtStat(Sint32 ptid)
{
    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT || !partitions[ptid].loaded)
        return ADXF_STAT_ERROR;
    return ADXF_STAT_READEND;
}

static ADXF new_handle(uint64_t offset, unsigned int size)
{
    int i;

    for (i = 0; i < PC_ADXF_MAX_HANDLES; i++) {
        if (!handles[i].used) {
            handles[i].used = 1;
            handles[i].offset = offset;
            handles[i].size = size;
            handles[i].stat = ADXF_STAT_STOP;
            return (ADXF)&handles[i];
        }
    }
    fprintf(stderr, "ADXF: out of file handles\n");
    return NULL;
}

ADXF ADXF_OpenAfs(Sint32 ptid, Sint32 flid)
{
    PcAfsPartition *pt;

    if (ptid < 0 || ptid >= PC_ADXF_MAX_PT || !partitions[ptid].loaded)
        return NULL;
    pt = &partitions[ptid];
    if (flid < 0 || (unsigned int)flid >= pt->count)
        return NULL;
    return new_handle((uint64_t)pt->lsn * PC_DISC_SECTOR + pt->entries[flid].offset,
                      pt->entries[flid].size);
}

ADXF ADXF_Open(Char8 *fname, void *atr)
{
    unsigned int lsn, size;

    if (!pc_disc_find(fname, &lsn, &size)) {
        fprintf(stderr, "ADXF: '%s' not found on the disc\n", fname);
        return NULL;
    }
    return new_handle((uint64_t)lsn * PC_DISC_SECTOR, size);
}

void ADXF_Close(ADXF adxf)
{
    if (adxf != NULL)
        ((PcAdxf *)adxf)->used = 0;
}

Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void *buf)
{
    PcAdxf *f = (PcAdxf *)adxf;
    unsigned int want;

    if (f == NULL)
        return 0;
    want = (unsigned int)nsct * PC_DISC_SECTOR;
    if (want > f->size)
        want = f->size;
    f->stat = pc_disc_read_bytes(f->offset, want, buf) ? ADXF_STAT_READEND : ADXF_STAT_ERROR;
    return nsct;
}

Sint32 ADXF_Stop(ADXF adxf)
{
    if (adxf != NULL)
        ((PcAdxf *)adxf)->stat = ADXF_STAT_STOP;
    return 0;
}

Sint32 ADXF_GetStat(ADXF adxf)
{
    return adxf != NULL ? ((PcAdxf *)adxf)->stat : ADXF_STAT_ERROR;
}

Sint32 ADXF_GetFsizeByte(ADXF adxf)
{
    return adxf != NULL ? (Sint32)((PcAdxf *)adxf)->size : 0;
}

Sint32 ADXF_GetFsizeSct(ADXF adxf)
{
    return adxf != NULL ? (Sint32)((((PcAdxf *)adxf)->size + PC_DISC_SECTOR - 1) / PC_DISC_SECTOR) : 0;
}

/* ---- ADXT: audio streams (silent for now) ----------------------------- */

typedef struct {
    int used;
    Sint32 stat;
} PcAdxt;

static PcAdxt players[8];

void ADXT_SetupHostFs(void *sprm) { (void)sprm; }
void ADXT_SetupDvdFs(void *sprm) { (void)sprm; pc_disc_open(); }
void ADXPS2_SetupThrd(void *tprm) { (void)tprm; }
void ADXPS2_Lock(void) {}
void ADXPS2_Unlock(void) {}

void ADXT_Init(void) { memset(players, 0, sizeof(players)); }
void ADXT_Finish(void) {}
void ADXT_SetNumRetry(Sint32 num) { (void)num; }
void ADXT_SetOutputMono(Sint32 flag) { (void)flag; }

ADXT ADXT_Create(Sint32 maxnch, void *work, Sint32 worksize)
{
    int i;

    for (i = 0; i < 8; i++) {
        if (!players[i].used) {
            players[i].used = 1;
            players[i].stat = ADXT_STAT_STOP;
            return (ADXT)&players[i];
        }
    }
    return NULL;
}

void ADXT_Destroy(ADXT adxt)
{
    if (adxt != NULL)
        ((PcAdxt *)adxt)->used = 0;
}

void ADXT_StartAfs(ADXT adxt, Sint32 patid, Sint32 fid)
{
    /* Nothing is decoded yet: report the stream as already finished so the
     * game never waits on it. */
    if (adxt != NULL)
        ((PcAdxt *)adxt)->stat = ADXT_STAT_PLAYEND;
}

void ADXT_Stop(ADXT adxt)
{
    if (adxt != NULL)
        ((PcAdxt *)adxt)->stat = ADXT_STAT_STOP;
}

Sint32 ADXT_GetStat(ADXT adxt)
{
    return adxt != NULL ? ((PcAdxt *)adxt)->stat : ADXT_STAT_STOP;
}

void ADXT_GetTime(ADXT adxt, Sint32 *ncount, Sint32 *tscale)
{
    *ncount = 0;
    *tscale = 44100;
}

void ADXT_SetAutoRcvr(ADXT adxt, Sint32 rmode) {}
void ADXT_SetOutVol(ADXT adxt, Sint32 vol) {}
void ADXT_SetReloadSct(ADXT adxt, Sint32 minsct) {}
void ADXT_SetSvrFreq(ADXT adxt, Sint32 freq) {}
