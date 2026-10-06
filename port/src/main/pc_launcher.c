/*
 * Settings window shown before the game: the disc image, the window, the
 * renderer, sound, vibration and the keyboard keys, saved into cvx.ini
 * (pc_config.c) when the player presses Jouer or Quitter.
 *
 * Drawn with SDL's 2D renderer and the fonts of pc_font.h; it is used with
 * the mouse, the keyboard (arrows, Return, Tab for the pages) or a game
 * controller (D-pad, A, LB/RB, Start). The disc image is chosen with the
 * system's file dialog (Windows; zenity elsewhere) or dropped on the window.
 *
 * CVX_LAUNCHER_SHOTS=prefix saves each page as prefix0.bmp ... and quits
 * (the build's test).
 */
#include "pc_window.h"

#include "../host/pc_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CVX_HAVE_SDL
#define SDL_MAIN_HANDLED /* main() is pc_main.c's */
#include <SDL.h>
#include <SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include "pc_font.h"

#define WIDTH 880
#define HEIGHT 620

/* Colours */
enum {
    C_BACK = 0x14161a, C_HEADER = 0x3a0e12, C_LINE = 0x2e333b, C_FIELD = 0x23272e,
    C_FIELD_HOVER = 0x2c313a, C_TEXT = 0xe8e6e3, C_DIM = 0x8f959d, C_ACCENT = 0xc8343c,
    C_ACCENT_HOVER = 0xdc4a52, C_GOOD = 0x5cb85c, C_BAD = 0xe5534b, C_OFF = 0x434952,
};

/* What can be clicked or focused */
enum {
    ID_NONE, ID_TAB0, ID_TAB1, ID_TAB2, ID_TAB3, ID_ISO, ID_BROWSE, ID_FULLSCREEN, ID_WINDOW,
    ID_LAUNCHER, ID_RENDERER, ID_UPSCALE, ID_FILTER, ID_WIDESCREEN, ID_FPS60, ID_TEXTURES, ID_SOUND, ID_SOUND3D, ID_VIBRATION, ID_KEYS_DEFAULT,
    ID_QUIT, ID_PLAY, ID_KEY0 = 100,
};

#define MAX_KEYS 32

typedef struct {
    char iso[512];
    char window[32];
    int fullscreen, launcher, renderer, upscale, filter, widescreen, fps60, textures, sound, sound3d, vibration;
    char keys[MAX_KEYS][96];
} Settings;

static Settings now, before;
static int nkeys;
static const char *key_setting[MAX_KEYS], *key_default[MAX_KEYS];
static int upscale_set; /* upscale is in cvx.ini */

/* French names of the actions of pc_window.c's key_actions */
static const struct {
    const char *setting, *label;
} key_labels[] = {
    { "key_up", "Haut" }, { "key_down", "Bas" }, { "key_left", "Gauche" }, { "key_right", "Droite" },
    { "key_stick_up", "Stick ↑" }, { "key_stick_down", "Stick ↓" }, { "key_stick_left", "Stick ←" },
    { "key_stick_right", "Stick →" }, { "key_cross", "✕  Action" }, { "key_circle", "○  Annuler" },
    { "key_square", "□  Courir" }, { "key_triangle", "△  Triangle" }, { "key_l1", "L1" },
    { "key_r1", "R1  Viser" }, { "key_l2", "L2" }, { "key_r2", "R2" }, { "key_l3", "L3" },
    { "key_r3", "R3" }, { "key_start", "Start" }, { "key_select", "Select" },
    { "key_fast_forward", "Avance rapide" },
};

