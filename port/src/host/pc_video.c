/*
 * MPEG-1/2 video decoding for the game's movies, with FFmpeg's libavcodec.
 * The movie player (port/src/game/pc_movie.c) demultiplexes the files and
 * feeds the video stream here; frames come out as RGBA, converted like the
 * PS2's IPU does (ITU-R BT.601, video range).
 *
 * Without FFmpeg (CVX_WITH_FFMPEG=OFF) there is no decoder and the movies are
 * skipped.
 */
#include "pc_host.h"

#include <stdlib.h>
#include <string.h>

#ifdef CVX_HAVE_FFMPEG
#include <libavcodec/avcodec.h>

#define MAX_FRAMES 8

struct PcVideo {
    AVCodecContext *ctx;
    AVCodecParserContext *parser;
    AVPacket *pkt;
    AVFrame *frame;
    /* Decoded frames waiting to be taken, oldest first. */
    uint32_t *rgba[MAX_FRAMES];
    int count;
    int w, h;
    uint32_t *out; /* the frame last handed out */
};

PcVideo *pc_video_open(void)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    PcVideo *v;

    if (codec == NULL)
        return NULL;
    v = calloc(1, sizeof(*v));
    if (v == NULL)
        return NULL;
    v->ctx = avcodec_alloc_context3(codec);
    v->parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    v->pkt = av_packet_alloc();
    v->frame = av_frame_alloc();
    if (v->ctx == NULL || v->parser == NULL || v->pkt == NULL || v->frame == NULL ||
        avcodec_open2(v->ctx, codec, NULL) < 0) {
        pc_video_close(v);
        return NULL;
    }
    return v;
}

void pc_video_close(PcVideo *v)
{
    int i;

    if (v == NULL)
        return;
    for (i = 0; i < v->count; i++)
        free(v->rgba[i]);
    free(v->out);
    av_frame_free(&v->frame);
    av_packet_free(&v->pkt);
    if (v->parser != NULL)
        av_parser_close(v->parser);
    avcodec_free_context(&v->ctx);
    free(v);
}

static uint8_t clamp8(int x)
{
    return x < 0 ? 0 : (x > 255 ? 255 : x);
}

/* YCbCr 4:2:0 (video range) to RGBA, alpha 0x80 like the IPU's output. */
static uint32_t *to_rgba(const AVFrame *f)
{
    uint32_t *out = malloc((size_t)f->width * f->height * 4);
    int x, y;

    if (out == NULL)
        return NULL;
    for (y = 0; y < f->height; y++) {
        const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
        const uint8_t *pu = f->data[1] + (size_t)(y >> 1) * f->linesize[1];
        const uint8_t *pv = f->data[2] + (size_t)(y >> 1) * f->linesize[2];
        uint32_t *o = out + (size_t)y * f->width;

        for (x = 0; x < f->width; x++) {
            int c = (py[x] - 16) * 298;
            int d = pu[x >> 1] - 128;
            int e = pv[x >> 1] - 128;
            int r = (c + 409 * e + 128) >> 8;
            int g = (c - 100 * d - 208 * e + 128) >> 8;
            int b = (c + 516 * d + 128) >> 8;

            o[x] = clamp8(r) | (clamp8(g) << 8) | ((uint32_t)clamp8(b) << 16) | 0x80000000u;
        }
    }
    return out;
}

static void receive(PcVideo *v)
{
    while (avcodec_receive_frame(v->ctx, v->frame) == 0) {
        if (v->frame->format == AV_PIX_FMT_YUV420P && v->count < MAX_FRAMES) {
            uint32_t *rgba = to_rgba(v->frame);

            if (rgba != NULL) {
                v->rgba[v->count++] = rgba;
                v->w = v->frame->width;
                v->h = v->frame->height;
            }
        }
        av_frame_unref(v->frame);
    }
}

int pc_video_feed(PcVideo *v, const uint8_t *data, int size)
{
    int flush = size == 0;

    while (size > 0 || flush) {
        int used = av_parser_parse2(v->parser, v->ctx, &v->pkt->data, &v->pkt->size,
                                    data, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (used < 0)
            return v->count;
        data += used;
        size -= used;
        if (v->pkt->size > 0) {
            if (avcodec_send_packet(v->ctx, v->pkt) == 0)
                receive(v);
        }
        if (flush && v->pkt->size == 0) {
            avcodec_send_packet(v->ctx, NULL);
            receive(v);
            break;
        }
    }
    return v->count;
}

int pc_video_ready(PcVideo *v)
{
    return v->count;
}

int pc_video_full(PcVideo *v)
{
    return v->count >= MAX_FRAMES / 2;
}

const uint32_t *pc_video_take(PcVideo *v, int *w, int *h)
{
    if (v->count == 0)
        return NULL;
    free(v->out);
    v->out = v->rgba[0];
    memmove(&v->rgba[0], &v->rgba[1], (v->count - 1) * sizeof(v->rgba[0]));
    v->count--;
    *w = v->w;
    *h = v->h;
    return v->out;
}

double pc_video_fps(PcVideo *v)
{
    if (v->ctx->framerate.num > 0 && v->ctx->framerate.den > 0)
        return (double)v->ctx->framerate.num / v->ctx->framerate.den;
    return 30000.0 / 1001.0;
}

#else

PcVideo *pc_video_open(void) { return NULL; }
void pc_video_close(PcVideo *v) { (void)v; }
int pc_video_feed(PcVideo *v, const uint8_t *data, int size) { (void)v; (void)data; (void)size; return 0; }
int pc_video_ready(PcVideo *v) { (void)v; return 0; }
int pc_video_full(PcVideo *v) { (void)v; return 1; }
const uint32_t *pc_video_take(PcVideo *v, int *w, int *h) { (void)v; (void)w; (void)h; return NULL; }
double pc_video_fps(PcVideo *v) { (void)v; return 30000.0 / 1001.0; }

#endif
