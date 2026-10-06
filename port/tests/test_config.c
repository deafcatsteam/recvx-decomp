/*
 * The settings file cvx.ini (port/src/host/pc_config.c): comments, spaces,
 * quotes, yes/no values, the settings turned into environment variables and
 * a variable set by hand winning over the file.
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

    if (failures == 0)
        printf("config: all checks passed\n");
    return failures != 0;
}