/* 4:3, then 16:9 */
static const char *const window_sizes[2][6] = {
    { "640x480", "960x720", "1280x960", "1600x1200", "1920x1440", "2560x1920" },
    { "854x480", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" },
};
/* sound_3d: off, headphones, speakers (pc_spatial.h) */
static const char *const sound3d_values[] = { "off", "headphones", "speakers" };
static const char *const sound3d_names[] = { "Non  (comme la PS2)", "Casque", "Enceintes" };
static const char *const textures_values[] = { "original", "hd", "dump" };
static const char *const textures_names[] = { "D'origine  (PS2)", "HD  (si présentes)", "HD + exporter" };
static const char *const upscale_names[] = {
    "×1  (640×448, la PS2)", "×2  (1280×896)", "×3  (1920×1344)", "×4  (2560×1792)",
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static int tab, focus = ID_PLAY, hover, capturing = -1, result = -1;
static char status[256];
static int status_colour;

/* ---- Text ---- */

typedef struct {
    const Font *font;
    SDL_Texture *tex;
    SDL_Rect rects[400];
    short latin[256]; /* glyph of code points below 256, or -1 */
} FontTex;

static FontTex text, title;

static int load_font(FontTex *ft, const Font *f)
{
    int x = 0, y = 0, row = 0, w = 512, h;
    uint32_t *pixels;

    ft->font = f;
    memset(ft->latin, -1, sizeof(ft->latin));
    for (int i = 0; i < f->count && i < 400; i++) {
        const FontGlyph *g = &f->glyphs[i];
        if (x + g->w + 1 > w) {
            x = 0;
            y += row + 1;
            row = 0;
        }
        ft->rects[i].x = x;
        ft->rects[i].y = y;
        ft->rects[i].w = g->w;
        ft->rects[i].h = g->h;
        x += g->w + 1;
        if (g->h > row)
            row = g->h;
        if (g->code < 256)
            ft->latin[g->code] = (short)i;
    }
    h = y + row;
    pixels = calloc((size_t)w * h, 4);
    if (pixels == NULL)
        return 0;
    for (int i = 0; i < f->count && i < 400; i++) {
        const FontGlyph *g = &f->glyphs[i];
        const char *p = f->pixels + g->offset;
        for (int gy = 0; gy < g->h; gy++) {
            for (int gx = 0; gx < g->w; gx++, p++) {
                int v = *p <= '9' ? *p - '0' : *p - 'a' + 10;
                pixels[(ft->rects[i].y + gy) * w + ft->rects[i].x + gx] = (uint32_t)(v * 17) << 24 | 0xffffff;
            }
        }
    }
    ft->tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (ft->tex != NULL) {
        SDL_UpdateTexture(ft->tex, NULL, pixels, w * 4);
        SDL_SetTextureBlendMode(ft->tex, SDL_BLENDMODE_BLEND);
    }
    free(pixels);
    return ft->tex != NULL;
}

/* The next character of s: UTF-8, or Latin-1 (Windows paths) for bytes
 * that are not. */
static unsigned next_char(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = p[0], cp;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;

    if (c < 0x80 || n == 0) {
        *s += 1;
        return c;
    }
    cp = c & (0x3fu >> n);
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xc0) != 0x80) {
            *s += 1;
            return c;
        }
        cp = cp << 6 | (p[i] & 0x3f);
    }
    *s += n + 1;
    return cp;
}

static int find_glyph(const FontTex *ft, unsigned cp)
{
    if (cp < 256)
        return ft->latin[cp] >= 0 ? ft->latin[cp] : ft->latin['?'];
    for (int i = 0; i < ft->font->count; i++) {
        if (ft->font->glyphs[i].code == cp)
            return i;
    }
    return ft->latin['?'];
}

static int text_width(const FontTex *ft, const char *s)
{
    int w = 0;

    while (*s)
        w += ft->font->glyphs[find_glyph(ft, next_char(&s))].advance;
    return w;
}

/* Draws s with the top of its line at y; returns its width. */
static int draw_text(const FontTex *ft, int x, int y, const char *s, int colour)
{
    int x0 = x;

    SDL_SetTextureColorMod(ft->tex, colour >> 16 & 0xff, colour >> 8 & 0xff, colour & 0xff);
    while (*s) {
        int i = find_glyph(ft, next_char(&s));
        const FontGlyph *g = &ft->font->glyphs[i];
        SDL_Rect dst = { x + g->x, y + g->y, g->w, g->h };
        if (g->w > 0)
            SDL_RenderCopy(renderer, ft->tex, &ft->rects[i], &dst);
        x += g->advance;
    }
    return x - x0;
}

/* s cut at the left with "…" so that it fits in width */
static const char *fit_left(const FontTex *ft, const char *s, int width, char *buf, size_t size)
{
    if (text_width(ft, s) <= width)
        return s;
    width -= text_width(ft, "…");
    while (*s && text_width(ft, s) > width)
        next_char(&s);
    snprintf(buf, size, "…%s", s);
    return buf;
}

static void draw_centred(const FontTex *ft, SDL_Rect r, const char *s, int colour)
{
    draw_text(ft, r.x + (r.w - text_width(ft, s)) / 2, r.y + (r.h - ft->font->height) / 2, s, colour);
}

/* ---- Shapes ---- */

static void colour(int c)
{
    SDL_SetRenderDrawColor(renderer, c >> 16 & 0xff, c >> 8 & 0xff, c & 0xff, 255);
}

static void fill(int x, int y, int w, int h, int c)
{
    SDL_Rect r = { x, y, w, h };
    colour(c);
    SDL_RenderFillRect(renderer, &r);
}

/* A rectangle with rounded corners of radius rad */
static void fill_round(SDL_Rect r, int rad, int c)
{
    colour(c);
    for (int i = 0; i < rad && i < r.h / 2; i++) {
        float dy = rad - i - 0.5f;
        int inset = (int)(rad - SDL_sqrtf((float)(rad * rad) - dy * dy) + 0.5f);
        SDL_RenderDrawLine(renderer, r.x + inset, r.y + i, r.x + r.w - 1 - inset, r.y + i);
        SDL_RenderDrawLine(renderer, r.x + inset, r.y + r.h - 1 - i, r.x + r.w - 1 - inset, r.y + r.h - 1 - i);
    }
    if (r.h > 2 * rad) {
        SDL_Rect mid = { r.x, r.y + rad, r.w, r.h - 2 * rad };
        SDL_RenderFillRect(renderer, &mid);
    }
}

static void ring(SDL_Rect r, int rad, int c, int inside)
{
    SDL_Rect in = { r.x + 2, r.y + 2, r.w - 4, r.h - 4 };
    fill_round(r, rad, c);
    fill_round(in, rad > 2 ? rad - 2 : 0, inside);
}

/* ---- Widgets: rebuilt at every drawing, for the mouse and the focus ---- */

