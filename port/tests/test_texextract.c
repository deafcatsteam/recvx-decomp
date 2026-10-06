/*
 * Texture export from the disc's files (pc_texextract.c): a PVP pack made
 * like the game's (a palette list, a 4-bit picture using a palette bank and
 * an 8-bit one with its own palette) is put in the middle of other data;
 * both pictures must be found and exported under the fingerprint of the
 * picture the game would draw: palette entries in the files' order, alpha
 * set up as the game does (black transparent for bank palettes, halved).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs.h"
#include "../src/gs/gs_texrep.h"

int pc_extract_textures_in(const void *bytes, uint32_t size, int compressed);

static unsigned char file[256 * 1024];
static uint32_t pos;

static void w32(uint32_t v)
{
    memcpy(file + pos, &v, 4);
    pos += 4;
}

static void chunk(uint32_t code, uint32_t size)
{
    w32(code);
    w32(size);
    for (int i = 0; i < 6; i++)
        w32(0);
}

/* A TIM2 picture with the game's headers; returns where its image goes. */
static uint32_t tim2(int type, int pict, int w, int h, uint32_t image, uint32_t clut, int colours)
{
    static uint32_t gindex = 4321;
    uint32_t t = pos;

    memset(file + t, 0, 0x100);
    memcpy(file + t, "TIM2", 4);
    file[t + 4] = 4;
    file[t + 5] = 1;
    file[t + 6] = 1;
    memcpy(file + t + 8, &gindex, 4); /* Gindex, one per picture */
    gindex++;
    file[t + 0x0C] = 6;                           /* ARGB8888 */
    memcpy(file + t + 0x80, &(uint32_t){ 0x80 + image + clut }, 4);
    memcpy(file + t + 0x84, &clut, 4);
    memcpy(file + t + 0x88, &image, 4);
    memcpy(file + t + 0x8C, &(uint16_t){ 0x80 }, 2);
    memcpy(file + t + 0x8E, &(uint16_t){ (uint16_t)colours }, 2);
    file[t + 0x90] = (unsigned char)pict;
    file[t + 0x92] = clut != 0 ? 3 : 0; /* ClutType: RGB32 */
    file[t + 0x93] = (unsigned char)type;
    memcpy(file + t + 0x94, &(uint16_t){ (uint16_t)w }, 2);
    memcpy(file + t + 0x96, &(uint16_t){ (uint16_t)h }, 2);
    pos = t + 0x100 + image + clut;
    return t + 0x100;
}

static uint32_t colour(int i, int k)
{
    return (uint32_t)((i * 9 + k) & 255) | (uint32_t)((255 - i * 5) & 255) << 8 |
           (uint32_t)((i * 3 + k) & 255) << 16 | (uint32_t)((i * 16 + 15) & 255) << 24;
}

static uint32_t halved(uint32_t c)
{
    return (c & 0xffffff) | ((((c >> 24) + 1) >> 1) << 24);
}

/* Packs data as the rooms are (Expand), with literal bytes only: a bit 1
 * before each byte, then the end (bits 0 and 1, offset 0). Descriptor
 * bytes come where the decoder reads them: when it needs a ninth bit. */
static unsigned char packed[300 * 1024];
static uint32_t pk, desc;
static int used = 8;

static void bit(int b)
{
    if (used == 8) {
        desc = pk++;
        packed[desc] = 0;
        used = 0;
    }
    packed[desc] |= (unsigned char)(b << used++);
}

static uint32_t pack(const unsigned char *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        bit(1);
        packed[pk++] = src[i];
    }
    bit(0);
    bit(1);
    packed[pk++] = 0;
    packed[pk++] = 0;
    return pk;
}

