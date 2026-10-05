/*
 * Software emulation of the PS2 Graphics Synthesizer (GS).
 *
 * The game drives the GS directly: it uploads textures and CLUTs into GS
 * memory, sends GIF packets (register writes and primitives) through DMA, and
 * displays a frame buffer that lives in GS memory. This module interprets
 * that traffic and renders into its own copy of GS memory (gs_mem.h); the
 * window layer then shows the displayed frame buffer.
 *
 * Everything here is plain C with no game headers.
 */
#ifndef GS_H
#define GS_H

#include <stdint.h>

/* Clears GS memory and registers. */
void gs_reset(void);

/* Feeds GIF data (PATH3): count quadwords, each as two 64-bit halves. */
void gs_gif_write(const uint64_t *qwords, uint32_t count);

/* Writes one GS register by its A+D address (0x00-0x63). */
void gs_write_reg(int addr, uint64_t value);

/* Runs a DMA source chain on the GIF channel starting at the tag at tadr,
 * a PS2 address as the game wrote it (see gs_dma_pointer). */
void gs_dma_gif_chain(uint32_t tadr);

/* Sends qwc quadwords from a PS2 address to the GIF (normal DMA). */
void gs_dma_gif_normal(uint32_t madr, uint32_t qwc);

/* Translates a PS2 address used in DMA to a host pointer: scratchpad
 * addresses (bit 31, or 0x70000000) map to the port's scratchpad, others are
 * host addresses below 256 MB. */
void *gs_dma_pointer(uint32_t addr);

/* Display circuit settings (PMODE, DISPFB1/2, DISPLAY1/2, BGCOLOR). */
void gs_set_display(uint64_t dispfb, uint64_t display);

/* Converts the displayed frame buffer into 32-bit RGBA (R in the low byte,
 * alpha forced opaque). Returns its size through w and h; dst must hold
 * GS_DISPLAY_MAX_W * GS_DISPLAY_MAX_H pixels. Returns 0, without converting,
 * when nothing was drawn or displayed since the last call. */
#define GS_DISPLAY_MAX_W 1024
#define GS_DISPLAY_MAX_H 1024
int gs_read_display(uint32_t *dst, int *w, int *h);

/* Statistics of the last frame, for debugging. */
typedef struct {
    uint32_t prims;
    uint32_t pixels;
    uint32_t uploads;
    uint32_t chains;
} GsStats;
extern GsStats gs_stats;

/* One-line summary of the GS activity since the last call (counters are then
 * reset) and of what the display shows. */
void gs_debug_status(char *buf, int size);

#endif