static struct {
    int id;
    SDL_Rect r;
} widgets[64];
static int nwidgets;

static void add_widget(int id, SDL_Rect r)
{
    if (nwidgets < 64) {
        widgets[nwidgets].id = id;
        widgets[nwidgets].r = r;
        nwidgets++;
    }
}

static int widget_at(int x, int y)
{
    for (int i = 0; i < nwidgets; i++) {
        SDL_Rect *r = &widgets[i].r;
        if (x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h)
            return widgets[i].id;
    }
    return ID_NONE;
}

static int field_colour(int id)
{
    return hover == id ? C_FIELD_HOVER : C_FIELD;
}

/* Focus ring around the widget */
static void draw_focus(int id, SDL_Rect r, int rad)
{
    if (focus != id)
        return;
    SDL_Rect o = { r.x - 3, r.y - 3, r.w + 6, r.h + 6 };
    ring(o, rad + 3, C_ACCENT_HOVER, C_BACK);
}

static void button(int id, SDL_Rect r, const char *label, int primary)
{
    int c = primary ? (hover == id ? C_ACCENT_HOVER : C_ACCENT) : field_colour(id);

    draw_focus(id, r, 6);
    fill_round(r, 6, c);
    draw_centred(&text, r, label, C_TEXT);
    add_widget(id, r);
}

static void toggle(int id, int x, int y, int on)
{
    SDL_Rect r = { x, y, 120, 34 }, pill = { x, y + 3, 52, 28 };
    SDL_Rect knob = { on ? x + 28 : x + 4, y + 7, 20, 20 };

    draw_focus(id, pill, 14);
    fill_round(pill, 14, on ? (hover == id ? C_ACCENT_HOVER : C_ACCENT) : (hover == id ? 0x555b65 : C_OFF));
    fill_round(knob, 10, 0xf4f4f4);
    draw_text(&text, x + 64, y + (34 - text.font->height) / 2, on ? "Oui" : "Non", C_TEXT);
    add_widget(id, r);
}

static void choice(int id, int x, int y, int w, const char *value, int enabled)
{
    SDL_Rect r = { x, y, w, 34 };

    if (enabled)
        draw_focus(id, r, 6);
    fill_round(r, 6, enabled ? field_colour(id) : 0x1b1e23);
    draw_text(&text, x + 12, y + (34 - text.font->height) / 2, "◀", enabled ? C_DIM : C_LINE);
    draw_text(&text, x + w - 26, y + (34 - text.font->height) / 2, "▶", enabled ? C_DIM : C_LINE);
    draw_centred(&text, r, value, enabled ? C_TEXT : C_DIM);
    if (enabled)
        add_widget(id, r);
}

/* A setting's name and, below, a hint; the control goes at x 520. */
static void label(int y, const char *name, const char *hint)
{
    if (hint == NULL) {
        draw_text(&text, 40, y + (34 - text.font->height) / 2, name, C_TEXT);
    } else {
        draw_text(&text, 40, y - 4, name, C_TEXT);
        draw_text(&text, 40, y + 18, hint, C_DIM);
    }
}

/* ---- The settings ---- */

static int yes(const char *name, int otherwise)
{
    return pc_config_yes(name, otherwise);
}

static void read_settings(void)
{
    const char *v;

    v = pc_config_get("iso");
    if (v == NULL || v[0] == 0)
        v = getenv("CVX_ISO");
    snprintf(now.iso, sizeof(now.iso), "%s", v != NULL && v[0] != 0 ? v : "cvx.iso");
    now.widescreen = yes("widescreen", 0);
    now.fps60 = yes("fps60", 0);
    v = pc_config_get("textures");
    now.textures = 1;
    for (int i = 0; i < 3; i++) {
        if (v != NULL && SDL_strcasecmp(v, textures_values[i]) == 0)
            now.textures = i;
    }
    v = pc_config_get("window");
    snprintf(now.window, sizeof(now.window), "%s", v != NULL && v[0] != 0 ? v : window_sizes[now.widescreen][2]);
    now.fullscreen = yes("fullscreen", 0);
    now.launcher = yes("launcher", 1);
    v = pc_config_get("renderer");
    now.renderer = v != NULL && SDL_strcasecmp(v, "opengl") == 0;
    v = pc_config_get("upscale");
    upscale_set = v != NULL && v[0] != 0;
    now.upscale = upscale_set ? atoi(v) : 1;
    now.upscale = now.upscale < 1 ? 1 : now.upscale > 4 ? 4 : now.upscale;
    v = pc_config_get("filter");
    now.filter = v != NULL && SDL_strcasecmp(v, "smooth") == 0;
    now.sound = yes("sound", 1);
    v = pc_config_get("sound_3d");
    now.sound3d = 0;
    for (int i = 1; i < 3; i++) {
        if (v != NULL && SDL_strcasecmp(v, sound3d_values[i]) == 0)
            now.sound3d = i;
    }
    now.vibration = yes("vibration", 1);
    for (nkeys = 0; nkeys < MAX_KEYS; nkeys++) {
        key_setting[nkeys] = pc_window_key_setting(nkeys, &key_default[nkeys]);
        if (key_setting[nkeys] == NULL)
            break;
        v = pc_config_get(key_setting[nkeys]);
        snprintf(now.keys[nkeys], sizeof(now.keys[0]), "%s", v != NULL ? v : key_default[nkeys]);
    }
    before = now;
}

