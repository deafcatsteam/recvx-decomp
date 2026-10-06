/*
 * Texture packs: export and replacement files, see gs_texrep.h.
 */
#include "gs_texrep.h"
#include "../host/pc_host.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_UTF8 /* the names come from opendir, in the local code page */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_UTF8
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "../../third_party/stb/stb_image.h"
#include "../../third_party/stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

volatile int texrep_reload_asked;

static int mode = -1;
static char root[512];

/* ---- Sets of fingerprints ------------------------------------------------- */

typedef struct {
    uint64_t *keys;  /* 0: empty (a fingerprint is never 0) */
    char **paths;    /* replacements: the file, or NULL */
    uint32_t *count; /* slots: pictures seen */
    size_t n, cap;
} Set;

static Set replace, dumped, slots;

static size_t find(const Set *s, uint64_t key)
{
    size_t i = (size_t)(key ^ key >> 29) & (s->cap - 1);

    while (s->keys[i] != 0 && s->keys[i] != key)
        i = (i + 1) & (s->cap - 1);
    return i;
}

static void set_clear(Set *s)
{
    for (size_t i = 0; i < s->cap; i++) {
        if (s->paths != NULL)
            free(s->paths[i]);
    }
    free(s->keys);
    free(s->paths);
    free(s->count);
    memset(s, 0, sizeof(*s));
}

/* Adds key (or finds it); returns its place, or -1 without memory. */
static long set_add(Set *s, uint64_t key, int with_paths, int with_count)
{
    size_t i;

    if (key == 0)
        key = 1;
    if ((s->n + 1) * 2 > s->cap) {
        Set bigger = { 0 };

        bigger.cap = s->cap != 0 ? s->cap * 2 : 1024;
        bigger.keys = calloc(bigger.cap, sizeof(uint64_t));
        bigger.paths = with_paths ? calloc(bigger.cap, sizeof(char *)) : NULL;
        bigger.count = with_count ? calloc(bigger.cap, sizeof(uint32_t)) : NULL;
        if (bigger.keys == NULL || (with_paths && bigger.paths == NULL) || (with_count && bigger.count == NULL)) {
            free(bigger.keys);
            free(bigger.paths);
            free(bigger.count);
            return -1;
        }
        for (size_t j = 0; j < s->cap; j++) {
            if (s->keys[j] == 0)
                continue;
            i = find(&bigger, s->keys[j]);
            bigger.keys[i] = s->keys[j];
            if (with_paths)
                bigger.paths[i] = s->paths[j];
            if (with_count)
                bigger.count[i] = s->count[j];
        }
        bigger.n = s->n;
        free(s->keys);
        free(s->paths);
        free(s->count);
        *s = bigger;
    }
    i = find(s, key);
    if (s->keys[i] == 0) {
        s->keys[i] = key;
        s->n++;
    }
    return (long)i;
}

static int set_has(const Set *s, uint64_t key)
{
    if (key == 0)
        key = 1;
    return s->cap != 0 && s->keys[find(s, key)] == key;
}

/* ---- Files ---------------------------------------------------------------- */

static void make_dir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

/* The fingerprint at the end of a name like 256x128_0123456789abcdef.png
 * (16 hexadecimal digits before .png), or 0. */
static uint64_t name_hash(const char *name)
{
    size_t len = strlen(name);
    uint64_t h = 0;

    if (len < 20 || (strcmp(name + len - 4, ".png") != 0 && strcmp(name + len - 4, ".PNG") != 0))
        return 0;
    for (size_t i = len - 20; i < len - 4; i++) {
        char c = name[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                               : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return 0;
        h = h << 4 | (uint64_t)d;
    }
    return h;
}

/* Every .png under dir: replacements (into replace) or exported ones. */
static int scan(const char *dir, int replacements, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;
        uint64_t h;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 8)
                found += scan(path, replacements, depth + 1);
            continue;
        }
        h = name_hash(e->d_name);
        if (h == 0)
            continue;
        if (!replacements) {
            set_add(&dumped, h, 0, 0);
            continue;
        }
        long i = set_add(&replace, h, 1, 0);
        if (i < 0)
            break;
        if (replace.paths[i] != NULL) {
            printf("textures: %s and %s replace the same texture, the second is used\n", replace.paths[i],
                   path);
            free(replace.paths[i]);
        }
        replace.paths[i] = strdup(path);
        found++;
    }
    closedir(d);
    return found;
}

static void rescan(void)
{
    char path[600];
    int n;

    set_clear(&replace);
    set_clear(&dumped);
    if (mode == TEXREP_OFF)
        return;
    snprintf(path, sizeof(path), "%s/replace", root);
    n = scan(path, 1, 0);
    if (n > 0)
        printf("textures: %d replacements in %s\n", n, path);
    if (mode == TEXREP_DUMP) {
        make_dir(root);
        snprintf(path, sizeof(path), "%s/dump", root);
        make_dir(path);
        scan(path, 0, 0);
        printf("textures: exported to %s (%d there already)\n", path, (int)dumped.n);
    }
}

