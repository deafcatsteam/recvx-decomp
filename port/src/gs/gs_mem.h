/*
 * GS local memory (4 MB of video memory) with the PS2's pixel layouts.
 *
 * The GS stores pixels in pages of blocks of columns rather than in rows, with
 * a different arrangement per pixel format. The game manages this memory
 * itself (it places textures and CLUTs by block address and reads frame
 * buffers back as textures), so the layouts have to be exact.
 *
 * Addresses: bp is a block pointer (256-byte units), bw a buffer width in
 * units of 64 pixels, as in the GS registers.
 */
#ifndef GS_MEM_H
#define GS_MEM_H

#include <stdint.h>
#include <string.h>

#define GS_MEM_SIZE (4 * 1024 * 1024)

/* Pixel storage formats (PSM field values). */
enum {
    GS_PSMCT32 = 0x00, GS_PSMCT24 = 0x01, GS_PSMCT16 = 0x02, GS_PSMCT16S = 0x0a,
    GS_PSMT8 = 0x13, GS_PSMT4 = 0x14, GS_PSMT8H = 0x1b, GS_PSMT4HL = 0x24,
    GS_PSMT4HH = 0x2c, GS_PSMZ32 = 0x30, GS_PSMZ24 = 0x31, GS_PSMZ16 = 0x32,
    GS_PSMZ16S = 0x3a,
};

extern uint8_t gs_vram[GS_MEM_SIZE];

/*
 * Addresses. Inside a page every format has a fixed arrangement, and pages
 * follow each other as plain block numbers, so an address is
 * (bp + page * 32) * 256 plus the offset of (x, y) inside its page, taken
 * from these tables (built from the GS block and column layouts).
 */
extern uint16_t gs_page32[2][32 * 64];  /* CT32, Z32 (bytes) */
extern uint16_t gs_page16[4][64 * 64];  /* CT16, CT16S, Z16, Z16S (bytes) */
extern uint16_t gs_page8[64 * 128];     /* bytes */
extern uint16_t gs_page4[128 * 128];    /* nibbles */

/* Pages per buffer row, for formats with pages 64 or 128 pixels wide. */
#define GS_ROW64(bw) ((bw) ? (bw) : 1)
#define GS_ROW128(bw) ((bw) >= 2 ? (bw) >> 1 : 1)

/* Byte address of a 32-bit-word pixel; z selects the depth layout. */
static inline uint32_t gs_addr32(int z, uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t page = (uint32_t)(y >> 5) * GS_ROW64(bw) + (uint32_t)(x >> 6);
    return ((bp + page * 32) * 256 + gs_page32[z][(y & 31) * 64 + (x & 63)]) & (GS_MEM_SIZE - 1);
}

/* Byte address of a 16-bit pixel; k: 0 CT16, 1 CT16S, 2 Z16, 3 Z16S. */
static inline uint32_t gs_addr16(int k, uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t page = (uint32_t)(y >> 6) * GS_ROW64(bw) + (uint32_t)(x >> 6);
    return ((bp + page * 32) * 256 + gs_page16[k][(y & 63) * 64 + (x & 63)]) & (GS_MEM_SIZE - 1);
}

static inline uint32_t gs_addr8(uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t page = (uint32_t)(y >> 6) * GS_ROW128(bw) + (uint32_t)(x >> 7);
    return ((bp + page * 32) * 256 + gs_page8[(y & 63) * 128 + (x & 127)]) & (GS_MEM_SIZE - 1);
}

/* Nibble address of a 4-bit pixel. */
static inline uint32_t gs_addr4(uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t page = (uint32_t)(y >> 7) * GS_ROW128(bw) + (uint32_t)(x >> 7);
    return ((bp + page * 32) * 512 + gs_page4[(y & 127) * 128 + (x & 127)]) & (GS_MEM_SIZE * 2 - 1);
}

static inline uint32_t gs_rd32(uint32_t a) { uint32_t v; __builtin_memcpy(&v, gs_vram + a, 4); return v; }
static inline void gs_wr32(uint32_t a, uint32_t v) { __builtin_memcpy(gs_vram + a, &v, 4); }
static inline uint32_t gs_rd16(uint32_t a) { uint16_t v; __builtin_memcpy(&v, gs_vram + a, 2); return v; }
static inline void gs_wr16(uint32_t a, uint32_t v) { uint16_t h = (uint16_t)v; __builtin_memcpy(gs_vram + a, &h, 2); }

/* Raw pixel value of format psm at (x, y) of the buffer at bp/bw. 24-bit
 * formats return the low 24 bits, the 4/8-bit "H" formats the index. */
uint32_t gs_read_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y);

/* Stores a raw pixel value; for 24-bit and "H" formats only the bits of
 * that format are changed in the 32-bit word. */
void gs_write_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y, uint32_t v);

/* Bits per pixel of a storage format (32 for the 24-bit ones). */
int gs_psm_bpp(int psm);

#endif