/* Puts what changed into the settings and saves cvx.ini. */
static void save_settings(void)
{
    int changed = 0;
    char number[8];

#define SET(field, name, value)                     \
    if (now.field != before.field) {                \
        pc_config_set(name, value);                 \
        changed++;                                  \
    }
    if (strcmp(now.iso, before.iso) != 0) {
        pc_config_set("iso", now.iso);
        changed++;
    }
    if (strcmp(now.window, before.window) != 0) {
        pc_config_set("window", now.window);
        changed++;
    }
    snprintf(number, sizeof(number), "%d", now.upscale);
    SET(fullscreen, "fullscreen", now.fullscreen ? "yes" : "no")
    SET(launcher, "launcher", now.launcher ? "yes" : "no")
    SET(renderer, "renderer", now.renderer ? "opengl" : "software")
    SET(upscale, "upscale", number)
    SET(filter, "filter", now.filter ? "smooth" : "sharp")
    SET(widescreen, "widescreen", now.widescreen ? "yes" : "no")
    SET(fps60, "fps60", now.fps60 ? "yes" : "no")
    SET(textures, "textures", textures_values[now.textures])
    SET(sound, "sound", now.sound ? "yes" : "no")
    SET(sound3d, "sound_3d", sound3d_values[now.sound3d])
    SET(vibration, "vibration", now.vibration ? "yes" : "no")
#undef SET
    for (int i = 0; i < nkeys; i++) {
        if (strcmp(now.keys[i], before.keys[i]) != 0) {
            pc_config_set(key_setting[i], now.keys[i]);
            changed++;
        }
    }
    if (changed == 0)
        return;
    if (pc_config_save("cvx.ini"))
        printf("launcher: %d setting(s) saved into cvx.ini\n", changed);
    else
        printf("launcher: cannot write cvx.ini, the settings are only for this time\n");
    before = now;
}

/* 0: the disc image is there, 1: no such file, 2: not a disc image */
static int iso_state(void)
{
    static char checked[512];
    static int state;
    unsigned char pvd[6];
    FILE *f;

    if (strcmp(checked, now.iso) == 0)
        return state;
    snprintf(checked, sizeof(checked), "%s", now.iso);
    f = fopen(now.iso, "rb");
    if (f == NULL)
        return state = 1;
    state = fseek(f, 16 * 2048, SEEK_SET) != 0 || fread(pvd, 1, 6, f) != 6 || memcmp(pvd + 1, "CD001", 5) != 0
                ? 2 : 0;
    fclose(f);
    return state;
}

static void set_status(int c, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
    status_colour = c;
}

/* The disc image chosen: kept as a name alone when it is in the game's
 * folder. */
static void set_iso(const char *path)
{
    char cwd[512];
    size_t len;

    if (getcwd(cwd, sizeof(cwd)) != NULL && (len = strlen(cwd)) > 0 && SDL_strncasecmp(path, cwd, len) == 0
        && (path[len] == '/' || path[len] == '\\') && strpbrk(path + len + 1, "/\\") == NULL)
        path += len + 1;
    snprintf(now.iso, sizeof(now.iso), "%s", path);
    if (iso_state() == 0)
        set_status(C_GOOD, "Image du disque choisie.");
}

#ifdef _WIN32
/* fopen takes the ANSI code page on Windows; SDL gives UTF-8. */
static void set_iso_utf8(const char *path)
{
    wchar_t wide[512];
    char ansi[512];
    BOOL lost = FALSE;

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 512) == 0
        || WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, sizeof(ansi), NULL, &lost) == 0 || lost) {
        set_status(C_BAD, "Le jeu ne sait pas ouvrir ce chemin : renomme le dossier sans caractères spéciaux.");
        return;
    }
    set_iso(ansi);
}
#else
#define set_iso_utf8 set_iso
#endif

static void browse(void)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char file[MAX_PATH] = "";
    SDL_SysWMinfo info;

    memset(&ofn, 0, sizeof(ofn));
    SDL_VERSION(&info.version);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = SDL_GetWindowWMInfo(window, &info) ? info.info.win.window : NULL;
    ofn.lpstrFilter = "Image du disque (*.iso)\0*.iso\0Tous les fichiers (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrTitle = "Image du disque de Code: Veronica X";
    /* NOCHANGEDIR: the saves and cvx.ini are in the current folder */
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&ofn))
        set_iso(file);
#else
    char file[512] = "";
    FILE *p = popen("zenity --file-selection --title='Image du disque' --file-filter='*.iso *.ISO' "
                    "--file-filter='*' 2>/dev/null", "r");

    if (p != NULL) {
        if (fgets(file, sizeof(file), p) == NULL)
            file[0] = 0;
        pclose(p);
    }
    file[strcspn(file, "\r\n")] = 0;
    if (file[0] != 0)
        set_iso(file);
    else
        set_status(C_DIM, "Glisse le fichier .iso sur cette fenêtre.");
#endif
}

