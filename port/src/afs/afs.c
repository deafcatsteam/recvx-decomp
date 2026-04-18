#include "recvx_afs.h"
#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct recvx_afs {
    FILE*     fp;
    unsigned  count;
    uint32_t* off;
    uint32_t* sz;
};

static uint32_t rd_u32_le(const unsigned char* p) {
    return  (uint32_t)p[0]         |
           ((uint32_t)p[1] <<  8)  |
           ((uint32_t)p[2] << 16)  |
           ((uint32_t)p[3] << 24);
}

recvx_afs_t* recvx_afs_open(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        RX_LOG("afs", "open FAIL: %s", path);
        return NULL;
    }
    unsigned char hdr[8];
    if (fread(hdr, 1, 8, fp) != 8 || memcmp(hdr, "AFS\0", 4) != 0) {
        RX_LOG("afs", "bad magic in %s", path);
        fclose(fp);
        return NULL;
    }
    unsigned count = rd_u32_le(hdr + 4);
    if (count == 0 || count > 65536) {
        RX_LOG("afs", "bogus entry_count=%u in %s", count, path);
        fclose(fp);
        return NULL;
    }

    size_t toc_bytes = (size_t)count * 8;
    unsigned char* toc = (unsigned char*)malloc(toc_bytes);
    if (!toc || fread(toc, 1, toc_bytes, fp) != toc_bytes) {
        free(toc);
        fclose(fp);
        return NULL;
    }

    recvx_afs_t* a = (recvx_afs_t*)calloc(1, sizeof *a);
    a->fp    = fp;
    a->count = count;
    a->off   = (uint32_t*)malloc(count * sizeof(uint32_t));
    a->sz    = (uint32_t*)malloc(count * sizeof(uint32_t));
    for (unsigned i = 0; i < count; ++i) {
        a->off[i] = rd_u32_le(toc + i * 8 + 0);
        a->sz [i] = rd_u32_le(toc + i * 8 + 4);
    }
    free(toc);
    RX_LOG("afs", "opened %s: %u entries", path, count);
    return a;
}

void recvx_afs_close(recvx_afs_t* a) {
    if (!a) return;
    if (a->fp) fclose(a->fp);
    free(a->off);
    free(a->sz);
    free(a);
}

unsigned recvx_afs_count(const recvx_afs_t* a) { return a ? a->count : 0; }

uint32_t recvx_afs_entry_size(const recvx_afs_t* a, unsigned idx) {
    if (!a || idx >= a->count) return 0;
    return a->sz[idx];
}

uint32_t recvx_afs_read(recvx_afs_t* a, unsigned idx, void* dst) {
    if (!a || !dst || idx >= a->count) return 0;
    uint32_t sz = a->sz[idx];
    if (!sz) return 0;
    if (fseek(a->fp, (long)a->off[idx], SEEK_SET) != 0) return 0;
    size_t got = fread(dst, 1, sz, a->fp);
    return (uint32_t)got;
}
