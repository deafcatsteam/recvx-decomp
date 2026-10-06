/*
 * PC stand-in for ps2_MovieFunc.c, the movie player. The PS2 decodes the
 * MPEG-2 video of the .PSS files with its IPU and streams their sound to the
 * IOP; here the file is read from the disc and demultiplexed in C, the video
 * is decoded by pc_video.c (FFmpeg) and the sound goes to pc_audio.c.
 *
 * The game calls readMpeg() until a frame is ready to show (movie_draw) or
 * the movie is over (rmi.iMovieState == 3), then setImageTag() and
 * vbrank_draw() to put the frame on screen, like on the PS2: the frame is
 * uploaded to GS memory and drawn as a sprite stretched to the screen.
 *
 * Start, Select or Circle (Enter, Backspace or Escape) ends any movie.
 *
 * Replacement movies: a video file named after the movie in the "movies"
 * folder (or the folder in CVX_MOVIES), such as movies/MV_000.mp4, is shown
 * instead of the movie's picture, at its own resolution, over the movie's
 * area of the screen. The .PSS file still plays underneath: it gives the
 * sound, the timing and the end, so the replacement must keep the
 * original's length and frame timing (an upscaled copy does).
 */
#include "ps2_MovieFunc.h"
#include "ps2_dummy.h"
#include "ps2_loadtim2.h"

#include "../gs/gs.h"
#include "../gs/gs_mem.h"
#include "../host/pc_host.h"
#include "../platform/pc_disc.h"

RMI_WORK rmi;
MDSIZE_WORK mdSize;
int movie_draw;
StrFile infile;
u_long128 test_tag[1400];
VoBuf voBuf;

static u_long128 draw_tags[16] __attribute__((aligned(16)));

extern unsigned short pc_pad_buttons; /* pc_sdk.c, PS2 pad bits, 0 = pressed */

#define PAD_SELECT 0x0100
#define PAD_START 0x0800
#define PAD_CIRCLE 0x0020
#define SKIP_BUTTONS (PAD_SELECT | PAD_START | PAD_CIRCLE)

/* The frame goes where the PS2 player puts it: GS block 14208. */
#define IMAGE_TBP 14208
#define IMAGE_MAX_PIXELS ((4 * 1024 * 1024 - IMAGE_TBP * 256) / 4)

#define READ_SECTORS 32

static struct {
    PcVideo* video;
    int open;

    /* Disc reading */
    unsigned int lsn, size, pos;
    unsigned char buf[READ_SECTORS * PC_DISC_SECTOR];
    unsigned int buf_len, buf_pos;
    int eof;

    /* Sound: the "SShd" header, then interleaved blocks per channel. */
    unsigned char au_hdr[40];
    int au_hdr_len;
    int au_codec, au_rate, au_channels, au_interleave;
    unsigned char* au_data;
    int au_len, au_cap;
    int adpcm_hist[2][2];
    int volume; /* 0..0x8000 */

    /* Timing */
    double fps;
    int64_t clock_ns, last_ns;
    int shown;      /* frames shown */
    int skip_armed; /* the skip buttons were released since the start */

    /* Picture */
    const uint32_t* frame;
    int w, h;
    int uploaded;

    /* Replacement movie */
    PcHdMovie* hd;
} mv;

/* ---- Reading the file ------------------------------------------------------ */

static int fill(void)
{
    unsigned int n;

    if (mv.buf_pos < mv.buf_len)
    {
        memmove(mv.buf, mv.buf + mv.buf_pos, mv.buf_len - mv.buf_pos);
    }
    mv.buf_len -= mv.buf_pos;
    mv.buf_pos = 0;

    if (mv.pos >= mv.size)
    {
        return 0;
    }

    n = READ_SECTORS - (mv.buf_len + PC_DISC_SECTOR - 1) / PC_DISC_SECTOR;
    if (n == 0)
    {
        return 1;
    }
    if (n * PC_DISC_SECTOR > mv.size - mv.pos)
    {
        n = (mv.size - mv.pos + PC_DISC_SECTOR - 1) / PC_DISC_SECTOR;
    }

    {
        unsigned char tmp[READ_SECTORS * PC_DISC_SECTOR];
        unsigned int bytes = n * PC_DISC_SECTOR;

        if (!pc_disc_read(mv.lsn + mv.pos / PC_DISC_SECTOR, n, tmp))
        {
            mv.pos = mv.size;
            return 0;
        }
        if (bytes > mv.size - mv.pos)
        {
            bytes = mv.size - mv.pos;
        }
        if (bytes > sizeof(mv.buf) - mv.buf_len)
        {
            bytes = sizeof(mv.buf) - mv.buf_len;
        }
        memcpy(mv.buf + mv.buf_len, tmp, bytes);
        mv.buf_len += bytes;
        mv.pos += bytes;
    }
    return 1;
}

