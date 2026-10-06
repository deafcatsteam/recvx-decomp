/*
 * Exports every texture of the disc for the texture packs (gs_texrep.h),
 * without playing: "Extraire les textures" in the settings window, or
 * cvx_pc --extract-textures.
 *
 * The game's textures are TIM2 pictures, mostly in "PVP" packs (a palette
 * list, then the pictures, see bhSetMemPvpTexture), in the rooms of
 * RDX_LNK.AFS (compressed, see Expand) and in the other archives and files
 * (players, enemies, items, menus, files, maps...), plus single pictures
 * (the ADV screens, SetPvrInfo). Every file of the disc is searched for
 * them. Each one found goes through the game's own code, as when it is
 * loaded in game: its alpha is set up (Ps2CheckTextureAlpha), its palettes
 * go to their banks, it is sent to GS memory with the TEX0 the game draws
 * it with (Ps2TexLoad), and the GS decodes it as the GPU renderer does
 * before drawing. Its fingerprint is then the one it has in game.
 *
 * What this cannot know: palettes the game changes while playing (fades),
 * and pictures it draws itself. Those are exported in game (textures =
 * dump).
 */
#include "ps2_dummy.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_texture.h"
#include "adv.h"
#include "main.h"

#include "../gs/gs.h"
#include "../gs/gs_texrep.h"
#include "../platform/pc_disc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char Ps2_tex_mem[10485760];
extern unsigned char *Ps2_PXLCONV;
static unsigned char pxlconv[1 << 20] __attribute__((aligned(64))); /* before the game sets it up */

#define CODE_PLI 0x00494C50u  /* 'P','L','I',0 */
#define CODE_TIM2 0x324D4954u /* 'T','I','M','2' */
#define MAX_TEX 1024
#define PAD 65536             /* zeros after a copy: palettes read past a short file */

static NJS_TEXMEMLIST mem[MAX_TEX];
static NJS_TEXNAME names[MAX_TEX];
static NJS_TEXINFO infos[MAX_TEX];
static uint32_t picture[1024 * 1024];
static unsigned char *work;
static uint32_t work_cap;
static int found;
volatile int pc_texextract_found; /* for the settings window */

/* Packs already done (the same enemy is in many rooms). */
static uint64_t *seen;
static uint32_t seen_n, seen_cap;

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static int pow2(uint32_t v)
{
    return v >= 8 && v <= 1024 && (v & (v - 1)) == 0;
}

/* A TIM2 picture the game's loaders take, within avail bytes; its size. */
static uint32_t tim2_size(const unsigned char *t, uint32_t avail, int bare)
{
    uint32_t total, clut, image, w, h, type, need;

    if (avail < 0x100 || rd32(t) != CODE_TIM2)
        return 0;
    total = rd32(t + 0x80);
    clut = rd32(t + 0x84);
    image = rd32(t + 0x88);
    type = t[0x93];
    w = rd16(t + 0x94);
    h = rd16(t + 0x96);
    /* the picture at 0x100, its palette after it (as the game reads them) */
    need = type == 3 ? w * h * 4 : type == 4 ? w * h / 2 : type == 5 ? w * h : 0;
    if (!pow2(w) || !pow2(h) || need == 0 || image < need || image > (4u << 20) || (type == 3 && !bare) ||
        0x100 + image > avail)
        return 0;
    if (type != 3 && (clut < (type == 4 ? 64u : 1024u) || clut > (1u << 20)))
        return 0;
    need = 0x100 + image + clut;
    if (total + 128 > need)
        need = total + 128;
    return need > avail ? avail : need;
}

/* A PVP pack starting at p: its end, or 0. */
static uint32_t pack_at(const unsigned char *b, uint32_t n, uint32_t p)
{
    uint32_t q = p, pictures = 0;

    while (q + 32 <= n) {
        uint32_t code = rd32(b + q), sz = rd32(b + q + 4);

        if (code == 0xFFFFFFFFu) {
            q += 32;
            break;
        }
        if (sz > n - q - 32)
            break;
        if (code == CODE_PLI) {
            uint32_t pal = sz >= 4 ? rd32(b + q + 32) : 0;
            if (sz != 0 && (pal > 64 || 4 + 8 * pal > sz))
                break;
        } else if (code == CODE_TIM2) {
            if (tim2_size(b + q + 32, sz, 0) == 0)
                break;
            pictures++;
        } else {
            break; /* the game stops there too */
        }
        q += 32 + sz;
    }
    return pictures > 0 ? q : 0;
}

static unsigned char *copy(const unsigned char *src, uint32_t size)
{
    if (work_cap < size + PAD + 64) {
        free(work);
        work_cap = size + PAD + 64;
        work = malloc(work_cap + 64);
        if (work == NULL) {
            work_cap = 0;
            return NULL;
        }
    }
    unsigned char *w = (unsigned char *)(((uintptr_t)work + 63) & ~(uintptr_t)63);
    memcpy(w, src, size);
    memset(w + size, 0, PAD);
    return w;
}

