/* GS local memory layouts (see gs_mem.h and the GS User's Manual, chapter
 * "Local memory"). */
#include "gs_mem.h"

#include <string.h>

uint8_t gs_vram[GS_MEM_SIZE] __attribute__((aligned(64)));

/* Block number inside a page, by block row and column. */
static const uint8_t block32[4][8] = {
    { 0, 1, 4, 5, 16, 17, 20, 21 },
    { 2, 3, 6, 7, 18, 19, 22, 23 },
    { 8, 9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};
static const uint8_t block32z[4][8] = {
    { 24, 25, 28, 29, 8, 9, 12, 13 },
    { 26, 27, 30, 31, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 0, 1, 4, 5 },
    { 18, 19, 22, 23, 2, 3, 6, 7 },
};
static const uint8_t block16[8][4] = {
    { 0, 2, 8, 10 }, { 1, 3, 9, 11 }, { 4, 6, 12, 14 }, { 5, 7, 13, 15 },
    { 16, 18, 24, 26 }, { 17, 19, 25, 27 }, { 20, 22, 28, 30 }, { 21, 23, 29, 31 },
};
static const uint8_t block16s[8][4] = {
    { 0, 2, 16, 18 }, { 1, 3, 17, 19 }, { 8, 10, 24, 26 }, { 9, 11, 25, 27 },
    { 4, 6, 20, 22 }, { 5, 7, 21, 23 }, { 12, 14, 28, 30 }, { 13, 15, 29, 31 },
};
static const uint8_t block16z[8][4] = {
    { 24, 26, 16, 18 }, { 25, 27, 17, 19 }, { 28, 30, 20, 22 }, { 29, 31, 21, 23 },
    { 8, 10, 0, 2 }, { 9, 11, 1, 3 }, { 12, 14, 4, 6 }, { 13, 15, 5, 7 },
};
static const uint8_t block16sz[8][4] = {
    { 24, 26, 8, 10 }, { 25, 27, 9, 11 }, { 16, 18, 0, 2 }, { 17, 19, 1, 3 },
    { 28, 30, 12, 14 }, { 29, 31, 13, 15 }, { 20, 22, 4, 6 }, { 21, 23, 5, 7 },
};
#define block8 block32
#define block4 block16

/* Position of a pixel inside its block, in units of the pixel size. */
static const uint8_t column32[8][8] = {
    { 0, 1, 4, 5, 8, 9, 12, 13 },
    { 2, 3, 6, 7, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 24, 25, 28, 29 },
    { 18, 19, 22, 23, 26, 27, 30, 31 },
    { 32, 33, 36, 37, 40, 41, 44, 45 },
    { 34, 35, 38, 39, 42, 43, 46, 47 },
    { 48, 49, 52, 53, 56, 57, 60, 61 },
    { 50, 51, 54, 55, 58, 59, 62, 63 },
};
static const uint8_t column16[8][16] = {
    { 0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27 },
    { 4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31 },
    { 32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59 },
    { 36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63 },
    { 64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91 },
    { 68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95 },
    { 96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123 },
    { 100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127 },
};

/*
 * 8- and 4-bit pixels live inside the 32-bit column layout: a block column
 * of 16x4 (8-bit) or 32x4 (4-bit) pixels covers the same 16 words as a
 * 8x2 area of 32-bit pixels. Pixel x selects the word through column32
 * (x & 7) and the byte or nibble inside it (x >> 3); rows 2-3 of a column
 * take the upper half of that byte/nibble pair. Every other row pair also
 * swaps the two halves of the word row, alternately per column.
 */
static int column_word(int y, int x)
{
    int col = (y >> 2) & 3; /* column inside the block */
    int row = y & 3;        /* row inside the column */
    int swap = ((row >> 1) ^ col) & 1;

    return column32[col * 2 + (row & 1)][(x & 7) ^ (swap ? 4 : 0)];
}

/* Byte offset of an 8-bit pixel inside its 16x16 block. */
static int column8_offset(int x, int y)
{
    return column_word(y, x) * 4 + ((x >> 3) & 1) * 2 + ((y >> 1) & 1);
}

/* Nibble offset of a 4-bit pixel inside its 32x16 block. */
static int column4_offset(int x, int y)
{
    return column_word(y, x) * 8 + ((x >> 3) & 3) * 2 + ((y >> 1) & 1);
}

/* Block number of (x, y) for a format with the given page and block sizes:
 * blocks are added to bp as plain numbers, like the GS does. */
static uint32_t block_number(uint32_t bp, uint32_t bw, int x, int y, int page_w, int page_h,
                             int block_w, int block_h, const uint8_t *table, int table_w)
{
    uint32_t pages_per_row = (bw * 64) / page_w;
    uint32_t page;

    if (pages_per_row == 0)
        pages_per_row = 1;
    page = (uint32_t)(y / page_h) * pages_per_row + (uint32_t)(x / page_w);
    return (bp + page * 32 + table[((y % page_h) / block_h) * table_w + (x % page_w) / block_w]) & 0x3fff;
}

int gs_psm_bpp(int psm)
{
    switch (psm) {
    case GS_PSMCT16: case GS_PSMCT16S: case GS_PSMZ16: case GS_PSMZ16S:
        return 16;
    case GS_PSMT8:
        return 8;
    case GS_PSMT4:
        return 4;
    default:
        return 32; /* 32/24-bit and the H formats, stored in 32-bit words */
    }
}

/* Byte address of a 32-bit-word pixel. */
static uint32_t addr32(const uint8_t *table, uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t block = block_number(bp, bw, x, y, 64, 32, 8, 8, table, 8);
    return block * 256 + column32[y & 7][x & 7] * 4;
}

/* Byte address of a 16-bit pixel. */
static uint32_t addr16(const uint8_t *table, uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t block = block_number(bp, bw, x, y, 64, 64, 16, 8, table, 4);
    return block * 256 + column16[y & 7][x & 15] * 2;
}

static uint32_t addr8(uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t block = block_number(bp, bw, x, y, 128, 64, 16, 16, &block8[0][0], 8);
    return block * 256 + column8_offset(x & 15, y & 15);
}

/* Nibble address of a 4-bit pixel. */
static uint32_t addr4(uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t block = block_number(bp, bw, x, y, 128, 128, 32, 16, &block4[0][0], 4);
    return block * 512 + column4_offset(x & 31, y & 15);
}

static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, gs_vram + a, 4); return v; }
static void wr32(uint32_t a, uint32_t v) { memcpy(gs_vram + a, &v, 4); }
static uint32_t rd16(uint32_t a) { uint16_t v; memcpy(&v, gs_vram + a, 2); return v; }
static void wr16(uint32_t a, uint32_t v) { uint16_t h = (uint16_t)v; memcpy(gs_vram + a, &h, 2); }