/* Makes at least n bytes available at mv.buf + mv.buf_pos. */
static int need(unsigned int n)
{
    while (mv.buf_len - mv.buf_pos < n)
    {
        unsigned int before = mv.buf_len - mv.buf_pos;

        if (!fill() || mv.buf_len - mv.buf_pos == before)
        {
            return 0;
        }
    }
    return 1;
}

/* ---- Sound ------------------------------------------------------------------ */

static void audio_out(int16_t* lr, int frames)
{
    int i;

    if (mv.volume != 0x8000)
    {
        for (i = 0; i < frames * 2; i++)
        {
            lr[i] = (int16_t)((lr[i] * mv.volume) >> 15);
        }
    }
    pc_audio_push(lr, frames, mv.au_rate);
}

/* PS ADPCM: 16-byte frames of 28 samples. */
static void adpcm_frame(const unsigned char* in, int* hist, int16_t* out)
{
    static const int coef[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };
    int shift = in[0] & 0xF;
    int filter = (in[0] >> 4) % 5;
    int i;

    for (i = 0; i < 28; i++)
    {
        int nib = (in[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        int s = (int16_t)(nib << 12) >> shift;

        s += (hist[0] * coef[filter][0] + hist[1] * coef[filter][1]) >> 6;
        s = s < -32768 ? -32768 : (s > 32767 ? 32767 : s);
        hist[1] = hist[0];
        hist[0] = s;
        out[i] = (int16_t)s;
    }
}

/* Decodes the complete interleave blocks gathered so far. */
static void audio_decode(void)
{
    int block = mv.au_interleave * mv.au_channels;
    int used = 0;

    if (mv.au_interleave <= 0 || mv.au_channels < 1 || mv.au_channels > 2)
    {
        mv.au_len = 0;
        return;
    }

    while (mv.au_len - used >= block)
    {
        const unsigned char* b = mv.au_data + used;
        int frames;
        int16_t* lr;
        int c, i;

        if (mv.au_codec == 0x10)
        {
            frames = (mv.au_interleave / 16) * 28;
        }
        else
        {
            frames = mv.au_interleave / 2;
        }

        lr = malloc(frames * 4);
        if (lr == NULL)
        {
            break;
        }

        for (c = 0; c < 2; c++)
        {
            const unsigned char* src = b + (mv.au_channels == 2 ? c : 0) * mv.au_interleave;

            if (mv.au_codec == 0x10)
            {
                int16_t s[28];
                int f, k;

                for (f = 0; f < mv.au_interleave / 16; f++)
                {
                    adpcm_frame(src + f * 16, mv.adpcm_hist[c], s);
                    for (k = 0; k < 28; k++)
                    {
                        lr[(f * 28 + k) * 2 + c] = s[k];
                    }
                }
            }
            else
            {
                for (i = 0; i < frames; i++)
                {
                    lr[i * 2 + c] = (mv.au_codec == 0x02) ? (int16_t)((src[i * 2] << 8) | src[i * 2 + 1])
                                                          : (int16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
                }
            }
        }

        audio_out(lr, frames);
        free(lr);
        used += block;
    }

    memmove(mv.au_data, mv.au_data + used, mv.au_len - used);
    mv.au_len -= used;
}

static unsigned int le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* Payload of a private stream 1 packet: 4 bytes of sub-stream header, then
 * the "SShd"/"SSbd" header once, then the sound data. */
static void audio_payload(const unsigned char* p, int len)
{
    if (len <= 4 || !pc_audio_open)
    {
        return;
    }
    p += 4;
    len -= 4;

    while (mv.au_hdr_len < 40 && len > 0)
    {
        mv.au_hdr[mv.au_hdr_len++] = *p++;
        len--;

        if (mv.au_hdr_len == 40)
        {
            if (memcmp(mv.au_hdr, "SShd", 4) == 0)
            {
                mv.au_codec = le32(mv.au_hdr + 8);
                mv.au_rate = le32(mv.au_hdr + 12);
                mv.au_channels = le32(mv.au_hdr + 16);
                mv.au_interleave = le32(mv.au_hdr + 20);
                printf("movie: sound codec %d, %d Hz, %d channels, interleave %d\n", mv.au_codec,
                       mv.au_rate, mv.au_channels, mv.au_interleave);
            }
            else
            {
                printf("movie: unknown sound header\n");
                mv.au_interleave = 0;
            }
        }
    }

    if (len <= 0 || mv.au_interleave <= 0 || mv.au_rate <= 0)
    {
        return;
    }

    if (mv.au_len + len > mv.au_cap)
    {
        int cap = (mv.au_len + len) * 2;
        unsigned char* d = realloc(mv.au_data, cap);

        if (d == NULL)
        {
            return;
        }
        mv.au_data = d;
        mv.au_cap = cap;
    }
    memcpy(mv.au_data + mv.au_len, p, len);
    mv.au_len += len;
    audio_decode();
}

/* ---- Demultiplexing (MPEG program stream) ---------------------------------- */

/* Reads the next packet; returns 0 at the end of the file. */
static int demux_packet(void)
{
    const unsigned char* b;
    unsigned int code;
    unsigned int len;

    for (;;)
    {
        if (!need(4))
        {
            return 0;
        }
        b = mv.buf + mv.buf_pos;
        if (b[0] == 0 && b[1] == 0 && b[2] == 1)
        {
            break;
        }
        mv.buf_pos++; /* resynchronise */
    }

    code = b[3];

    if (code == 0xB9) /* end of the program stream */
    {
        mv.buf_pos += 4;
        return 0;
    }

    if (code == 0xBA) /* pack header */
    {
        if (!need(14))
        {
            return 0;
        }
        b = mv.buf + mv.buf_pos;
        if ((b[4] & 0xC0) == 0x40)
        {
            mv.buf_pos += 14 + (b[13] & 7);
        }
        else
        {
            mv.buf_pos += 12;
        }
        return 1;
    }

    if (code < 0xBB)
    {
        mv.buf_pos += 4;
        return 1;
    }

    if (!need(6))
    {
        return 0;
    }
    len = (mv.buf[mv.buf_pos + 4] << 8) | mv.buf[mv.buf_pos + 5];
    if (!need(6 + len))
    {
        return 0;
    }
    b = mv.buf + mv.buf_pos;

    if ((code >= 0xE0 && code <= 0xEF) || code == 0xBD)
    {
        unsigned int hdr;

        if ((b[6] & 0xC0) == 0x80) /* MPEG-2 PES header */
        {
            hdr = 9 + b[8];
        }
        else /* MPEG-1 */
        {
            hdr = 6;
            while (hdr < 6 + len && b[hdr] == 0xFF)
            {
                hdr++;
            }
            if (hdr < 6 + len && (b[hdr] & 0xC0) == 0x40)
            {
                hdr += 2;
            }
            if (hdr < 6 + len)
            {
                if ((b[hdr] & 0xF0) == 0x20)
                {
                    hdr += 5;
                }
                else if ((b[hdr] & 0xF0) == 0x30)
                {
                    hdr += 10;
                }
                else
                {
                    hdr += 1;
                }
            }
        }

        if (hdr < 6 + len)
        {
            if (code == 0xBD)
            {
                audio_payload(b + hdr, 6 + len - hdr);
            }
            else
            {
                pc_video_feed(mv.video, b + hdr, 6 + len - hdr);
            }
        }
    }

    mv.buf_pos += 6 + len;
    return 1;
}

/* Demultiplexes until a decoded frame is waiting; returns 0 at the end. */
static int next_frame_ready(void)
{
    while (pc_video_ready(mv.video) == 0)
    {
        if (mv.eof)
        {
            return 0;
        }
        if (!demux_packet())
        {
            mv.eof = 1;
            pc_video_feed(mv.video, NULL, 0);
        }
    }
    return 1;
}

/* ---- Replacement movies --------------------------------------------------------- */

/* Opens movies/<name>.<ext> for the movie in infile, if there is one. */
static void hd_open(void)
{
    static const char* const exts[] = { "mp4", "mkv", "webm", "mov", "avi" };
    const char* dir = getenv("CVX_MOVIES");
    char base[16], path[512];
    int i;

    /* "MV_000.PSS;1" -> "MV_000" */
    for (i = 0; i < 15 && infile.fp.name[i] != 0 && infile.fp.name[i] != '.' && infile.fp.name[i] != ';'; i++)
    {
        base[i] = infile.fp.name[i];
    }
    base[i] = 0;
    if (i == 0)
    {
        return;
    }
    if (dir == NULL || dir[0] == 0)
    {
        dir = "movies";
    }

    for (i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])); i++)
    {
        FILE* f;

        snprintf(path, sizeof(path), "%s/%s.%s", dir, base, exts[i]);
        f = fopen(path, "rb");
        if (f == NULL)
        {
            continue;
        }
        fclose(f);
        mv.hd = pc_hdmovie_open(path);
        if (mv.hd != NULL)
        {
            return;
        }
    }
}