/* Index of the window size in window_sizes, or -1 */
static int window_index(void)
{
    int w, h, sw, sh;

    if (sscanf(now.window, "%d x %d", &w, &h) != 2)
        return -1;
    for (int i = 0; i < 6; i++) {
        sscanf(window_sizes[now.widescreen][i], "%dx%d", &sw, &sh);
        if (sw == w && sh == h)
            return i;
    }
    return -1;
}

/* Removes key name from the other actions' lists; returns 1 if it was in
 * one, whose index goes into *from. */
static int take_key(int keep, const char *name, int *from)
{
    int found = 0;

    for (int a = 0; a < nkeys; a++) {
        char list[96] = "", *s;
        char copy[96];

        if (a == keep)
            continue;
        snprintf(copy, sizeof(copy), "%s", now.keys[a]);
        for (s = strtok(copy, ","); s != NULL; s = strtok(NULL, ",")) {
            while (*s == ' ')
                s++;
            size_t len = strlen(s);
            while (len > 0 && s[len - 1] == ' ')
                s[--len] = 0;
            if (SDL_strcasecmp(s, name) == 0) {
                found = 1;
                *from = a;
                continue;
            }
            if (len > 0)
                snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", list[0] ? ", " : "", s);
        }
        snprintf(now.keys[a], sizeof(now.keys[0]), "%s", list);
    }
    return found;
}

static const char *key_label(int a)
{
    for (size_t i = 0; i < sizeof(key_labels) / sizeof(key_labels[0]); i++) {
        if (strcmp(key_labels[i].setting, key_setting[a]) == 0)
            return key_labels[i].label;
    }
    return key_setting[a];
}

static void set_key(int a, SDL_Scancode sc)
{
    const char *name = SDL_GetScancodeName(sc);
    int from;

    if (name == NULL || name[0] == 0 || SDL_GetScancodeFromName(name) != sc) {
        set_status(C_BAD, "Cette touche ne peut pas être utilisée.");
        return;
    }
    if (take_key(a, name, &from))
        set_status(C_DIM, "« %s » est retirée de « %s ».", name, key_label(from));
    else
        status[0] = 0;
    snprintf(now.keys[a], sizeof(now.keys[0]), "%s", name);
}

static void play(void)
{
    if (iso_state() != 0) {
        tab = 0;
        focus = ID_BROWSE;
        set_status(C_BAD, "Choisis d'abord l'image du disque du jeu.");
        return;
    }
    result = 1;
}

/* Clicks, Return, A: dir is -1 for the left part of a choice */
static void activate(int id, int dir)
{
    int i;

    switch (id) {
    case ID_TAB0: case ID_TAB1: case ID_TAB2: case ID_TAB3:
        tab = id - ID_TAB0;
        status[0] = 0;
        break;
    case ID_ISO: case ID_BROWSE: browse(); break;
    case ID_FULLSCREEN: now.fullscreen ^= 1; break;
    case ID_LAUNCHER: now.launcher ^= 1; break;
    case ID_SOUND: now.sound ^= 1; break;
    case ID_SOUND3D: now.sound3d = (now.sound3d + dir + 3) % 3; break;
    case ID_VIBRATION: now.vibration ^= 1; break;
    case ID_FILTER: now.filter ^= 1; break;
    case ID_FPS60: now.fps60 ^= 1; break;
    case ID_TEXTURES: now.textures = (now.textures + dir + 3) % 3; break;
    case ID_WIDESCREEN:
        /* the window keeps its place in the list of sizes */
        i = window_index();
        now.widescreen ^= 1;
        if (i >= 0)
            snprintf(now.window, sizeof(now.window), "%s", window_sizes[now.widescreen][i]);
        break;
    case ID_RENDERER:
        now.renderer ^= 1;
        if (now.renderer && !upscale_set && now.upscale == 1)
            now.upscale = 2;
        break;
    case ID_UPSCALE:
        now.upscale = (now.upscale - 1 + dir + 4) % 4 + 1;
        break;
    case ID_WINDOW:
        i = window_index();
        if (i < 0)
            i = dir > 0 ? -1 : 0;
        i = (i + dir + 6) % 6;
        snprintf(now.window, sizeof(now.window), "%s", window_sizes[now.widescreen][i]);
        break;
    case ID_KEYS_DEFAULT:
        for (i = 0; i < nkeys; i++)
            snprintf(now.keys[i], sizeof(now.keys[0]), "%s", key_default[i]);
        set_status(C_DIM, "Touches d'origine remises.");
        break;
    case ID_QUIT: result = 0; break;
    case ID_PLAY: play(); break;
    default:
        if (id >= ID_KEY0 && id < ID_KEY0 + nkeys) {
            capturing = id - ID_KEY0;
            set_status(C_DIM, "Appuie sur la touche pour « %s ».", key_label(capturing));
        }
        break;
    }
}

/* Left and right on a setting change it; elsewhere they move the focus. */
static int changes(int id)
{
    return id == ID_FULLSCREEN || id == ID_LAUNCHER || id == ID_SOUND || id == ID_SOUND3D || id == ID_VIBRATION
           || id == ID_FILTER || id == ID_WIDESCREEN || id == ID_FPS60 || id == ID_TEXTURES || id == ID_RENDERER || id == ID_UPSCALE || id == ID_WINDOW;
}

