/*
 * Plays port/tests/data/test.pss (made by make_test_pss.py: red then blue
 * MPEG-2 pictures and a PCM sine, no game data) from the test ISO with the
 * PC movie player, the way the game drives it, and checks the pictures
 * uploaded to GS memory, the sound, the end of the movie and skipping.
 * Then again with a replacement movie (movies/MV_000.avi, made by
 * make_test_avi.py: green then yellow) shown over the picture.
 */
#include "ps2_MovieFunc.h"

#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs.h"
#include "../src/gs/gs_mem.h"
#include "../src/host/pc_host.h"
#include "../src/platform/pc_disc.h"

extern unsigned short pc_pad_buttons;

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* The picture uploaded by setImageTag: GS block 14208, 320 pixels wide. */
static unsigned int picture(int x, int y)
{
    return gs_read_pixel(GS_PSMCT32, 14208, 5, x, y);
}

static int is_red(unsigned int c)
{
    return (c & 0xFF) > 200 && ((c >> 8) & 0xFF) < 60 && ((c >> 16) & 0xFF) < 60;
}

static int is_blue(unsigned int c)
{
    return (c & 0xFF) < 60 && ((c >> 8) & 0xFF) < 60 && ((c >> 16) & 0xFF) > 200;
}

static void start(void)
{
    unsigned int lsn, size;

    CHECK(pc_disc_find("\\MOVIE\\MV_000.PSS;1", &lsn, &size));
    infile.fp.lsn = lsn;
    infile.size = size;
    strcpy(infile.fp.name, "MV_000.PSS;1");
    initAll();
}

/* One game frame: mwPlyExecServer. */
static void exec_server(void)
{
    movie_draw = 0;
    do
    {
        readMpeg();
    } while (movie_draw == 0 && rmi.iMovieState != 3);
    setImageTag((unsigned int*)test_tag, NULL);
}

int main(void)
{
    int frames = 0;
    int red = 0, blue = 0;
    int i;

    gs_reset();
    if (!pc_disc_open())
    {
        printf("test_movie: cannot open the test ISO (CVX_ISO)\n");
        return 1;
    }

    pc_audio_open = 1; /* no device: the samples stay queued */

    start();
    if (rmi.iMovieState == 3)
    {
        printf("test_movie: no video decoder in this build, skipped\n");
        return 0;
    }

    for (i = 0; i < 300 && rmi.iMovieState != 3; i++)
    {
        exec_server();
        if (rmi.iMovieState == 3)
            break;
        frames++;
        if (frames == 1)
            CHECK(is_red(picture(160, 120)));
        red |= is_red(picture(10, 10));
        blue |= is_blue(picture(310, 230));
        pc_host_sleep_ns(16666667);
    }

    printf("movie: %d game frames, red %d, blue %d, %d sound frames queued\n", frames, red, blue,
           pc_audio_queued());
    CHECK(rmi.iMovieState == 3);
    /* 15 pictures at 29.97 per second, about 30 game frames. */
    CHECK(frames >= 25 && frames <= 40);
    CHECK(red && blue);
    /* 0.5 s of sound at 8 kHz, resampled to 48 kHz. */
    CHECK(pc_audio_queued() > PC_AUDIO_RATE / 3);
    CHECK(pc_overlay.rgba == NULL); /* no replacement movie */
    termAll();

    /* Skipping: a button held at the start does nothing until released. */
    pc_audio_clear();
    pc_pad_buttons = (unsigned short)~0x0800; /* Start held */
    start();
    exec_server();
    exec_server();
    CHECK(rmi.iMovieState == 2);
    pc_pad_buttons = 0xFFFF;
    exec_server();
    CHECK(rmi.iMovieState == 2);
    pc_pad_buttons = (unsigned short)~0x0020; /* Circle */
    exec_server();
    CHECK(rmi.iMovieState == 3);
    termAll();

    /* With a replacement movie: its pictures are shown over the movie's
     * area, in time with the movie, at their own size. */
    if (getenv("CVX_TEST_MOVIES") != NULL)
    {
        static char env[600];
        unsigned int serial;
        int green = 0, yellow = 0;

        snprintf(env, sizeof(env), "CVX_MOVIES=%s", getenv("CVX_TEST_MOVIES"));
        putenv(env);
        pc_audio_clear();
        pc_pad_buttons = 0xFFFF;
        start();
        exec_server();
        CHECK(pc_overlay.rgba != NULL);
        if (pc_overlay.rgba != NULL)
        {
            printf("replacement: %dx%d at (%d, %d) %dx%d, first pixel %08X\n", pc_overlay.w, pc_overlay.h,
                   pc_overlay.x, pc_overlay.y, pc_overlay.cw, pc_overlay.ch, pc_overlay.rgba[0]);
            CHECK(pc_overlay.w == 64 && pc_overlay.h == 48);
            CHECK(pc_overlay.cw == 640 && pc_overlay.ch == 448);
        }
        serial = pc_overlay.serial;
        for (i = 0; i < 300 && rmi.iMovieState != 3; i++)
        {
            if (pc_overlay.rgba != NULL)
            {
                green |= pc_overlay.rgba[100] == 0xFF00FF00u;
                yellow |= pc_overlay.rgba[100] == 0xFF00FFFFu;
            }
            pc_host_sleep_ns(16666667);
            exec_server();
        }
        printf("replacement: green %d, yellow %d, %u pictures\n", green, yellow, pc_overlay.serial - serial);
        CHECK(green && yellow);
        CHECK(pc_overlay.serial - serial >= 10);
        termAll();
        CHECK(pc_overlay.rgba == NULL);
    }

    if (failures == 0)
        printf("test_movie: all checks passed\n");
    return failures != 0;
}
