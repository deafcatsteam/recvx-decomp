/*
 * FFmpeg-backed FMV player for RECVX .PSS files.
 *
 * .PSS on the USA disc is an MPEG-PS container with:
 *   - MPEG2 video (track 0xE0)
 *   - audio muxed as MPEG-1 Layer II or CRI ADX (track 0xC0+ / 0xBD)
 *
 * Phase 4a scope: open a file through the ISO reader, demux MPEG-PS with
 * libavformat, decode the first video frame to RGBA, expose it for the
 * GL backend to blit. No audio yet, no resync, no seeking.
 *
 * I/O: we back libavformat's AVIOContext with a callback pair that reads
 * raw sectors from recvx_iso_t. avformat_open_input never touches the
 * host filesystem — every read is routed through the disc image.
 */

#include "recvx_port.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include <stdlib.h>
#include <string.h>

#define ISO_SECTOR_SIZE 2048

struct recvx_fmv {
    recvx_iso_t*       iso;
    uint32_t           file_lba;
    uint32_t           file_size;   /* bytes */
    int64_t            file_cursor; /* bytes from file start */

    AVIOContext*       avio;
    unsigned char*     avio_buf;
    AVFormatContext*   fmt;
    AVCodecContext*    vdec;
    int                video_idx;
    struct SwsContext* sws;

    AVPacket*          pkt;
    AVFrame*           dec_frame;
    AVFrame*           rgba_frame;

    int                width, height;
    int                have_frame;
    int                eof;
};

/* --- I/O glue between libavformat and the ISO reader ------------------ */

static int iso_read_packet(void* opaque, uint8_t* buf, int buf_size) {
    recvx_fmv_t* f = (recvx_fmv_t*)opaque;
    if (f->file_cursor >= (int64_t)f->file_size) return AVERROR_EOF;
    int64_t remaining = (int64_t)f->file_size - f->file_cursor;
    if (buf_size > remaining) buf_size = (int)remaining;

    /* Align down to sector, read enough sectors, copy the window. */
    int64_t start_off = f->file_cursor;
    int64_t end_off   = start_off + buf_size;
    uint32_t first_s  = (uint32_t)(start_off / ISO_SECTOR_SIZE);
    uint32_t last_s   = (uint32_t)((end_off - 1) / ISO_SECTOR_SIZE);
    uint32_t count    = last_s - first_s + 1;

    uint8_t* scratch = (uint8_t*)malloc((size_t)count * ISO_SECTOR_SIZE);
    if (!scratch) return AVERROR(ENOMEM);
    if (recvx_iso_read_sectors(f->iso, f->file_lba + first_s, count, scratch) != 0) {
        free(scratch);
        return AVERROR(EIO);
    }
    int64_t skip = start_off - (int64_t)first_s * ISO_SECTOR_SIZE;
    memcpy(buf, scratch + skip, buf_size);
    free(scratch);

    f->file_cursor += buf_size;
    return buf_size;
}

static int64_t iso_seek(void* opaque, int64_t offset, int whence) {
    recvx_fmv_t* f = (recvx_fmv_t*)opaque;
    if (whence == AVSEEK_SIZE) return (int64_t)f->file_size;
    whence &= ~AVSEEK_FORCE;
    int64_t target;
    switch (whence) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = f->file_cursor + offset; break;
        case SEEK_END: target = (int64_t)f->file_size + offset; break;
        default: return -1;
    }
    if (target < 0) return -1;
    f->file_cursor = target;
    return target;
}

/* --- Lifecycle -------------------------------------------------------- */

