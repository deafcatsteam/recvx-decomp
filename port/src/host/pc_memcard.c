/*
 * The memory card (see pc_memcard.h): the card in slot 1 is the folder
 * "saves" next to the game (or CVX_SAVES), with the card's folders and files
 * as they are on a PS2 card, for example saves/BASLUS-20184/SAVEDATA-00.
 * Slot 2 stays empty.
 */
#include "pc_memcard.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define make_dir(p) _mkdir(p)
#else
#include <unistd.h>
#define make_dir(p) mkdir(p, 0777)
#endif

#define CARD_KB   8000          /* an 8 MB card */
#define MAX_FILE  (1024 * 1024) /* larger than any file the game writes */

static char cwd[1024] = "/";

static struct {
    int open, dirty;
    char path[1024];
    unsigned char *data;
    int size, pos;
} file;

const char *pc_mc_root(void)
{
    const char *root = getenv("CVX_SAVES");

    return root != NULL && root[0] != 0 ? root : "saves";
}

int pc_mc_card(int port)
{
    return port == 0;
}

/* ---- Paths --------------------------------------------------------------- */

static int bad_name(const char *s, int n, int wild)
{
    int i;

    if (n <= 0 || n > 31)
        return 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c < 0x20 || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|')
            return 1;
        if (!wild && (c == '*' || c == '?'))
            return 1;
    }
    return 0;
}

/* The card path of path, made absolute and without "." and "..": "/A/B",
 * or "/" for the root. Returns 0 if the path is not valid. */
static int card_path(const char *path, char *out, int wild)
{
    char full[2048];
    const char *p;
    int len = 0;

    if (path == NULL || strlen(path) >= 1024)
        return 0;
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s", path);
    else
        snprintf(full, sizeof(full), "%s/%s", cwd, path);

    out[0] = 0;
    for (p = full; *p != 0;) {
        const char *e;
        int n;

        while (*p == '/')
            p++;
        if (*p == 0)
            break;
        e = strchr(p, '/');
        n = e != NULL ? (int)(e - p) : (int)strlen(p);
        if (n == 1 && p[0] == '.') {
            /* stays */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (len > 0 && out[len - 1] != '/')
                len--;
            if (len > 0)
                len--;
            out[len] = 0;
        } else {
            if (bad_name(p, n, wild && (e == NULL || e[1] == 0)) || len + 1 + n >= 1000)
                return 0;
            out[len++] = '/';
            memcpy(out + len, p, (size_t)n);
            len += n;
            out[len] = 0;
        }
        p += n;
    }
    if (len == 0)
        strcpy(out, "/");
    return 1;
}

static void host_path(const char *card, char *out, size_t size)
{
    snprintf(out, size, "%s%s", pc_mc_root(), strcmp(card, "/") == 0 ? "" : card);
}

/* 0 = missing, 1 = file, 2 = folder */
static int kind(const char *host, struct stat *st)
{
    struct stat s;

    if (st == NULL)
        st = &s;
    if (stat(host, st) != 0)
        return 0;
    return S_ISDIR(st->st_mode) ? 2 : 1;
}

/* The folder holding a card path ("/A/B" -> "/A"). */
static void parent_of(const char *card, char *out)
{
    char *slash;

    strcpy(out, card);
    slash = strrchr(out, '/');
    if (slash == out)
        strcpy(out, "/");
    else if (slash != NULL)
        *slash = 0;
}

static void make_root(void)
{
    make_dir(pc_mc_root());
}

/* ---- Free space ---------------------------------------------------------- */

static int used_kb(const char *host, int depth)
{
    DIR *d = opendir(host);
    struct dirent *e;
    int kb = 0;

    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL) {
        char sub[2048];
        struct stat st;
        int k;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(sub, sizeof(sub), "%s/%s", host, e->d_name);
        k = kind(sub, &st);
        if (k == 1)
            kb += (int)((st.st_size + 1023) / 1024);
        else if (k == 2 && depth < 2)
            kb += 2 + used_kb(sub, depth + 1);
    }
    closedir(d);
    return kb;
}

int pc_mc_free_kb(int port)
{
    int left;

    if (!pc_mc_card(port))
        return 0;
    left = CARD_KB - used_kb(pc_mc_root(), 0);
    return left > 0 ? left : 0;
}