static int already(const unsigned char *b, uint32_t size)
{
    uint64_t h = 14695981039346656037ull;

    for (uint32_t i = 0; i < size; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    h ^= size;
    for (uint32_t i = 0; i < seen_n; i++) {
        if (seen[i] == h)
            return 1;
    }
    if (seen_n == seen_cap) {
        uint64_t *s = realloc(seen, (seen_cap ? seen_cap * 2 : 4096) * sizeof(*s));
        if (s == NULL)
            return 0;
        seen = s;
        seen_cap = seen_cap ? seen_cap * 2 : 4096;
    }
    seen[seen_n++] = h;
    return 0;
}

static void start_textures(void)
{
    if (Ps2_PXLCONV == NULL)
        Ps2_PXLCONV = pxlconv; /* for 256 and 512 pictures (Ps2PxlconvCheck) */
    Ps2_tex_buff = Ps2_tex_mem;
    njInitTexture(mem, MAX_TEX);
    memset(names, 0, sizeof(names));
    sys->ef_pbnk = 32;
}

/* Sends texture i to the GS as when the game draws with it, and exports it. */
static void export(int i)
{
    int w, h;

    Ps2_current_texbreak = 1;
    Ps2TexLoad((NJS_TEXMEMLIST *)names[i].texaddr);
    if (!gs_decode_current_texture(0, picture, 1024 * 1024, &w, &h))
        return;
    uint64_t hash = texrep_hash(picture, w, h);
    texrep_dump(hash, picture, w, h, hash);
    found++;
    pc_texextract_found = found;
}

static void do_pack(const unsigned char *src, uint32_t size)
{
    NJS_TEXLIST tl = { names, MAX_TEX };
    unsigned char *b;
    int n;

    if (already(src, size) || (b = copy(src, size)) == NULL)
        return;
    memset(b + size, 0xFF, 32); /* the end, if the pack had none */
    start_textures();
    n = bhSetMemPvpTexture(&tl, b, 0);
    njSetPaletteData(0, 1024, palbuf);
    for (int i = 0; i < n && i < MAX_TEX; i++)
        export(i);
}

static void do_picture(const unsigned char *src, uint32_t size)
{
    NJS_TEXLIST tl = { names, 1 };
    unsigned char *b;

    if (already(src, size) || (b = copy(src, size)) == NULL)
        return;
    start_textures();
    SetPvrInfo(&names[0], &infos[0], b, 0, 0);
    njLoadTexture(&tl);
    export(0);
}

static void search(const unsigned char *b, uint32_t n)
{
    uint32_t p = 0;

    while (p + 32 <= n) {
        uint32_t end, size;

        if ((end = pack_at(b, n, p)) != 0) {
            do_pack(b + p, end - p);
            p = end;
        } else if ((size = tim2_size(b + p, n - p, 1)) != 0) {
            do_picture(b + p, size);
            p += size;
        } else {
            p += 4;
            continue;
        }
        p = (p + 3) & ~3u;
    }
}

/* ---- Files of the disc --------------------------------------------------- */

static unsigned char *data, *expanded;
static uint32_t data_cap;
#define EXPANDED_MAX (32u << 20)

/* Expand (pc_game_asm.c), the rooms' compression, kept within src and dst:
 * the size, or -1 if the data is not compressed that way. */
static int expand_safe(const unsigned char *src, uint32_t n, unsigned char *dst, uint32_t cap)
{
    const unsigned char *s = src, *end = src + n;
    unsigned int field, bit;
    uint32_t o = 0;
    int remaining = 9;

#define NEXT_BIT()                          \
    do {                                    \
        if (--remaining == 0) {             \
            if (s >= end)                   \
                return -1;                  \
            field = *s++;                   \
            remaining = 8;                  \
        }                                   \
        bit = field & 1;                    \
        field >>= 1;                        \
    } while (0)

    if (n == 0)
        return -1;
    field = *s++;
    for (;;) {
        uint32_t length;
        int offset;

        NEXT_BIT();
        if (bit) {
            if (s >= end || o >= cap)
                return -1;
            dst[o++] = *s++;
            continue;
        }
        NEXT_BIT();
        if (!bit) {
            NEXT_BIT();
            length = bit * 2;
            NEXT_BIT();
            length += bit + 2;
            if (s >= end)
                return -1;
            offset = -0x100 + *s++;
        } else {
            if (s + 2 > end)
                return -1;
            unsigned int whole = s[0] | (s[1] << 8);
            s += 2;
            if (whole == 0)
                break;
            offset = -0x2000 + (int)(whole >> 3);
            length = whole & 7;
            if (length != 0) {
                length += 2;
            } else {
                if (s >= end)
                    return -1;
                length = *s++ + 1u;
            }
        }
        if ((int64_t)o + offset < 0 || o + length > cap)
            return -1;
        while (length-- != 0) {
            dst[o] = dst[(int64_t)o + offset];
            o++;
        }
    }
#undef NEXT_BIT
    return (int)o;
}

static int read_bytes(uint64_t offset, uint32_t size)
{
    if (data_cap < size) {
        free(data);
        data = malloc(size);
        data_cap = data != NULL ? size : 0;
        if (data == NULL)
            return 0;
    }
    return pc_disc_read_bytes(offset, size, data);
}

static int ends_with(const char *name, const char *ext)
{
    size_t a = strlen(name), b = strlen(ext);

    if (a < b)
        return 0;
    for (size_t i = 0; i < b; i++) {
        char c = name[a - b + i];
        if ((c >= 'a' && c <= 'z' ? c - 32 : c) != ext[i])
            return 0;
    }
    return 1;
}

static void each_file(const char *name, unsigned int lsn, unsigned int size, void *unused)
{
    uint64_t base = (uint64_t)lsn * PC_DISC_SECTOR;
    int before = found;

    (void)unused;
    /* Movies, music and voices hold no textures. */
    if (ends_with(name, ".SFD") || ends_with(name, ".PSS") || ends_with(name, ".ADX") ||
        ends_with(name, ".IRX") || strstr(name, "BGM") != NULL || strstr(name, "VOICE") != NULL)
        return;
    if (ends_with(name, ".AFS")) {
        unsigned char head[8];
        int rooms = strstr(name, "RDX_LNK") != NULL;

        if (!pc_disc_read_bytes(base, 8, head) || memcmp(head, "AFS", 3) != 0)
            return;
        uint32_t count = rd32(head + 4);
        if (count > 100000 || !read_bytes(base + 8, count * 8))
            return;
        uint32_t *table = malloc(count * 8);
        if (table == NULL)
            return;
        memcpy(table, data, count * 8);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t off = rd32((unsigned char *)&table[i * 2]), sz = rd32((unsigned char *)&table[i * 2 + 1]);
            if (sz == 0 || sz > (64u << 20) || (uint64_t)off + sz > size || !read_bytes(base + off, sz))
                continue;
            if (rooms) {
                if (expanded == NULL)
                    expanded = malloc(EXPANDED_MAX);
                if (expanded == NULL)
                    continue;
                int n = expand_safe(data, sz, expanded, EXPANDED_MAX);
                if (n > 0)
                    search(expanded, (uint32_t)n);
                else
                    search(data, sz);
            } else {
                search(data, sz);
            }
        }
        free(table);
    } else if (size <= (64u << 20) && read_bytes(base, size)) {
        search(data, size);
    }
    if (found > before)
        printf("textures: %s, %d textures\n", name, found - before);
}

