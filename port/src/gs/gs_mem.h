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

#define GS_MEM_SIZE (4 * 1024 * 1024)

/* Pixel storage formats (PSM field values). */
enum {
    GS_PSMCT32 = 0x00, GS_PSMCT24 = 0x01, GS_PSMCT16 = 0x02, GS_PSMCT16S = 0x0a,
    GS_PSMT8 = 0x13, GS_PSMT4 = 0x14, GS_PSMT8H = 0x1b, GS_PSMT4HL = 0x24,
    GS_PSMT4HH = 0x2c, GS_PSMZ32 = 0x30, GS_PSMZ24 = 0x31, GS_PSMZ16 = 0x32,
    GS_PSMZ16S = 0x3a,
};

extern uint8_t gs_vram[GS_MEM_SIZE];

/* Raw pixel value of format psm at (x, y) of the buffer at bp/bw. 24-bit
 * formats return the low 24 bits, the 4/8-bit "H" formats the index. */
uint32_t gs_read_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y);

/* Stores a raw pixel value; for 24-bit and "H" formats only the bits of
 * that format are changed in the 32-bit word. */
void gs_write_pixel(int psm, uint32_t bp, uint32_t bw, int x, int y, uint32_t v);

/* Bits per pixel of a storage format (32 for the 24-bit ones). */
int gs_psm_bpp(int psm);

#endif