/* ---- Files --------------------------------------------------------------- */

static int save_file(void)
{
    char tmp[1100];
    FILE *f;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.tmp", file.path);
    f = fopen(tmp, "wb");
    if (f == NULL)
        return 0;
    ok = fwrite(file.data, 1, (size_t)file.size, f) == (size_t)file.size;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
#ifdef _WIN32
    ok = ok && MoveFileExA(tmp, file.path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    ok = ok && rename(tmp, file.path) == 0;
#endif
    if (!ok)
        remove(tmp);
    return ok;
}

int pc_mc_close(int fd)
{
    int r = 0;

    if (fd != 0 || !file.open)
        return PC_MC_FAILED;
    if (file.dirty && !save_file()) {
        fprintf(stderr, "memcard: cannot write %s\n", file.path);
        r = PC_MC_FAILED;
    }
    free(file.data);
    memset(&file, 0, sizeof(file));
    return r;
}

int pc_mc_open(int port, const char *path, int mode)
{
    char card[1024], parent[1024], host[2048];
    struct stat st;
    int k;

    if (!pc_mc_card(port))
        return PC_MC_NOCARD;
    if (file.open)
        pc_mc_close(0); /* the game never keeps two files open */
    if (!card_path(path, card, 0) || strcmp(card, "/") == 0)
        return PC_MC_NOTFOUND;
    host_path(card, host, sizeof(host));
    if (strlen(host) >= sizeof(file.path))
        return PC_MC_NOTFOUND;

    k = kind(host, &st);
    if (k == 2)
        return PC_MC_DENIED;
    if (k == 0) {
        char phost[2048];

        if (!(mode & PC_MC_CREATE))
            return PC_MC_NOTFOUND;
        parent_of(card, parent);
        host_path(parent, phost, sizeof(phost));
        if (strcmp(parent, "/") == 0)
            make_root();
        if (kind(phost, NULL) != 2)
            return PC_MC_NOTFOUND;
    }

    memset(&file, 0, sizeof(file));
    strcpy(file.path, host);
    if (k == 1) {
        FILE *f;

        if (st.st_size > MAX_FILE)
            return PC_MC_FAILED;
        file.size = (int)st.st_size;
        file.data = malloc(file.size > 0 ? (size_t)file.size : 1);
        f = fopen(host, "rb");
        if (file.data == NULL || f == NULL ||
            fread(file.data, 1, (size_t)file.size, f) != (size_t)file.size) {
            if (f != NULL)
                fclose(f);
            free(file.data);
            memset(&file, 0, sizeof(file));
            return PC_MC_FAILED;
        }
        fclose(f);
    } else {
        file.dirty = 1; /* a new file exists once closed, even if empty */
    }
    file.open = 1;
    return 0;
}

int pc_mc_read(int fd, void *buf, int size)
{
    int n;

    if (fd != 0 || !file.open || size < 0)
        return PC_MC_FAILED;
    n = file.size - file.pos;
    if (n > size)
        n = size;
    if (n > 0)
        memcpy(buf, file.data + file.pos, (size_t)n);
    else
        n = 0;
    file.pos += n;
    return n;
}

int pc_mc_write(int fd, const void *buf, int size)
{
    if (fd != 0 || !file.open || size < 0 || file.pos + size > MAX_FILE)
        return PC_MC_FAILED;
    if (file.pos + size > file.size) {
        unsigned char *grown = realloc(file.data, (size_t)(file.pos + size));

        if (grown == NULL)
            return PC_MC_FAILED;
        memset(grown + file.size, 0, (size_t)(file.pos + size - file.size));
        file.data = grown;
        file.size = file.pos + size;
    }
    memcpy(file.data + file.pos, buf, (size_t)size);
    file.pos += size;
    file.dirty = 1;
    return size;
}

/* ---- Folders ------------------------------------------------------------- */

int pc_mc_mkdir(int port, const char *path)
{
    char card[1024], parent[1024], host[2048];

    if (!pc_mc_card(port))
        return PC_MC_NOCARD;
    if (!card_path(path, card, 0) || strcmp(card, "/") == 0)
        return PC_MC_NOTFOUND;
    parent_of(card, parent);
    if (strcmp(parent, "/") == 0)
        make_root();
    host_path(parent, host, sizeof(host));
    if (kind(host, NULL) != 2)
        return PC_MC_NOTFOUND;
    host_path(card, host, sizeof(host));
    if (kind(host, NULL) != 0)
        return PC_MC_NOTFOUND; /* already there */
    return make_dir(host) == 0 ? 0 : PC_MC_FAILED;
}

int pc_mc_chdir(int port, const char *path, char *old)
{
    char card[1024], host[2048];

    if (!pc_mc_card(port))
        return PC_MC_NOCARD;
    if (old != NULL)
        strcpy(old, cwd);
    if (!card_path(path, card, 0))
        return PC_MC_NOTFOUND;
    host_path(card, host, sizeof(host));
    if (strcmp(card, "/") != 0 && kind(host, NULL) != 2)
        return PC_MC_NOTFOUND;
    strcpy(cwd, card);
    return 0;
}

int pc_mc_delete(int port, const char *path)
{
    char card[1024], host[2048];
    int k;

    if (!pc_mc_card(port))
        return PC_MC_NOCARD;
    if (!card_path(path, card, 0) || strcmp(card, "/") == 0)
        return PC_MC_NOTFOUND;
    host_path(card, host, sizeof(host));
    k = kind(host, NULL);
    if (k == 0)
        return PC_MC_NOTFOUND;
    if (k == 1)
        return remove(host) == 0 ? 0 : PC_MC_FAILED;
    return rmdir(host) == 0 ? 0 : PC_MC_DENIED; /* only when empty */
}

static int match(const char *pat, const char *s)
{
    if (*pat == 0)
        return *s == 0;
    if (*pat == '*')
        return match(pat + 1, s) || (*s != 0 && match(pat, s + 1));
    if (*s != 0 && (*pat == '?' || *pat == *s))
        return match(pat + 1, s + 1);
    return 0;
}

static void entry(PcMcEntry *e, const char *name, const char *host)
{
    struct stat st;
    int k = kind(host, &st);

    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = k == 2;
    if (k == 1)
        e->size = (uint32_t)st.st_size;
    if (k != 0)
        e->mtime = (int64_t)st.st_mtime;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const PcMcEntry *)a)->name, ((const PcMcEntry *)b)->name);
}

