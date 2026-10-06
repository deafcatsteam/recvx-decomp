/*
 * Settings file of the PC port: cvx.ini in the current folder (next to the
 * game when it is started by a double click).
 *
 * Each line is "name = value"; lines starting with ; or # are comments. The
 * settings that the rest of the port reads as environment variables (iso,
 * saves, movies, sound, and any CVX_... name written as is) are turned into
 * those variables, unless the variable is already set: a variable set by
 * hand wins over the file. The others (window, keys, ...) are read with
 * pc_config_get().
 *
 * When there is no cvx.ini, one is written with every setting commented
 * out, so the player can see what can be changed.
 */
#include "pc_host.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETTINGS 128

static struct {
    char name[48];
    char value[464];
} settings[MAX_SETTINGS];
static int nsettings;
static char report[1024];

static const char template_text[] =
    "; Réglages de Resident Evil Code: Veronica X (portage PC)\n"
    "; Enlève le ; au début d'une ligne pour l'activer, puis relance le jeu.\n"
    "\n"
    "; Image du disque (par défaut : cvx.iso dans ce dossier)\n"
    ";iso = C:\\Jeux\\cvx.iso\n"
    "\n"
    "; Dossier des sauvegardes (par défaut : saves)\n"
    ";saves = saves\n"
    "\n"
    "; Dossier des vidéos HD de remplacement (par défaut : movies, puis MOVIE)\n"
    ";movies = movies\n"
    "\n"
    "; Fenêtre\n"
    ";fullscreen = yes\n"
    ";window = 1280x960\n"
    "; sharp : pixels nets ; smooth : image lissée\n"
    ";filter = smooth\n"
    "\n"
    "; Dessin par la carte graphique (OpenGL 3.3) au lieu du processeur, et\n"
    "; sa résolution : 1 = celle de la PS2, 2 = deux fois plus fine... jusqu'à 4\n"
    ";renderer = opengl\n"
    ";upscale = 2\n"
    "\n"
    "; Son et vibration de la manette\n"
    ";sound = no\n"
    ";vibration = no\n"
    "\n"
    "; Touches du clavier. Noms : A..Z, 0..9, Space, Return, Escape, Tab,\n"
    "; Backspace, Left Shift, Right Shift, Left Ctrl, Right Ctrl, Left Alt,\n"
    "; Up, Down, Left, Right, Keypad 8... Plusieurs touches : Space, Return\n"
    "; Rien après le = : aucune touche.\n"
    ";key_up = Up\n"
    ";key_down = Down\n"
    ";key_left = Left\n"
    ";key_right = Right\n"
    ";key_stick_up = W\n"
    ";key_stick_down = S\n"
    ";key_stick_left = A\n"
    ";key_stick_right = D\n"
    ";key_cross = Space\n"
    ";key_circle = Escape\n"
    ";key_square = Left Shift\n"
    ";key_triangle = E\n"
    ";key_l1 = Q\n"
    ";key_r1 = Left Ctrl\n"
    ";key_l2 = 1\n"
    ";key_r2 = 3\n"
    ";key_l3 =\n"
    ";key_r3 =\n"
    ";key_start = Return\n"
    ";key_select = Backspace\n"
    ";key_fast_forward = Tab\n";

/* Messages kept until the log is open (pc_config_report). */
static void add_report(const char *fmt, ...)
{
    size_t len = strlen(report);
    va_list ap;

    va_start(ap, fmt);
    if (len < sizeof(report) - 1)
        vsnprintf(report + len, sizeof(report) - len, fmt, ap);
    va_end(ap);
}

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
    /* "C:\Program Files\cvx.iso" may be written in quotes */
    if (end - s >= 2 && s[0] == '"' && end[-1] == '"') {
        end[-1] = 0;
        s++;
    }
    return s;
}

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    }
    return *a == *b;
}

const char *pc_config_get(const char *name)
{
    for (int i = nsettings - 1; i >= 0; i--) {
        if (same(settings[i].name, name))
            return settings[i].value;
    }
    return NULL;
}

int pc_config_yes(const char *name, int otherwise)
{
    const char *v = pc_config_get(name);

    if (v == NULL || v[0] == 0)
        return otherwise;
    if (same(v, "yes") || same(v, "on") || same(v, "true") || same(v, "1") || same(v, "oui"))
        return 1;
    if (same(v, "no") || same(v, "off") || same(v, "false") || same(v, "0") || same(v, "non"))
        return 0;
    add_report("config: '%s' should be yes or no\n", name);
    return otherwise;
}

