/*
 * tex_dump.c — debug texture dumper for game_texture_stubs.c.
 *
 * Lives in recvx_port_stubs (NOT recvx_game) so we get the real MSVC CRT
 * stdio without KATANA's SH-series stdio shadow blocking the FILE typedef
 * via the recvx_game_prelude.h /FI guards. game_texture_stubs.c calls in
 * via an extern declaration.
 *
 * Output: 32-bit BGRA uncompressed TGA at port/debug/tex/tex_NN_WxH.tga
 * (relative to the exe's working directory). Gated by env RECVX_DUMP_TEX=1
 * — off by default so normal runs don't spam files. Re-decoding the same
 * slot just overwrites the file; latest TEXLIST wins.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>     /* getenv */
#include <direct.h>     /* _mkdir on MSVC */

extern void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)

static int dump_enabled(void) {
    static int checked = 0;
    static int on      = 0;
    if (!checked) {
        const char* e = getenv("RECVX_DUMP_TEX");
        on = (e && e[0] && e[0] != '0');
        if (on) {
            (void)_mkdir("port");
            (void)_mkdir("port/debug");
            (void)_mkdir("port/debug/tex");
            RX_LOG("dump", "RECVX_DUMP_TEX on -> port/debug/tex/");
        }
        checked = 1;
    }
    return on;
}

void recvx_dump_rgba_tga(int slot, int w, int h, const void* rgba_in) {
    if (!dump_enabled() || !rgba_in || w <= 0 || h <= 0) return;
    const uint8_t* rgba = (const uint8_t*)rgba_in;

    char path[160];
    snprintf(path, sizeof(path), "port/debug/tex/tex_%02d_%dx%d.tga",
             slot, w, h);
    FILE* f = fopen(path, "wb");
    if (!f) { RX_LOG("dump", "open %s FAILED", path); return; }

    /* TGA header: image type 2 (uncompressed true-color), 32 bpp BGRA,
     * top-left origin (descriptor 0x28 = top-down + 8 alpha bits). */
    uint8_t hdr[18] = {0};
    hdr[2]  = 2;
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)((w >> 8) & 0xFF);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)((h >> 8) & 0xFF);
    hdr[16] = 32;
    hdr[17] = 0x28;
    fwrite(hdr, 1, sizeof(hdr), f);

    /* Swap RGBA -> BGRA in chunks so we don't need a w*h*4 staging buffer. */
    enum { CHUNK = 4096 };
    static uint8_t buf[CHUNK * 4];
    int total = w * h, i = 0;
    while (i < total) {
        int n = (total - i) < CHUNK ? (total - i) : CHUNK;
        for (int k = 0; k < n; ++k) {
            buf[k*4+0] = rgba[(i+k)*4+2];
            buf[k*4+1] = rgba[(i+k)*4+1];
            buf[k*4+2] = rgba[(i+k)*4+0];
            buf[k*4+3] = rgba[(i+k)*4+3];
        }
        fwrite(buf, 4, (size_t)n, f);
        i += n;
    }
    fclose(f);
    RX_LOG("dump", "wrote %s", path);
}