/* Moves the focus to the nearest widget in that direction. */
static void move_focus(int dx, int dy)
{
    SDL_Rect *from = NULL;
    int best = -1, best_score = 1 << 30;

    for (int i = 0; i < nwidgets; i++) {
        if (widgets[i].id == focus)
            from = &widgets[i].r;
    }
    if (from == NULL) {
        focus = ID_PLAY;
        return;
    }
    int fx = from->x + from->w / 2, fy = from->y + from->h / 2;
    for (int i = 0; i < nwidgets; i++) {
        SDL_Rect *r = &widgets[i].r;
        int along, across;
        if (widgets[i].id == focus || widgets[i].id <= ID_TAB3)
            continue;
        if (dy != 0) {
            along = ((r->y + r->h / 2) - fy) * dy;
            across = abs(r->x + r->w / 2 - fx);
            if (fx >= r->x && fx < r->x + r->w)
                across = 0;
        } else {
            along = ((r->x + r->w / 2) - fx) * dx;
            across = abs(r->y + r->h / 2 - fy);
        }
        if (along <= 0)
            continue;
        if (along + across * 3 < best_score) {
            best_score = along + across * 3;
            best = widgets[i].id;
        }
    }
    if (best != -1)
        focus = best;
}

/* ---- Pages ---- */

static void page_general(void)
{
    char buf[600];
    SDL_Rect path = { 40, 182, 660, 38 }, more = { 712, 182, 128, 38 };
    int state = iso_state();
    int i = window_index();

    draw_text(&text, 40, 152, "Image du disque du jeu (fichier .iso)", C_TEXT);
    draw_focus(ID_ISO, path, 6);
    fill_round(path, 6, field_colour(ID_ISO));
    draw_text(&text, 52, path.y + (38 - text.font->height) / 2, fit_left(&text, now.iso, path.w - 24, buf, sizeof(buf)),
              C_TEXT);
    add_widget(ID_ISO, path);
    button(ID_BROWSE, more, "Parcourir…", 0);
    if (state == 0)
        draw_text(&text, 40, 230, "✓  Image du disque trouvée", C_GOOD);
    else if (state == 1)
        draw_text(&text, 40, 230, "✗  Fichier introuvable : clique sur Parcourir… ou glisse le .iso ici", C_BAD);
    else
        draw_text(&text, 40, 230, "✗  Ce fichier n'est pas une image de disque (.iso)", C_BAD);

    label(286, "Plein écran", "F11 pendant le jeu passe de l'un à l'autre");
    toggle(ID_FULLSCREEN, 520, 286, now.fullscreen);
    label(350, "Taille de la fenêtre", NULL);
    if (i >= 0) {
        int w, h;
        sscanf(window_sizes[now.widescreen][i], "%dx%d", &w, &h);
        snprintf(buf, sizeof(buf), "%d × %d", w, h);
    } else {
        snprintf(buf, sizeof(buf), "%s", now.window);
    }
    choice(ID_WINDOW, 520, 350, 320, buf, !now.fullscreen);
    label(414, "Montrer cette fenêtre au démarrage", "Sinon, garde Maj enfoncée en lançant le jeu");
    toggle(ID_LAUNCHER, 520, 414, now.launcher);
}

static void page_picture(void)
{
    label(152, "Dessin de l'image", "Carte graphique : image plus fine (OpenGL 3.3)");
    choice(ID_RENDERER, 520, 152, 320, now.renderer ? "Carte graphique" : "Processeur", 1);
    label(212, "Finesse de l'image", now.renderer ? "Plus fine demande une carte plus puissante"
                                                  : "Seulement avec la carte graphique");
    choice(ID_UPSCALE, 520, 212, 320, upscale_names[now.upscale - 1], now.renderer);
    label(272, "Lissage", "Lissée : moins de gros pixels visibles");
    choice(ID_FILTER, 520, 272, 320, now.filter ? "Lissée" : "Nette", 1);
    label(332, "Format de l'image", "16:9 : on voit plus large en jeu ; menus en 4:3");
    choice(ID_WIDESCREEN, 520, 332, 320, now.widescreen ? "16:9  (écran large)" : "4:3  (comme la PS2)", 1);
    label(392, "Images par seconde", "60 : mouvements plus fluides en jeu (essai)");
    choice(ID_FPS60, 520, 392, 320, now.fps60 ? "60  (plus fluide)" : "30  (comme la PS2)", 1);
    label(452, "Textures", now.renderer ? "HD : celles du dossier textures/replace (F9 : relire)"
                                        : "Seulement avec la carte graphique");
    choice(ID_TEXTURES, 520, 452, 320, textures_names[now.textures], now.renderer);
    draw_text(&text, 40, 500, "Si la carte graphique ne convient pas, le jeu revient tout seul", C_DIM);
    draw_text(&text, 40, 522, "au processeur (c'est noté dans cvx_log.txt).", C_DIM);
}