static void set_env(const char *name, const char *value)
{
    if (getenv(name) != NULL)
        return;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}

/* Settings the port reads as environment variables. */
static void apply(const char *name, const char *value)
{
    static const struct {
        const char *name, *env;
    } paths[] = {
        { "iso", "CVX_ISO" }, { "saves", "CVX_SAVES" }, { "movies", "CVX_MOVIES" },
    };

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (same(name, paths[i].name) && value[0] != 0)
            set_env(paths[i].env, value);
    }
    if (same(name, "sound") && !pc_config_yes("sound", 1))
        set_env("CVX_NO_AUDIO", "1");
    if (strncmp(name, "CVX_", 4) == 0 || strncmp(name, "cvx_", 4) == 0) {
        char env[48];
        for (int i = 0; ; i++) {
            env[i] = (char)toupper((unsigned char)name[i]);
            if (name[i] == 0)
                break;
        }
        set_env(env, value);
    }
}

static int known(const char *name)
{
    static const char *const names[] = {
        "iso", "saves", "movies", "fullscreen", "window", "filter", "sound", "vibration", "renderer",
        "upscale",
        /* the keys, see key_actions in pc_window.c */
        "key_up", "key_down", "key_left", "key_right", "key_stick_up", "key_stick_down",
        "key_stick_left", "key_stick_right", "key_cross", "key_circle", "key_square",
        "key_triangle", "key_l1", "key_r1", "key_l2", "key_r2", "key_l3", "key_r3",
        "key_start", "key_select", "key_fast_forward",
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (same(name, names[i]))
            return 1;
    }
    return strncmp(name, "CVX_", 4) == 0 || strncmp(name, "cvx_", 4) == 0;
}

void pc_config_parse(const char *text)
{
    char line[512];
    int number = 0;

    while (*text) {
        size_t len = strcspn(text, "\r\n");
        char *s, *eq, *name, *value;

        number++;
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, text, len);
        line[len] = 0;
        text += strcspn(text, "\r\n");
        if (*text == '\r')
            text++;
        if (*text == '\n')
            text++;

        s = line;
        if (number == 1 && memcmp(s, "\xef\xbb\xbf", 3) == 0)
            s += 3; /* UTF-8 mark some editors write first */
        s = trim(s);
        if (s[0] == 0 || s[0] == ';' || s[0] == '#' || s[0] == '[')
            continue;
        eq = strchr(s, '=');
        if (eq == NULL) {
            add_report("config: cvx.ini line %d has no '='\n", number);
            continue;
        }
        *eq = 0;
        name = trim(s);
        value = trim(eq + 1);
        if (!known(name) || strlen(name) >= sizeof(settings[0].name)) {
            add_report("config: unknown setting '%s' (line %d)\n", name, number);
            continue;
        }
        if (nsettings == MAX_SETTINGS)
            break;
        snprintf(settings[nsettings].name, sizeof(settings[0].name), "%s", name);
        snprintf(settings[nsettings].value, sizeof(settings[0].value), "%s", value);
        nsettings++;
        apply(name, value);
    }
}

void pc_config_load(void)
{
    FILE *f = fopen("cvx.ini", "rb");
    char *text;
    long size;

    if (f == NULL) {
        /* Not in the tests' runs: only for a player starting the game. */
        if (getenv("CVX_HEADLESS") != NULL)
            return;
        f = fopen("cvx.ini", "wb");
        if (f != NULL) {
            fwrite(template_text, 1, sizeof(template_text) - 1, f);
            fclose(f);
            add_report("config: wrote cvx.ini (every setting is off; see inside)\n");
        }
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 1 << 20 || (text = malloc((size_t)size + 1)) == NULL) {
        fclose(f);
        return;
    }
    size = (long)fread(text, 1, (size_t)size, f);
    text[size] = 0;
    fclose(f);
    pc_config_parse(text);
    free(text);
    add_report("config: cvx.ini read, %d setting(s) on\n", nsettings);
}

void pc_config_report(void)
{
    fputs(report, stdout);
    report[0] = 0;
}
