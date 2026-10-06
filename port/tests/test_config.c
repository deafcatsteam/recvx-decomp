/*
 * The settings file cvx.ini (port/src/host/pc_config.c): comments, spaces,
 * quotes, yes/no values, the settings turned into environment variables and
 * a variable set by hand winning over the file; changing settings and
 * writing them back into a file, which keeps its comments.
 */
#include "../src/host/pc_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static int is(const char *a, const char *b)
{
    return a != NULL && strcmp(a, b) == 0;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

/* The file, or "" (to be freed). */
static char *read_file(const char *path)
{
    static char text[8192];
    FILE *f = fopen(path, "rb");
    size_t n = 0;

    if (f != NULL) {
        n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
    }
    text[n] = 0;
    return text;
}

int main(void)
{
#ifdef _WIN32
    _putenv_s("CVX_SAVES", "by hand");
#else
    setenv("CVX_SAVES", "by hand", 1);
#endif
    pc_config_parse("\xef\xbb\xbf; a comment\r\n"
                    "# another\r\n"
                    "[section]\r\n"
                    "  iso =  \"C:\\Program Files\\cvx.iso\"  \r\n"
                    "saves = mine\r\n"
                    "Fullscreen=Yes\n"
                    "sound = non\n"
                    "vibration = maybe\n"
                    "key_cross = Space, Return\n"
                    "key_l3 =\n"
                    "cvx_gs_threads = 3\n"
                    "colour = blue\n"
                    "no equals sign\n"
                    ";window = 640x480\n"
                    "window = 800x600");

    CHECK(is(pc_config_get("iso"), "C:\\Program Files\\cvx.iso"));
    CHECK(is(getenv("CVX_ISO"), "C:\\Program Files\\cvx.iso"));
    CHECK(is(pc_config_get("saves"), "mine"));
    CHECK(is(getenv("CVX_SAVES"), "by hand"));
    CHECK(pc_config_yes("fullscreen", 0) == 1);
    CHECK(pc_config_yes("FULLSCREEN", 0) == 1);
    CHECK(pc_config_yes("sound", 1) == 0);
    CHECK(getenv("CVX_NO_AUDIO") != NULL);
    CHECK(pc_config_yes("vibration", 1) == 1); /* not yes or no: the default */
    CHECK(pc_config_yes("filter", 0) == 0);    /* not set */
    CHECK(is(pc_config_get("key_cross"), "Space, Return"));
    CHECK(is(pc_config_get("key_l3"), ""));
    CHECK(is(getenv("CVX_GS_THREADS"), "3"));
    CHECK(pc_config_get("colour") == NULL);
    CHECK(is(pc_config_get("window"), "800x600"));

    pc_config_report(); /* the unknown setting, the line without '=', "maybe" */

    /* Changes, as the settings window makes them */
    pc_config_set("saves", "other");
    CHECK(is(pc_config_get("saves"), "other"));
    CHECK(is(getenv("CVX_SAVES"), "by hand")); /* still */
    pc_config_set("iso", "D:\\cvx.iso");
    CHECK(is(getenv("CVX_ISO"), "D:\\cvx.iso"));
    pc_config_set("sound", "yes");
    CHECK(is(pc_config_get("SOUND"), "yes"));
    CHECK(getenv("CVX_NO_AUDIO") == NULL || getenv("CVX_NO_AUDIO")[0] == 0);
    pc_config_set("cvx_gs_threads", NULL);
    CHECK(pc_config_get("cvx_gs_threads") == NULL);
    CHECK(getenv("CVX_GS_THREADS") == NULL || getenv("CVX_GS_THREADS")[0] == 0);

    /* Written back: values on their own lines, a commented line of the
     * template used, the second line of a setting gone, a removed setting
     * commented out, the others at the end. */
    write_file("test_config.ini", "; my comment\r\n"
                                  "iso = old\r\n"
                                  ";fullscreen = yes\r\n"
                                  "window = 640x480\r\n"
                                  "window = 320x240\r\n"
                                  "cvx_gs_threads = 2\r\n"
                                  "\r\n"
                                  ";key_l3 = Z\r\n"
                                  "key_l3 = X\r\n");
    CHECK(pc_config_save("test_config.ini"));
    CHECK(is(read_file("test_config.ini"), "; my comment\r\n"
                                           "iso = D:\\cvx.iso\r\n"
                                           "Fullscreen = Yes\r\n"
                                           "window = 800x600\r\n"
                                           ";cvx_gs_threads = 2\r\n"
                                           "\r\n"
                                           ";key_l3 = Z\r\n"
                                           "key_l3 =\r\n"
                                           "vibration = maybe\r\n"
                                           "key_cross = Space, Return\r\n"
                                           "saves = other\r\n"
                                           "sound = yes\r\n"));
    if (failures)
        printf("%s", read_file("test_config.ini"));
    /* No file yet: the template with the settings in it */
    remove("test_config.ini");
    CHECK(pc_config_save("test_config.ini"));
    CHECK(strstr(read_file("test_config.ini"), "\niso = D:\\cvx.iso\n;saves = saves\n") == NULL);
    CHECK(strstr(read_file("test_config.ini"), "\niso = D:\\cvx.iso\n") != NULL);
    CHECK(strstr(read_file("test_config.ini"), "\nsaves = other\n") != NULL);
    CHECK(strstr(read_file("test_config.ini"), "\nkey_cross = Space, Return\n") != NULL);
    CHECK(strstr(read_file("test_config.ini"), ";key_up = Up\n") != NULL);
    CHECK(strstr(read_file("test_config.ini"), "Réglages") != NULL);
    remove("test_config.ini");

    if (failures == 0)
        printf("config: all checks passed\n");
    return failures != 0;
}