int pc_mc_getdir(int port, const char *path, int max, PcMcEntry *out)
{
    char card[1024], dir[1024], host[2048];
    const char *pat;
    DIR *d;
    struct dirent *e;
    PcMcEntry list[64];
    int n = 0, count = 0, i;

    if (!pc_mc_card(port) || max <= 0)
        return 0;
    if (!card_path(path, card, 1))
        return 0;
    parent_of(card, dir);
    pat = strrchr(card, '/') + 1;

    if (strchr(pat, '*') == NULL && strchr(pat, '?') == NULL) {
        /* One entry: the file or folder itself. */
        host_path(card, host, sizeof(host));
        if (*pat == 0 || kind(host, NULL) == 0)
            return 0;
        entry(&out[0], pat, host);
        return 1;
    }

    host_path(dir, host, sizeof(host));
    if (kind(host, NULL) != 2)
        return 0;
    if (strcmp(dir, "/") != 0) {
        static const char *const dots[2] = { ".", ".." };

        for (i = 0; i < 2 && count < max; i++) {
            if (match(pat, dots[i])) {
                entry(&out[count], dots[i], host);
                out[count++].is_dir = 1;
            }
        }
    }
    d = opendir(host);
    if (d == NULL)
        return count;
    while ((e = readdir(d)) != NULL && n < 64) {
        char sub[2100];
        size_t len = strlen(e->d_name);

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 || len > 31)
            continue;
        if (len > 4 && strcmp(e->d_name + len - 4, ".tmp") == 0)
            continue; /* an interrupted save */
        if (!match(pat, e->d_name))
            continue;
        snprintf(sub, sizeof(sub), "%s/%s", host, e->d_name);
        entry(&list[n++], e->d_name, sub);
    }
    closedir(d);
    qsort(list, (size_t)n, sizeof(list[0]), by_name);
    for (i = 0; i < n && count < max; i++)
        out[count++] = list[i];
    return count;
}