static void page_sound(void)
{
    int y = 360, n = SDL_NumJoysticks();

    label(156, "Son", NULL);
    toggle(ID_SOUND, 520, 156, now.sound);
    label(210, "Son 3D", now.sound3d == 1 ? "Devant, derrière : mieux au casque" : "Bruits placés autour de toi en jeu");
    choice(ID_SOUND3D, 520, 210, 320, sound3d_names[now.sound3d], now.sound);
    label(270, "Vibration de la manette", NULL);
    toggle(ID_VIBRATION, 520, 270, now.vibration);

    draw_text(&text, 40, 332, "Manettes branchées", C_TEXT);
    if (n <= 0) {
        draw_text(&text, 40, y, "Aucune pour l'instant : tu peux la brancher même pendant le jeu.", C_DIM);
        y += 22;
    }
    for (int i = 0; i < n && i < 4; i++, y += 22) {
        const char *name = SDL_IsGameController(i) ? SDL_GameControllerNameForIndex(i) : SDL_JoystickNameForIndex(i);
        char line[300];
        snprintf(line, sizeof(line), "•  %s", name != NULL ? name : "Manette");
        draw_text(&text, 40, y, line, C_GOOD);
    }
    y += 24;
    draw_text(&text, 40, y, "Manette Xbox : A = ✕   B = ○   X = □   Y = △   LB / RB = L1 / R1", C_DIM);
    draw_text(&text, 40, y + 22, "LT / RT = L2 / R2   Back = Select   (même place sur les autres)", C_DIM);
}

static void page_keys(void)
{
    for (int a = 0; a < nkeys; a++) {
        int col = a < 11 ? 0 : 1, row = a < 11 ? a : a - 11;
        int x = col == 0 ? 40 : 460, y = 148 + row * 35;
        SDL_Rect r = { x + 170, y, 210, 30 };
        char buf[128];
        int id = ID_KEY0 + a;

        draw_text(&text, x, y + (30 - text.font->height) / 2, key_label(a), C_TEXT);
        draw_focus(id, r, 5);
        if (capturing == a) {
            ring(r, 5, C_ACCENT, C_FIELD);
            draw_centred(&text, r, "Appuie sur une touche…", C_ACCENT_HOVER);
        } else {
            fill_round(r, 5, field_colour(id));
            if (now.keys[a][0] == 0)
                draw_centred(&text, r, "—", C_DIM);
            else
                draw_centred(&text, r, fit_left(&text, now.keys[a], r.w - 16, buf, sizeof(buf)), C_TEXT);
        }
        add_widget(id, r);
    }
    SDL_Rect def = { 630, 148 + 10 * 35, 210, 30 };
    button(ID_KEYS_DEFAULT, def, "Touches d'origine", 0);
}

static void draw(void)
{
    static const char *const tabs[] = { "Général", "Image", "Son et manette", "Touches" };
    int x = 40;

    nwidgets = 0;
    colour(C_BACK);
    SDL_RenderClear(renderer);
    for (int y = 0; y < 90; y++) {
        int t = y * 256 / 90;
        int c = 0;
        for (int sh = 0; sh < 24; sh += 8)
            c |= (((C_HEADER >> sh & 0xff) * (256 - t) + (C_BACK >> sh & 0xff) * t) >> 8) << sh;
        fill(0, y, WIDTH, 1, c);
    }
    draw_text(&title, 38, 16, "Resident Evil Code: Veronica X", C_TEXT);
    draw_text(&text, 40, 56, "Portage PC  ·  réglages, enregistrés dans cvx.ini", C_DIM);

    for (int i = 0; i < 4; i++) {
        int w = text_width(&text, tabs[i]);
        SDL_Rect r = { x - 10, 96, w + 20, 38 };
        int c = i == tab ? C_TEXT : hover == ID_TAB0 + i ? 0xc4c7cb : C_DIM;
        draw_text(&text, x, 104, tabs[i], c);
        if (i == tab)
            fill(x - 4, 131, w + 8, 3, C_ACCENT);
        add_widget(ID_TAB0 + i, r);
        x += w + 36;
    }
    fill(0, 134, WIDTH, 1, C_LINE);

    switch (tab) {
    case 0: page_general(); break;
    case 1: page_picture(); break;
    case 2: page_sound(); break;
    default: page_keys(); break;
    }

    fill(0, 548, WIDTH, 1, C_LINE);
    if (status[0] != 0)
        draw_text(&text, 40, 573, status, status_colour);
    else if (tab == 3)
        draw_text(&text, 40, 573, "Clic : changer la touche   ·   clic droit : aucune", C_DIM);
    else
        draw_text(&text, 40, 573, "Manette : croix pour choisir, A pour valider, LB / RB : pages", C_DIM);
    SDL_Rect quit = { 588, 562, 120, 40 }, go = { 720, 562, 120, 40 };
    button(ID_QUIT, quit, "Quitter", 0);
    button(ID_PLAY, go, "Jouer", 1);
}

/* ---- Input ---- */

static void key_press(SDL_Keysym k)
{
    if (capturing >= 0) {
        set_key(capturing, k.scancode);
        capturing = -1;
        return;
    }
    switch (k.sym) {
    case SDLK_UP: move_focus(0, -1); break;
    case SDLK_DOWN: move_focus(0, 1); break;
    case SDLK_LEFT:
        if (changes(focus))
            activate(focus, -1);
        else
            move_focus(-1, 0);
        break;
    case SDLK_RIGHT:
        if (changes(focus))
            activate(focus, 1);
        else
            move_focus(1, 0);
        break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
        activate(focus, 1);
        break;
    case SDLK_TAB: case SDLK_PAGEDOWN: case SDLK_PAGEUP:
        tab = (tab + ((k.sym == SDLK_PAGEUP || (k.mod & KMOD_SHIFT)) ? 3 : 1)) % 4;
        status[0] = 0;
        focus = ID_PLAY;
        break;
    }
}

