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
    int64_t busy_ns; /* time spent in GIF transfers and drawing */
} GsStats;
extern GsStats gs_stats;

/* ---- Drawing a frame again (60 images a second, port/src/game/pc_interp.c)
 *
 * gs_rec_start keeps the GS state and memory as they are, and records the
 * GIF data sent from then on, until gs_rec_stop. While recording, the
 * three quadwords of each vertex sent as ST, RGBAQ, XYZF2 (the game's 3D
 * strips) are given to gs_rec_vertex_id, which returns an id for the
 * vertices it knows, or -1. gs_rec_replay then draws the recorded frame
 * again from the kept state, with patch(id, words) changing the vertices
 * that have an id (12 words: S T Q -, R G B A, X Y Z F), keeps the picture
 * displayed by the result aside, and puts the GS state, memory and
 * displayed picture back as they were before the replay. gs_show_held(1)
 * shows that picture instead of the display (gs_read_display and
 * gs_gpu_present) until gs_show_held(0). */
extern int (*gs_rec_vertex_id)(const uint32_t words[12]);
void gs_rec_start(void);
void gs_rec_stop(void);
int gs_rec_vertices(void); /* vertices with an id in the recording */
int gs_rec_replay(void (*patch)(int id, uint32_t words[12]));
void gs_show_held(int on);

/* Frame dumps, for reproducing a picture away from the game: gs_dump_frame
 * asks for the next frame to be written to path (the GS state, its memory,
 * then everything sent to it until the following V-blank); call
 * gs_dump_vblank at every V-blank. gs_replay draws a dump again and returns
 * 0, or -1 if the file cannot be read. */
void gs_dump_frame(const char *path);
void gs_dump_vblank(void);
int gs_replay(const char *path);

/* Clock used for busy_ns (nanoseconds); not measured when NULL. */
extern int64_t (*gs_clock_ns)(void);

/* One-line summary of the GS activity since the last call (counters are then
 * reset) and of what the display shows. */
void gs_debug_status(char *buf, int size);

#endif