/* Shows the replacement's picture for the movie's current time. */
static void hd_show(void)
{
    const uint32_t* rgba;
    int w, h, changed;

    if (mv.hd == NULL)
    {
        return;
    }
    rgba = pc_hdmovie_frame(mv.hd, (double)mv.clock_ns / 1e9, &w, &h, &changed);
    if (rgba == NULL)
    {
        return;
    }
    pc_overlay.rgba = rgba;
    pc_overlay.w = w;
    pc_overlay.h = h;
    /* Where vbrank_draw() puts the original picture. */
    pc_overlay.x = 0;
    pc_overlay.y = mdSize.sDispY;
    pc_overlay.cw = mdSize.sWidth != 0 ? mdSize.sWidth : 640;
    pc_overlay.ch = mdSize.sHeight != 0 ? mdSize.sHeight : 448;
    if (changed)
    {
        pc_overlay.serial++;
    }
}

/* ---- The game's interface ---------------------------------------------------- */

void initAll()
{
    termAll();

    memset(&rmi, 0, sizeof(rmi));
    memset(&voBuf, 0, sizeof(voBuf));

    rmi.iMovieState = 1;
    rmi.iMovieFrame = Ps2_vcount;

    mv.volume = 0x8000;
    mv.video = pc_video_open();
    if (mv.video == NULL)
    {
        printf("movie: no video decoder in this build, skipped\n");
        rmi.iMovieState = 3;
        return;
    }

    mv.lsn = infile.fp.lsn;
    mv.size = infile.size;
    mv.fps = 30000.0 / 1001.0;
    mv.open = 1;
    hd_open();

    pc_audio_clear();
}