uint32_t gs_read_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y)
{
    switch (psm) {
    case GS_PSMCT32: return rd32(addr32(&block32[0][0], bp, bw, x, y));
    case GS_PSMCT24: return rd32(addr32(&block32[0][0], bp, bw, x, y)) & 0xffffff;
    case GS_PSMZ32: return rd32(addr32(&block32z[0][0], bp, bw, x, y));
    case GS_PSMZ24: return rd32(addr32(&block32z[0][0], bp, bw, x, y)) & 0xffffff;
    case GS_PSMCT16: return rd16(addr16(&block16[0][0], bp, bw, x, y));
    case GS_PSMCT16S: return rd16(addr16(&block16s[0][0], bp, bw, x, y));
    case GS_PSMZ16: return rd16(addr16(&block16z[0][0], bp, bw, x, y));
    case GS_PSMZ16S: return rd16(addr16(&block16sz[0][0], bp, bw, x, y));
    case GS_PSMT8: return gs_vram[addr8(bp, bw, x, y)];
    case GS_PSMT4: {
        uint32_t n = addr4(bp, bw, x, y);
        return (gs_vram[n >> 1] >> ((n & 1) * 4)) & 15;
    }
    case GS_PSMT8H: return rd32(addr32(&block32[0][0], bp, bw, x, y)) >> 24;
    case GS_PSMT4HL: return (rd32(addr32(&block32[0][0], bp, bw, x, y)) >> 24) & 15;
    case GS_PSMT4HH: return rd32(addr32(&block32[0][0], bp, bw, x, y)) >> 28;
    }
    return 0;
}

void gs_write_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y, uint32_t v)
{
    uint32_t a;

    switch (psm) {
    case GS_PSMCT32: wr32(addr32(&block32[0][0], bp, bw, x, y), v); return;
    case GS_PSMZ32: wr32(addr32(&block32z[0][0], bp, bw, x, y), v); return;
    case GS_PSMCT24:
        a = addr32(&block32[0][0], bp, bw, x, y);
        wr32(a, (rd32(a) & 0xff000000) | (v & 0xffffff));
        return;
    case GS_PSMZ24:
        a = addr32(&block32z[0][0], bp, bw, x, y);
        wr32(a, (rd32(a) & 0xff000000) | (v & 0xffffff));
        return;
    case GS_PSMCT16: wr16(addr16(&block16[0][0], bp, bw, x, y), v); return;
    case GS_PSMCT16S: wr16(addr16(&block16s[0][0], bp, bw, x, y), v); return;
    case GS_PSMZ16: wr16(addr16(&block16z[0][0], bp, bw, x, y), v); return;
    case GS_PSMZ16S: wr16(addr16(&block16sz[0][0], bp, bw, x, y), v); return;
    case GS_PSMT8: gs_vram[addr8(bp, bw, x, y)] = (uint8_t)v; return;
    case GS_PSMT4: {
        uint32_t n = addr4(bp, bw, x, y);
        int shift = (n & 1) * 4;
        gs_vram[n >> 1] = (uint8_t)((gs_vram[n >> 1] & ~(15 << shift)) | ((v & 15) << shift));
        return;
    }
    case GS_PSMT8H:
        a = addr32(&block32[0][0], bp, bw, x, y);
        wr32(a, (rd32(a) & 0x00ffffff) | (v << 24));
        return;
    case GS_PSMT4HL:
        a = addr32(&block32[0][0], bp, bw, x, y);
        wr32(a, (rd32(a) & 0xf0ffffff) | ((v & 15) << 24));
        return;
    case GS_PSMT4HH:
        a = addr32(&block32[0][0], bp, bw, x, y);
        wr32(a, (rd32(a) & 0x0fffffff) | ((v & 15) << 28));
        return;
    }
}

#ifdef GS_MEM_SELFTEST
/* Dumps the generated layout tables, for comparing with a reference. */
#include <stdio.h>
int main(void)
{
    for (int y = 0; y < 16; y++) { for (int x = 0; x < 16; x++) printf("%d ", column8_offset(x, y)); printf("\n"); }
    for (int y = 0; y < 16; y++) { for (int x = 0; x < 32; x++) printf("%d ", column4_offset(x, y)); printf("\n"); }
    return 0;
}
#endif
