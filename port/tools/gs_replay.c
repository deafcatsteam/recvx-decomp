/*
 * Draws a GS frame dump (F10 in the game, see gs_dump_frame) again and saves
 * what the display shows as a BMP picture:
 *
 *   gs_replay cvx_gsdump_000.bin out.bmp [page]
 *
 * The display often shows a copy of the previous frame (drawn by the game,
 * not by this replay); page shows a 640-pixel-wide CT32 buffer at that frame
 * buffer page instead, such as the one the frame is drawn into.
 */
#include "../src/gs/gs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

unsigned char port_scratchpad[0x4000];

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int main(int argc, char **argv)
{
    static uint32_t pixels[GS_DISPLAY_MAX_W * GS_DISPLAY_MAX_H];
    int w, h;
    FILE *f;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: gs_replay DUMP OUT.bmp [PAGE]\n");
        return 2;
    }
    gs_reset();
    if (gs_replay(argv[1]) != 0) {
        fprintf(stderr, "gs_replay: cannot read %s (or made by another version)\n", argv[1]);
        return 1;
    }
    if (argc == 4)
        gs_set_display((uint64_t)atoi(argv[3]) | (10ull << 9), (2559ull << 32) | (479ull << 44));
    gs_read_display(pixels, &w, &h);
    f = fopen(argv[2], "wb");
    if (f == NULL)
        return 1;
    /* 32-bit BMP, bottom-up rows of B, G, R, A */
    fputc('B', f); fputc('M', f);
    put32(f, 54 + w * h * 4); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, w); put32(f, h); put16(f, 1); put16(f, 32);
    put32(f, 0); put32(f, w * h * 4); put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) {
            uint32_t c = pixels[y * w + x];
            fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); fputc(255, f);
        }
    fclose(f);
    printf("gs_replay: %dx%d picture saved to %s (%u primitives)\n", w, h, argv[2], gs_stats.prims);
    return 0;
}