void termAll()
{
    pc_overlay.rgba = NULL;
    if (mv.hd != NULL)
    {
        pc_hdmovie_close(mv.hd);
    }
    if (mv.open)
    {
        pc_video_close(mv.video);
        free(mv.au_data);
        pc_audio_clear();
    }
    memset(&mv, 0, sizeof(mv));
}

void readMpeg()
{
    int64_t now;
    int target;

    if (rmi.iMovieState == 3)
    {
        return;
    }

    if (!mv.open)
    {
        rmi.iMovieState = 3;
        return;
    }

    /* Skip: a button pressed after all of them were released. */
    if ((pc_pad_buttons & SKIP_BUTTONS) == SKIP_BUTTONS)
    {
        mv.skip_armed = 1;
    }
    else if (mv.skip_armed)
    {
        rmi.iMovieState = 3;
        return;
    }

    now = pc_host_time_ns();

    if (rmi.iMovieState == 1)
    {
        /* Ready to play once the first picture is decoded. */
        if (!next_frame_ready())
        {
            rmi.iMovieState = 3;
            return;
        }
        mv.fps = pc_video_fps(mv.video);
        mv.frame = pc_video_take(mv.video, &mv.w, &mv.h);
        mv.uploaded = 0;
        mv.shown = 1;
        mv.clock_ns = 0;
        mv.last_ns = now;
        rmi.iMovieState = 2;
        movie_draw = 1;
        hd_show();
        return;
    }

    if (!(rmi.uiContFlag & 0x2)) /* not paused */
    {
        mv.clock_ns += now - mv.last_ns;
    }
    mv.last_ns = now;

    target = 1 + (int)((double)mv.clock_ns * mv.fps / 1e9);

    while (mv.shown < target)
    {
        if (!next_frame_ready())
        {
            rmi.iMovieState = 3;
            return;
        }
        mv.frame = pc_video_take(mv.video, &mv.w, &mv.h);
        mv.uploaded = 0;
        mv.shown++;
    }

    /* Keep the sound ahead while the next picture is not due yet. */
    while (!mv.eof && !pc_video_full(mv.video) && pc_audio_open && mv.au_rate > 0 &&
           pc_audio_queued() < PC_AUDIO_RATE / 4)
    {
        if (!demux_packet())
        {
            mv.eof = 1;
            pc_video_feed(mv.video, NULL, 0);
        }
    }

    hd_show();
    movie_draw = 1;
}

