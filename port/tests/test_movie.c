/*
 * Plays port/tests/data/test.pss (made by make_test_pss.py: red then blue
 * MPEG-2 pictures and a PCM sine, no game data) from the test ISO with the
 * PC movie player, the way the game drives it, and checks the pictures
 * uploaded to GS memory, the sound, the end of the movie and skipping.
 */
#include "ps2_MovieFunc.h"

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

    if (failures == 0)
        printf("test_movie: all checks passed\n");
    return failures != 0;
}