/* For the tests: the textures in data, as if it was a file of the disc
 * (compressed: a room of RDX_LNK.AFS). */
int pc_extract_textures_in(const void *bytes, uint32_t size, int compressed)
{
    int before = found;

    seen_n = 0;
    gs_reset();
    if (compressed) {
        if (expanded == NULL)
            expanded = malloc(EXPANDED_MAX);
        int n = expanded != NULL ? expand_safe(bytes, size, expanded, EXPANDED_MAX) : -1;
        if (n <= 0)
            return -1;
        search(expanded, (uint32_t)n);
    } else {
        search(bytes, size);
    }
    return found - before;
}

int pc_extract_textures(void)
{
    /* What the game's code changes here, put back after for the game. */
    SYS_WORK *saved_sys = malloc(sizeof(SYS_WORK));
    unsigned int *saved_pal = malloc(sizeof(palbuf));
    unsigned char *saved_pxlconv = Ps2_PXLCONV;
    int ok;

    if (saved_sys == NULL || saved_pal == NULL) {
        free(saved_sys);
        free(saved_pal);
        return -1;
    }
    memcpy(saved_sys, sys, sizeof(SYS_WORK));
    memcpy(saved_pal, palbuf, sizeof(palbuf));
    found = 0;
    pc_texextract_found = 0;
    gs_reset();
    texrep_set(TEXREP_DUMP, getenv("CVX_TEXTURES") != NULL ? getenv("CVX_TEXTURES") : "textures");
    printf("textures: exporting every texture of the disc to textures/dump...\n");
    ok = pc_disc_list(each_file, NULL);
    if (!ok)
        printf("textures: no disc image (iso in cvx.ini)\n");
    else
        printf("textures: done, %d textures looked at (the same ones only once in textures/dump)\n", found);

    memcpy(sys, saved_sys, sizeof(SYS_WORK));
    memcpy(palbuf, saved_pal, sizeof(palbuf));
    Ps2_PXLCONV = saved_pxlconv;
    free(saved_sys);
    free(saved_pal);
    free(data);
    free(expanded);
    free(work);
    free(seen);
    data = expanded = work = NULL;
    data_cap = work_cap = 0;
    seen = NULL;
    seen_n = seen_cap = 0;
    gs_reset();
    texrep_set(-1, "");
    return ok ? found : -1;
}