/* Uploads the current frame to GS memory. */
void setImageTag(unsigned int* tags, void* image)
{
    int step, w, bw, x, y;

    if (mv.frame == NULL || mv.uploaded)
    {
        return;
    }

    /* Too big for the free GS memory: keep every other column. */
    step = (mv.w * mv.h > IMAGE_MAX_PIXELS) ? 2 : 1;
    w = mv.w / step;
    bw = (w + 63) / 64;

    for (y = 0; y < mv.h; y++)
    {
        const uint32_t* row = mv.frame + y * mv.w;

        for (x = 0; x < w; x++)
        {
            gs_write_pixel(GS_PSMCT32, IMAGE_TBP, bw, x, y, row[x * step]);
        }
    }
    mv.uploaded = 1;
}

static int log2up(int v)
{
    int n = 0;

    while ((1 << n) < v)
    {
        n++;
    }
    return n;
}

/* Draws the frame as a sprite stretched over the movie area (context 2). */
void vbrank_draw()
{
    unsigned long* tag = (unsigned long*)draw_tags;
    int iw, ih, u, v, y, w, h;

    if (mv.frame == NULL)
    {
        return;
    }

    iw = (mv.w * mv.h > IMAGE_MAX_PIXELS) ? mv.w / 2 : mv.w;
    ih = mv.h;

    u = (iw - 1) * 16;
    v = (ih - 1) * 16;

    w = mdSize.sWidth != 0 ? mdSize.sWidth : 640;
    h = mdSize.sHeight != 0 ? mdSize.sHeight : 448;

    y = (mdSize.sDispY + 1824) * 16;
    w *= 16;
    h *= 16;

    D2_SyncTag();

    *tag++ = DMAend | 0x9;
    *tag++ = 0;

    *tag++ = SCE_GIF_SET_TAG(0, 1, 0, 0, 0, 1) | 0x8;
    *tag++ = 0x000000000000000EL;

    *tag++ = 0;
    *tag++ = SCE_GS_TEXFLUSH;

    *tag++ = SCE_GS_SET_TEX1_1(0, 0, 1, 1, 0, 0, 0);
    *tag++ = SCE_GS_TEX1_2;

    *tag++ = SCE_GS_SET_TEX0_2(IMAGE_TBP, (iw + 63) / 64, SCE_GS_PSMCT32, log2up(iw), log2up(ih), 0, SCE_GS_DECAL, 0,
                               SCE_GS_PSMCT32, 0, 0, 0);
    *tag++ = SCE_GS_TEX0_2;

    *tag++ = SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, 1, 0);
    *tag++ = SCE_GS_PRIM;

    *tag++ = SCE_GS_SET_UV(8, 8);
    *tag++ = SCE_GS_UV;

    *tag++ = SCE_GS_SET_XYZ2(27648, y, 0);
    *tag++ = SCE_GS_XYZ2;

    *tag++ = SCE_GS_SET_UV(u + 8, v + 8);
    *tag++ = SCE_GS_UV;

    *tag++ = SCE_GS_SET_XYZ2(w + 27648, y + h, 0);
    *tag++ = SCE_GS_XYZ2;

    SyncPath();

    loadImage(draw_tags);

    D2_SyncTag();
}

int sendToIOP(int dst, u_char* src, int size)
{
    return size;
}

/* Volume from ps2mwPlySetOutVol: 0 (silent) to 32767. */
void changeInputVolume(u_int val)
{
    mv.volume = (val >= 0x7FFF) ? 0x8000 : (int)val;
}