recvx_fmv_t* recvx_fmv_open(const char* iso_path) {
    recvx_iso_t* iso = recvx_iso_global();
    if (!iso) {
        RX_LOG("fmv", "no global ISO — cannot open %s", iso_path);
        return NULL;
    }

    uint32_t lba = 0, sz = 0;
    if (recvx_iso_find(iso, iso_path, &lba, &sz) != 0) {
        RX_LOG("fmv", "%s not found in ISO", iso_path);
        return NULL;
    }

    recvx_fmv_t* f = (recvx_fmv_t*)calloc(1, sizeof(*f));
    f->iso       = iso;
    f->file_lba  = lba;
    f->file_size = sz;
    f->video_idx = -1;

    const int io_bufsz = 64 * 1024;
    f->avio_buf = (unsigned char*)av_malloc(io_bufsz);
    f->avio     = avio_alloc_context(f->avio_buf, io_bufsz, 0, f,
                                     iso_read_packet, NULL, iso_seek);
    if (!f->avio) { recvx_fmv_close(f); return NULL; }

    f->fmt = avformat_alloc_context();
    f->fmt->pb = f->avio;
    /* .PSS is MPEG-PS; force the demuxer so probing doesn't trip on the
     * zero-padded sectors libavformat sometimes sees through CD I/O. */
    const AVInputFormat* ifmt = av_find_input_format("mpeg");

    if (avformat_open_input(&f->fmt, NULL, ifmt, NULL) != 0) {
        RX_LOG("fmv", "avformat_open_input failed for %s", iso_path);
        recvx_fmv_close(f);
        return NULL;
    }
    if (avformat_find_stream_info(f->fmt, NULL) < 0) {
        RX_LOG("fmv", "no stream info in %s", iso_path);
        recvx_fmv_close(f);
        return NULL;
    }

    for (unsigned i = 0; i < f->fmt->nb_streams; ++i) {
        AVCodecParameters* par = f->fmt->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && f->video_idx < 0) {
            f->video_idx = (int)i;
        }
        RX_LOG("fmv", "  stream %u: type=%d codec=%s",
               i, par->codec_type, avcodec_get_name(par->codec_id));
    }
    if (f->video_idx < 0) { recvx_fmv_close(f); return NULL; }

    AVCodecParameters* vpar = f->fmt->streams[f->video_idx]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(vpar->codec_id);
    if (!dec) { recvx_fmv_close(f); return NULL; }
    f->vdec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(f->vdec, vpar);
    if (avcodec_open2(f->vdec, dec, NULL) < 0) {
        RX_LOG("fmv", "avcodec_open2 failed");
        recvx_fmv_close(f);
        return NULL;
    }

    f->width  = f->vdec->width;
    f->height = f->vdec->height;
    f->sws = sws_getContext(f->width, f->height, f->vdec->pix_fmt,
                            f->width, f->height, AV_PIX_FMT_RGBA,
                            SWS_BILINEAR, NULL, NULL, NULL);
    f->pkt        = av_packet_alloc();
    f->dec_frame  = av_frame_alloc();
    f->rgba_frame = av_frame_alloc();
    f->rgba_frame->format = AV_PIX_FMT_RGBA;
    f->rgba_frame->width  = f->width;
    f->rgba_frame->height = f->height;
    av_image_alloc(f->rgba_frame->data, f->rgba_frame->linesize,
                   f->width, f->height, AV_PIX_FMT_RGBA, 16);

    RX_LOG("fmv", "opened %s: %dx%d codec=%s",
           iso_path, f->width, f->height, avcodec_get_name(vpar->codec_id));
    return f;
}

void recvx_fmv_close(recvx_fmv_t* f) {
    if (!f) return;
    if (f->rgba_frame) {
        if (f->rgba_frame->data[0]) av_freep(&f->rgba_frame->data[0]);
        av_frame_free(&f->rgba_frame);
    }
    if (f->dec_frame) av_frame_free(&f->dec_frame);
    if (f->pkt)       av_packet_free(&f->pkt);
    if (f->sws)       sws_freeContext(f->sws);
    if (f->vdec)      avcodec_free_context(&f->vdec);
    if (f->fmt)       avformat_close_input(&f->fmt);
    if (f->avio) {
        av_freep(&f->avio->buffer);
        avio_context_free(&f->avio);
    }
    free(f);
}

const void* recvx_fmv_pixels(const recvx_fmv_t* f) {
    return (f && f->have_frame) ? f->rgba_frame->data[0] : NULL;
}
int recvx_fmv_width (const recvx_fmv_t* f) { return f ? f->width  : 0; }
int recvx_fmv_height(const recvx_fmv_t* f) { return f ? f->height : 0; }

bool recvx_fmv_advance(recvx_fmv_t* f) {
    if (!f || f->eof) return false;
    while (1) {
        int r = av_read_frame(f->fmt, f->pkt);
        if (r < 0) { f->eof = 1; return false; }
        if (f->pkt->stream_index != f->video_idx) {
            av_packet_unref(f->pkt);
            continue;
        }
        if (avcodec_send_packet(f->vdec, f->pkt) < 0) {
            av_packet_unref(f->pkt);
            continue;
        }
        av_packet_unref(f->pkt);
        int got = avcodec_receive_frame(f->vdec, f->dec_frame);
        if (got == 0) {
            sws_scale(f->sws,
                      (const uint8_t* const*)f->dec_frame->data,
                      f->dec_frame->linesize,
                      0, f->height,
                      f->rgba_frame->data, f->rgba_frame->linesize);
            f->have_frame = 1;
            return true;
        }
    }
}