int main(void)
{
    static uint32_t want4[16 * 16], want8[32 * 16], want_big[256 * 256];
    char name[256];
    int bad = 0, n;
    uint64_t h;

    if (system("rm -rf test_textures_disc") != 0)
        return 1;
    texrep_set(TEXREP_DUMP, "test_textures_disc");

    memset(file, 0x5a, 4096); /* other data before */
    pos = 4096;

    /* The palette list: one bank palette, bank 5, from colour 0 */
    chunk(0x00494C50u, 12);
    w32(1);
    w32(0);
    w32(0 | 5 << 8 | 0 << 16);

    /* 16x16, 4 bits, bank palette (PictFormat 5): 32 entries in the file,
     * the colours are 0-7 and 16-23; black ones become transparent */
    uint32_t start = pos;
    chunk(0x324D4954u, 0);
    uint32_t img = tim2(4, 5, 16, 16, 128, 128, 32);
    memcpy(file + start + 4, &(uint32_t){ pos - start - 32 }, 4);
    for (int i = 0; i < 128; i++)
        file[img + i] = (unsigned char)((i * 2 & 15) | ((i * 2 + 1) * 7 & 15) << 4);
    for (int k = 0; k < 32; k++) {
        uint32_t c = k == 3 ? 0x80000000u : colour(k, 1);
        memcpy(file + img + 128 + k * 4, &c, 4);
    }
    for (int i = 0; i < 256; i++) {
        int idx = (file[img + i / 2] >> ((i & 1) * 4)) & 15;
        int at = idx < 8 ? idx : idx + 8;
        uint32_t c = at == 3 ? 0 : halved(colour(at, 1));
        want4[i] = c;
    }

    /* 32x16, 8 bits, its own palette (PictFormat 0), stored with bits 3
     * and 4 of the index swapped (CSM1) */
    start = pos;
    chunk(0x324D4954u, 0);
    img = tim2(5, 0, 32, 16, 512, 1024, 256);
    memcpy(file + start + 4, &(uint32_t){ pos - start - 32 }, 4);
    for (int i = 0; i < 512; i++)
        file[img + i] = (unsigned char)(i * 37 + 11);
    for (int k = 0; k < 256; k++) {
        int idx = (k & ~0x18) | ((k & 8) << 1) | ((k & 16) >> 1);
        uint32_t c = colour(idx, 2);
        memcpy(file + img + 512 + k * 4, &c, 4);
    }
    for (int i = 0; i < 512; i++)
        want8[i] = halved(colour((unsigned char)(i * 37 + 11), 2));
    /* 256x256, 8 bits, own palette: sent rearranged as 32-bit texels
     * (Ps2PxlconvCheck, P32_Image_Load) */
    start = pos;
    chunk(0x324D4954u, 0);
    img = tim2(5, 0, 256, 256, 65536, 1024, 256);
    memcpy(file + start + 4, &(uint32_t){ pos - start - 32 }, 4);
    for (int i = 0; i < 65536; i++)
        file[img + i] = (unsigned char)((i & 255) ^ ((i >> 8) * 3));
    for (int k = 0; k < 256; k++) {
        int idx = (k & ~0x18) | ((k & 8) << 1) | ((k & 16) >> 1);
        uint32_t c = colour(idx, 3);
        memcpy(file + img + 65536 + k * 4, &c, 4);
    }
    for (int i = 0; i < 65536; i++)
        want_big[i] = halved(colour((unsigned char)((i & 255) ^ ((i >> 8) * 3)), 3));
    chunk(0xFFFFFFFFu, 0);
    memset(file + pos, 0xa5, 4096); /* other data after */
    pos += 4096;

    n = pc_extract_textures_in(file, pos, 0);
    printf("textures found: %d\n", n);
    bad |= n != 3;
    h = texrep_hash(want4, 16, 16);
    snprintf(name, sizeof(name), "test_textures_disc/dump/16x16_%08x%08x.png", (unsigned)(h >> 32), (unsigned)h);
    FILE *f = fopen(name, "rb");
    printf("%s: %s\n", name, f != NULL ? "exported" : "missing");
    bad |= f == NULL;
    if (f != NULL)
        fclose(f);
    h = texrep_hash(want8, 32, 16);
    snprintf(name, sizeof(name), "test_textures_disc/dump/32x16_%08x%08x.png", (unsigned)(h >> 32), (unsigned)h);
    f = fopen(name, "rb");
    printf("%s: %s\n", name, f != NULL ? "exported" : "missing");
    bad |= f == NULL;
    if (f != NULL)
        fclose(f);
    h = texrep_hash(want_big, 256, 256);
    snprintf(name, sizeof(name), "test_textures_disc/dump/256x256_%08x%08x.png", (unsigned)(h >> 32), (unsigned)h);
    f = fopen(name, "rb");
    printf("%s: %s\n", name, f != NULL ? "exported" : "missing");
    bad |= f == NULL;
    if (f != NULL)
        fclose(f);

    /* the same in a compressed room */
    n = pc_extract_textures_in(packed, pack(file, pos), 1);
    printf("textures found in a compressed room: %d\n", n);
    bad |= n != 3;
    if (bad) {
        static uint32_t got[256 * 256];
        int w, hh, shown = 0;
        gs_decode_current_texture(0, got, 256 * 256, &w, &hh); /* the last one */
        for (int i = 0; i < 65536 && shown < 8; i++) {
            if (got[i] != want_big[i]) {
                printf("texel %d,%d: %08x, not %08x\n", i & 255, i >> 8, got[i], want_big[i]);
                shown++;
            }
        }
    }
    return bad;
}