int texrep_mode(void)
{
    if (mode < 0) {
        const char *v = pc_config_get("textures"), *dir = getenv("CVX_TEXTURES");

        mode = TEXREP_HD;
        if (v != NULL && (strcmp(v, "original") == 0 || strcmp(v, "off") == 0 || strcmp(v, "no") == 0))
            mode = TEXREP_OFF;
        else if (v != NULL && strcmp(v, "dump") == 0)
            mode = TEXREP_DUMP;
        else if (v != NULL && v[0] != 0 && strcmp(v, "hd") != 0)
            printf("config: textures should be original, hd or dump, not '%s'\n", v);
        snprintf(root, sizeof(root), "%s", dir != NULL && dir[0] != 0 ? dir : "textures");
        rescan();
    }
    return mode;
}

void texrep_set(int m, const char *dir)
{
    mode = m;
    snprintf(root, sizeof(root), "%s", dir);
    set_clear(&slots);
    rescan();
}

int texrep_reload_pending(void)
{
    if (!texrep_reload_asked)
        return 0;
    texrep_reload_asked = 0;
    texrep_mode();
    rescan();
    set_clear(&slots);
    return 1;
}

/* ---- Fingerprints --------------------------------------------------------- */

static uint64_t rotl(uint64_t v, int k)
{
    return v << k | v >> (64 - k);
}

uint64_t texrep_hash(const uint32_t *rgba, int w, int h)
{
    const uint64_t p1 = 0x9e3779b185ebca87ull, p2 = 0xc2b2ae3d27d4eb4full;
    uint64_t a = 0x27d4eb2f165667c5ull ^ ((uint64_t)w << 32 | (uint32_t)h), b = p1;
    size_t n = (size_t)w * h, i = 0;

    for (; i + 2 <= n; i += 2) {
        uint64_t v = (uint64_t)rgba[i] | (uint64_t)rgba[i + 1] << 32;
        a = rotl(a ^ v * p2, 31) * p1;
        b += v;
        b = rotl(b, 27) * p2;
    }
    if (i < n)
        a = rotl(a ^ rgba[i] * p2, 31) * p1;
    a ^= rotl(b, 17);
    a ^= a >> 33;
    a *= p2;
    a ^= a >> 29;
    a *= 0x165667b19e3779f9ull;
    a ^= a >> 32;
    return a != 0 ? a : 1;
}

/* ---- Export and replacement ----------------------------------------------- */

void texrep_dump(uint64_t hash, const uint32_t *rgba, int w, int h, uint64_t slot)
{
    char path[700];
    long s;
    uint8_t *px;

    if (texrep_mode() != TEXREP_DUMP || set_has(&replace, hash) || set_has(&dumped, hash))
        return;
    s = set_add(&slots, slot, 0, 1);
    if (s < 0 || ++slots.count[s] > 8) /* the same place, picture after picture */
        return;
    if (set_add(&dumped, hash, 0, 0) < 0)
        return;
    px = malloc((size_t)w * h * 4);
    if (px == NULL)
        return;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t c = rgba[i], a = c >> 24;

        px[i * 4 + 0] = (uint8_t)c;
        px[i * 4 + 1] = (uint8_t)(c >> 8);
        px[i * 4 + 2] = (uint8_t)(c >> 16);
        px[i * 4 + 3] = (uint8_t)(a >= 128 ? 255 : (a * 255 + 64) / 128);
    }
    snprintf(path, sizeof(path), "%s/dump/%dx%d_%016llx.png", root, w, h, (unsigned long long)hash);
    if (!stbi_write_png(path, w, h, 4, px, w * 4))
        printf("textures: could not write %s\n", path);
    free(px);
}

int texrep_busy(void)
{
    return texrep_mode() == TEXREP_DUMP || replace.n > 0;
}

int texrep_has(uint64_t hash)
{
    return texrep_mode() != TEXREP_OFF && set_has(&replace, hash);
}

uint32_t *texrep_load(uint64_t hash, int *w, int *h)
{
    const char *path;
    uint8_t *px;
    int n;

    if (!texrep_has(hash))
        return NULL;
    path = replace.paths[find(&replace, hash)];
    px = stbi_load(path, w, h, &n, 4);
    if (px == NULL) {
        printf("textures: could not read %s (%s)\n", path, stbi_failure_reason());
        return NULL;
    }
    for (size_t i = 0; i < (size_t)*w * *h; i++)
        px[i * 4 + 3] = (uint8_t)((px[i * 4 + 3] * 128 + 127) / 255);
    return (uint32_t *)px;
}