static void pad_press(int button)
{
    if (capturing >= 0) {
        capturing = -1;
        status[0] = 0;
        return;
    }
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: move_focus(0, -1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move_focus(0, 1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        if (changes(focus))
            activate(focus, -1);
        else
            move_focus(-1, 0);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        if (changes(focus))
            activate(focus, 1);
        else
            move_focus(1, 0);
        break;
    case SDL_CONTROLLER_BUTTON_A:
        /* no keyboard keys from a controller */
        if (focus < ID_KEY0)
            activate(focus, 1);
        break;
    case SDL_CONTROLLER_BUTTON_START: play(); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        tab = (tab + (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? 3 : 1)) % 4;
        status[0] = 0;
        focus = ID_PLAY;
        break;
    }
}

static void handle(const SDL_Event *ev)
{
    static int stick_x, stick_y; /* left stick past half way: -1, 0, 1 */
    int id;

    switch (ev->type) {
    case SDL_QUIT:
        result = 0;
        break;
    case SDL_KEYDOWN:
        if (!ev->key.repeat || capturing < 0)
            key_press(ev->key.keysym);
        break;
    case SDL_MOUSEMOTION:
        hover = widget_at(ev->motion.x, ev->motion.y);
        break;
    case SDL_MOUSEBUTTONDOWN:
        id = widget_at(ev->button.x, ev->button.y);
        if (capturing >= 0) {
            capturing = -1;
            status[0] = 0;
            break;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT) {
            if (id >= ID_KEY0 && id < ID_KEY0 + nkeys)
                now.keys[id - ID_KEY0][0] = 0;
            break;
        }
        if (ev->button.button != SDL_BUTTON_LEFT || id == ID_NONE)
            break;
        if (id > ID_TAB3)
            focus = id;
        for (int i = 0; i < nwidgets; i++) {
            if (widgets[i].id == id)
                activate(id, ev->button.x < widgets[i].r.x + widgets[i].r.w / 3 ? -1 : 1);
        }
        break;
    case SDL_DROPFILE:
        set_iso_utf8(ev->drop.file);
        SDL_free(ev->drop.file);
        tab = 0;
        break;
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(ev->cdevice.which);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        pad_press(ev->cbutton.button);
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int *last = ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? &stick_x : &stick_y;
            int v = ev->caxis.value < -20000 ? -1 : ev->caxis.value > 20000 ? 1 : 0;
            if (v != 0 && v != *last) {
                if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                    pad_press(v < 0 ? SDL_CONTROLLER_BUTTON_DPAD_UP : SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                else
                    pad_press(v < 0 ? SDL_CONTROLLER_BUTTON_DPAD_LEFT : SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            }
            *last = v;
        }
        break;
    }
}

/* CVX_LAUNCHER_SHOTS: each page into a picture */
static void save_pages(const char *prefix)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);

    for (tab = 0; tab < 4 && s != NULL; tab++) {
        char path[512];
        draw();
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        snprintf(path, sizeof(path), "%s%d.bmp", prefix, tab);
        if (SDL_SaveBMP(s, path) != 0)
            printf("launcher: cannot save %s\n", path);
    }
    SDL_FreeSurface(s);
}

int pc_launcher_run(int argc, char *argv[])
{
    const char *shots = getenv("CVX_LAUNCHER_SHOTS");
    int ask;

    if (getenv("CVX_HEADLESS") != NULL)
        return 1;
    read_settings();
    ask = now.launcher || iso_state() != 0 || shots != NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-config") == 0)
            ask = 1;
    }
#ifdef _WIN32
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        ask = 1;
#endif
    if (!ask)
        return 1;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("launcher: no settings window (%s)\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Code: Veronica X - réglages", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH,
                              HEIGHT, 0);
    if (window != NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        if (renderer == NULL)
            renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (renderer == NULL || !load_font(&text, &font_text) || !load_font(&title, &font_title)) {
        printf("launcher: no settings window (%s)\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    if (iso_state() != 0)
        focus = ID_BROWSE;

    if (shots != NULL) {
        save_pages(shots);
        result = 0;
    }
    while (result < 0) {
        SDL_Event ev;
        draw();
        SDL_RenderPresent(renderer);
        /* a timeout to see controllers come and go */
        if (SDL_WaitEventTimeout(&ev, 500)) {
            handle(&ev);
            while (result < 0 && SDL_PollEvent(&ev))
                handle(&ev);
        }
    }
    if (shots == NULL)
        save_settings();

    SDL_DestroyTexture(text.tex);
    SDL_DestroyTexture(title.tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = NULL;
    window = NULL;
    /* The game window starts SDL again, and sees the controllers anew. */
    SDL_Quit();
    if (result == 0)
        printf("launcher: quit from the settings window\n");
    return result;
}

#else /* !CVX_HAVE_SDL */

int pc_launcher_run(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 1;
}

#endif
