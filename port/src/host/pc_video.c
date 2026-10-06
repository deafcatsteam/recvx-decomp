/*
 * MPEG-1/2 video decoding for the game's movies, with FFmpeg's libavcodec.
 * The movie player (port/src/game/pc_movie.c) demultiplexes the files and
 * feeds the video stream here; frames come out as RGBA, converted like the
 * PS2's IPU does (ITU-R BT.601, video range).
 *
 * It also plays the replacement movies (pc_hdmovie_*): video files made by
 * the player, read with libavformat, decoded on several threads and turned
 * into RGBA by libswscale.
 *
 * Without FFmpeg (CVX_WITH_FFMPEG=OFF) there is no decoder and the movies are
 * skipped.
 */
#include "pc_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CVX_HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

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


/* ---- Replacement movies ------------------------------------------------- */

#define HD_MAX_W 3840
#define HD_MAX_H 2160

struct PcHdMovie {
    AVFormatContext *fmt;
    AVCodecContext *ctx;
    int stream;
    double tb, start; /* seconds per timestamp unit, first timestamp */
    AVPacket *pkt;
    AVFrame *cur, *next; /* the picture shown, the one decoded after it */
    int have_cur, have_next, eof;
    struct SwsContext *sws;
    uint32_t *rgba;
    int w, h, converted;
};

PcHdMovie *pc_hdmovie_open(const char *path)
{
    PcHdMovie *m = calloc(1, sizeof(*m));
    const AVCodec *codec = NULL;
    AVStream *st;

    if (m == NULL)
        return NULL;
    if (avformat_open_input(&m->fmt, path, NULL, NULL) < 0) {
        free(m);
        return NULL;
    }
    if (avformat_find_stream_info(m->fmt, NULL) < 0 ||
        (m->stream = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0 ||
        codec == NULL) {
        printf("movie: %s: no video this build can decode\n", path);
        pc_hdmovie_close(m);
        return NULL;
    }
    st = m->fmt->streams[m->stream];
    m->ctx = avcodec_alloc_context3(codec);
    m->pkt = av_packet_alloc();
    m->cur = av_frame_alloc();
    m->next = av_frame_alloc();
    if (m->ctx == NULL || m->pkt == NULL || m->cur == NULL || m->next == NULL ||
        avcodec_parameters_to_context(m->ctx, st->codecpar) < 0) {
        pc_hdmovie_close(m);
        return NULL;
    }
    m->ctx->thread_count = 0; /* as many as the processor has */
    m->ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(m->ctx, codec, NULL) < 0) {
        pc_hdmovie_close(m);
        return NULL;
    }
    m->tb = av_q2d(st->time_base);
    m->start = st->start_time != AV_NOPTS_VALUE ? st->start_time * m->tb : 0.0;
    printf("movie: replacement %s: %dx%d, %s\n", path, m->ctx->width, m->ctx->height, codec->name);
    return m;
}

void pc_hdmovie_close(PcHdMovie *m)
{
    if (m == NULL)
        return;
    av_frame_free(&m->cur);
    av_frame_free(&m->next);
    av_packet_free(&m->pkt);
    avcodec_free_context(&m->ctx);
    avformat_close_input(&m->fmt);
    sws_freeContext(m->sws);
    free(m->rgba);
    free(m);
}

/* Decodes the next picture into m->next. */
static int hd_decode(PcHdMovie *m)
{
    for (;;) {
        int r = avcodec_receive_frame(m->ctx, m->next);

        if (r == 0)
            return 1;
        if (r != AVERROR(EAGAIN) || m->eof)
            return 0;
        for (;;) {
            r = av_read_frame(m->fmt, m->pkt);
            if (r < 0) {
                m->eof = 1;
                avcodec_send_packet(m->ctx, NULL);
                break;
            }
            if (m->pkt->stream_index == m->stream) {
                avcodec_send_packet(m->ctx, m->pkt);
                av_packet_unref(m->pkt);
                break;
            }
            av_packet_unref(m->pkt);
        }
    }
}

static double hd_time(PcHdMovie *m, const AVFrame *f)
{
    int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;

    return ts != AV_NOPTS_VALUE ? ts * m->tb - m->start : 0.0;
}

const uint32_t *pc_hdmovie_frame(PcHdMovie *m, double t, int *w, int *h, int *changed)
{
    *changed = 0;
    for (;;) {
        if (!m->have_next) {
            if (!hd_decode(m))
                break;
            m->have_next = 1;
        }
        if (m->have_cur && hd_time(m, m->next) > t)
            break;
        /* The next picture is due: it becomes the current one. */
        av_frame_unref(m->cur);
        av_frame_move_ref(m->cur, m->next);
        m->have_cur = 1;
        m->have_next = 0;
        m->converted = 0;
    }
    if (!m->have_cur)
        return NULL;

    if (!m->converted) {
        int sw = m->cur->width, sh = m->cur->height;
        int dw = sw, dh = sh;
        uint8_t *dst[4];
        int stride[4];

        if (dw > HD_MAX_W || dh > HD_MAX_H) {
            double k = dw * HD_MAX_H > dh * HD_MAX_W ? (double)HD_MAX_W / dw : (double)HD_MAX_H / dh;

            dw = (int)(dw * k) & ~1;
            dh = (int)(dh * k) & ~1;
        }
        m->sws = sws_getCachedContext(m->sws, sw, sh, m->cur->format, dw, dh, AV_PIX_FMT_RGBA,
                                      SWS_BICUBIC, NULL, NULL, NULL);
        if (m->sws == NULL)
            return NULL;
        if (dw != m->w || dh != m->h) {
            free(m->rgba);
            m->rgba = malloc((size_t)dw * dh * 4);
            m->w = dw;
            m->h = dh;
            if (m->rgba == NULL)
                return NULL;
        }
        dst[0] = (uint8_t *)m->rgba;
        dst[1] = dst[2] = dst[3] = NULL;
        stride[0] = dw * 4;
        stride[1] = stride[2] = stride[3] = 0;
        sws_scale(m->sws, (const uint8_t *const *)m->cur->data, m->cur->linesize, 0, sh, dst, stride);
        m->converted = 1;
        *changed = 1;
    }
    *w = m->w;
    *h = m->h;
    return m->rgba;
}

#else

PcVideo *pc_video_open(void) { return NULL; }
void pc_video_close(PcVideo *v) { (void)v; }
int pc_video_feed(PcVideo *v, const uint8_t *data, int size) { (void)v; (void)data; (void)size; return 0; }
int pc_video_ready(PcVideo *v) { (void)v; return 0; }
int pc_video_full(PcVideo *v) { (void)v; return 1; }
const uint32_t *pc_video_take(PcVideo *v, int *w, int *h) { (void)v; (void)w; (void)h; return NULL; }
double pc_video_fps(PcVideo *v) { (void)v; return 30000.0 / 1001.0; }
PcHdMovie *pc_hdmovie_open(const char *path) { (void)path; return NULL; }
void pc_hdmovie_close(PcHdMovie *m) { (void)m; }
const uint32_t *pc_hdmovie_frame(PcHdMovie *m, double t, int *w, int *h, int *changed)
{
    (void)m; (void)t; (void)w; (void)h;
    *changed = 0;
    return NULL;
}

#endif
