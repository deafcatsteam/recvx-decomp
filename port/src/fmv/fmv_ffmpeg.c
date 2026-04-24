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
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>

#include <stdlib.h>
#include <string.h>

#define ISO_SECTOR_SIZE 2048

struct recvx_fmv {
    /* I/O source — exactly one of these is non-NULL.
     *   iso != NULL: read via iso_read_sectors(iso, file_lba + sector, ...)
     *   fp  != NULL: read via fread on a loose file (gamedata/MOVIE/MV_*.PSS)
     * file_cursor is bytes from the start of the logical file (not sector).
     * file_size is the total payload size in bytes. */
    recvx_iso_t*       iso;
    FILE*              fp;          /* NULL unless opened via recvx_fmv_open_loose */
    uint32_t           file_lba;
    uint32_t           file_size;   /* bytes */
    int64_t            file_cursor; /* bytes from file start */

    AVIOContext*       avio;
    unsigned char*     avio_buf;
    AVFormatContext*   fmt;
    AVCodecContext*    vdec;
    int                video_idx;
    struct SwsContext* sws;

    AVCodecContext*    adec;
    int                audio_idx;
    struct SwrContext* swr;
    int                audio_rate;        /* output PCM rate (from source) */
    uint8_t*           pcm_buf;           /* interleaved S16 stereo scratch */
    int                pcm_cap;           /* bytes */

    AVFrame*           adec_frame;

    recvx_fmv_audio_sink audio_sink;
    void*                audio_sink_op;

    /* Parallel MPEG-PS audio walker — extracts Sofdec-framed 16-bit LE PCM
     * from 0xBD private_stream_1 PES packets that FFmpeg silently drops.
     * Runs with its own file cursor independent of avformat's reader. */
    int64_t            aud_cursor;
    uint8_t*           aud_buf;
    int                aud_cap;
    int                aud_used;
    int                aud_done;
    int                aud_rate;
    int                aud_ch;
    int                aud_header_seen;
    int64_t            aud_bytes_emitted; /* running total of PCM bytes sent */
    int                aud_diag_done;     /* one-shot rate-check log */
    int                aud_pkt_diag_left; /* remaining per-packet dumps */

    /* Sofdec LPCM is stored PLANAR in blocks: N samples of L followed by
     * N samples of R, repeating. SDL wants interleaved LRLR, so we
     * accumulate whole L+R block-pairs and de-interleave before emitting.
     * `aud_block_bytes` is (samples_per_channel × 2 bytes × 2 channels). */
    int                aud_block_bytes;
    uint8_t*           aud_inter_buf;   /* scratch for one L+R block-pair */
    int                aud_inter_used;  /* bytes accumulated in inter_buf */
    int                aud_ssbd_seen;   /* SSbd chunk header consumed yet */

    AVPacket*          pkt;
    AVFrame*           dec_frame;
    AVFrame*           rgba_frame;

    int                width, height;
    int                have_frame;
    int                eof;
    double             cur_pts_s; /* PTS of last decoded video frame, seconds */
};

/* --- I/O glue between libavformat and the underlying source ------------
 *
 * Dual-source: reads from the global ISO if f->iso is set, else from the
 * loose FILE* f->fp. The audio walker (pump_pss_audio) also calls
 * iso_read_packet directly with a temporarily-swapped file_cursor, so the
 * dispatcher has to live in this one helper instead of being split into
 * two read callbacks. */

static int iso_read_packet(void* opaque, uint8_t* buf, int buf_size) {
    recvx_fmv_t* f = (recvx_fmv_t*)opaque;
    if (f->file_cursor >= (int64_t)f->file_size) return AVERROR_EOF;
    int64_t remaining = (int64_t)f->file_size - f->file_cursor;
    if (buf_size > remaining) buf_size = (int)remaining;

    if (f->fp) {
        /* Loose-file path: simple pread-equivalent via fseek+fread. */
        if (fseek(f->fp, (long)f->file_cursor, SEEK_SET) != 0) {
            RX_LOG("fmv", "fseek failed at offset %lld", (long long)f->file_cursor);
            return AVERROR(EIO);
        }
        size_t got = fread(buf, 1, (size_t)buf_size, f->fp);
        if (got == 0) {
            if (feof(f->fp)) {
                RX_LOG("fmv", "fread EOF at cursor %lld (want %d bytes)", (long long)f->file_cursor, buf_size);
            } else {
                RX_LOG("fmv", "fread error at cursor %lld (want %d bytes)", (long long)f->file_cursor, buf_size);
            }
            return AVERROR_EOF;
        }
        f->file_cursor += (int64_t)got;
        return (int)got;
    }

    /* ISO path: align down to sector, read enough sectors, copy the window. */
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

    /* Allocate the parallel-audio walker buffer. ~512 KB holds roughly
     * 2-3 seconds of 48 kHz stereo S16 audio plus the MPEG-PS framing
     * overhead, which is plenty of headroom for SDL_QueueAudio pacing. */
    f->aud_cap = 512 * 1024;
    f->aud_buf = (uint8_t*)malloc(f->aud_cap);

    f->fmt = avformat_alloc_context();
    f->fmt->pb = f->avio;
    /* Bump the probe window so the mpeg-ps demuxer sees enough bytes to
     * identify the private_stream_1 substream (PS2 PSS tends to place
     * MP2/ADX audio there, and the default 5s window isn't enough). */
    f->fmt->probesize            = 20 * 1024 * 1024; /* 20 MB */
    f->fmt->max_analyze_duration = 30 * AV_TIME_BASE;
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
    av_dump_format(f->fmt, 0, iso_path, 0);

    f->audio_idx = -1;
    RX_LOG("fmv", "nb_streams=%u", f->fmt->nb_streams);
    for (unsigned i = 0; i < f->fmt->nb_streams; ++i) {
        AVStream* st = f->fmt->streams[i];
        AVCodecParameters* par = st->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && f->video_idx < 0) {
            f->video_idx = (int)i;
        }
        if (par->codec_type == AVMEDIA_TYPE_AUDIO && f->audio_idx < 0) {
            f->audio_idx = (int)i;
        }
        RX_LOG("fmv", "  stream %u: type=%d codec=%s id=0x%x",
               i, par->codec_type, avcodec_get_name(par->codec_id), st->id);
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
    if (!f->sws) {
        RX_LOG("fmv", "sws_getContext failed for %dx%d %s→RGBA",
               f->width, f->height, av_get_pix_fmt_name(f->vdec->pix_fmt));
        recvx_fmv_close(f);
        return NULL;
    }
    f->pkt        = av_packet_alloc();
    f->dec_frame  = av_frame_alloc();
    f->rgba_frame = av_frame_alloc();
    f->rgba_frame->format = AV_PIX_FMT_RGBA;
    f->rgba_frame->width  = f->width;
    f->rgba_frame->height = f->height;
    av_image_alloc(f->rgba_frame->data, f->rgba_frame->linesize,
                   f->width, f->height, AV_PIX_FMT_RGBA, 16);

    /* Audio — optional. Skip silently if open/init fails so the video
     * still plays muted rather than breaking the whole FMV path. */
    if (f->audio_idx >= 0) {
        AVCodecParameters* apar = f->fmt->streams[f->audio_idx]->codecpar;
        const AVCodec* adec = avcodec_find_decoder(apar->codec_id);
        if (adec) {
            f->adec = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(f->adec, apar);
            if (avcodec_open2(f->adec, adec, NULL) == 0) {
                f->audio_rate = f->adec->sample_rate;
                AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout in_layout  = f->adec->ch_layout;
                swr_alloc_set_opts2(&f->swr,
                    &out_layout, AV_SAMPLE_FMT_S16, f->audio_rate,
                    &in_layout,  f->adec->sample_fmt, f->adec->sample_rate,
                    0, NULL);
                if (swr_init(f->swr) < 0) {
                    RX_LOG("fmv", "swr_init failed");
                    swr_free(&f->swr);
                    avcodec_free_context(&f->adec);
                } else {
                    f->adec_frame = av_frame_alloc();
                    RX_LOG("fmv", "audio: %d Hz %d ch codec=%s",
                           f->audio_rate, f->adec->ch_layout.nb_channels,
                           avcodec_get_name(apar->codec_id));
                }
            } else {
                avcodec_free_context(&f->adec);
            }
        }
    }

    RX_LOG("fmv", "opened %s: %dx%d codec=%s",
           iso_path, f->width, f->height, avcodec_get_name(vpar->codec_id));
    return f;
}

/* Open a loose .PSS file from the host filesystem (used for
 * gamedata/MOVIE/MV_NNN.PSS playback from the game task state machine).
 * Shares the full demux/decode/audio path with recvx_fmv_open; only the
 * I/O source changes. */
recvx_fmv_t* recvx_fmv_open_loose(const char* fs_path) {
    FILE* fp = fopen(fs_path, "rb");
    if (!fp) {
        RX_LOG("fmv", "cannot open loose %s", fs_path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return NULL; }
    RX_LOG("fmv", "loose file %s: %ld bytes", fs_path, sz);
    rewind(fp);

    recvx_fmv_t* f = (recvx_fmv_t*)calloc(1, sizeof(*f));
    f->fp        = fp;
    f->file_size = (uint32_t)sz;
    f->video_idx = -1;

    const int io_bufsz = 64 * 1024;
    f->avio_buf = (unsigned char*)av_malloc(io_bufsz);
    f->avio     = avio_alloc_context(f->avio_buf, io_bufsz, 0, f,
                                     iso_read_packet, NULL, iso_seek);
    if (!f->avio) { recvx_fmv_close(f); return NULL; }

    f->aud_cap = 512 * 1024;
    f->aud_buf = (uint8_t*)malloc(f->aud_cap);

    f->fmt = avformat_alloc_context();
    f->fmt->pb = f->avio;
    f->fmt->probesize            = 20 * 1024 * 1024;
    f->fmt->max_analyze_duration = 30 * AV_TIME_BASE;
    const AVInputFormat* ifmt = av_find_input_format("mpeg");

    if (avformat_open_input(&f->fmt, NULL, ifmt, NULL) != 0) {
        RX_LOG("fmv", "avformat_open_input failed for %s", fs_path);
        recvx_fmv_close(f);
        return NULL;
    }
    if (avformat_find_stream_info(f->fmt, NULL) < 0) {
        RX_LOG("fmv", "no stream info in %s", fs_path);
        recvx_fmv_close(f);
        return NULL;
    }
    av_dump_format(f->fmt, 0, fs_path, 0);

    f->audio_idx = -1;
    for (unsigned i = 0; i < f->fmt->nb_streams; ++i) {
        AVStream* st = f->fmt->streams[i];
        AVCodecParameters* par = st->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && f->video_idx < 0) f->video_idx = (int)i;
        if (par->codec_type == AVMEDIA_TYPE_AUDIO && f->audio_idx < 0) f->audio_idx = (int)i;
    }
    if (f->video_idx < 0) { recvx_fmv_close(f); return NULL; }

    AVCodecParameters* vpar = f->fmt->streams[f->video_idx]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(vpar->codec_id);
    if (!dec) { recvx_fmv_close(f); return NULL; }
    f->vdec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(f->vdec, vpar);
    if (avcodec_open2(f->vdec, dec, NULL) < 0) {
        recvx_fmv_close(f);
        return NULL;
    }

    f->width  = f->vdec->width;
    f->height = f->vdec->height;
    f->sws = sws_getContext(f->width, f->height, f->vdec->pix_fmt,
                            f->width, f->height, AV_PIX_FMT_RGBA,
                            SWS_BILINEAR, NULL, NULL, NULL);
    if (!f->sws) {
        RX_LOG("fmv", "sws_getContext failed for %dx%d %s→RGBA",
               f->width, f->height, av_get_pix_fmt_name(f->vdec->pix_fmt));
        recvx_fmv_close(f);
        return NULL;
    }
    f->pkt        = av_packet_alloc();
    f->dec_frame  = av_frame_alloc();
    f->rgba_frame = av_frame_alloc();
    f->rgba_frame->format = AV_PIX_FMT_RGBA;
    f->rgba_frame->width  = f->width;
    f->rgba_frame->height = f->height;
    av_image_alloc(f->rgba_frame->data, f->rgba_frame->linesize,
                   f->width, f->height, AV_PIX_FMT_RGBA, 16);

    if (f->audio_idx >= 0) {
        AVCodecParameters* apar = f->fmt->streams[f->audio_idx]->codecpar;
        const AVCodec* adec = avcodec_find_decoder(apar->codec_id);
        if (adec) {
            f->adec = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(f->adec, apar);
            if (avcodec_open2(f->adec, adec, NULL) == 0) {
                f->audio_rate = f->adec->sample_rate;
                AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout in_layout  = f->adec->ch_layout;
                swr_alloc_set_opts2(&f->swr,
                    &out_layout, AV_SAMPLE_FMT_S16, f->audio_rate,
                    &in_layout,  f->adec->sample_fmt, f->adec->sample_rate,
                    0, NULL);
                if (swr_init(f->swr) < 0) {
                    swr_free(&f->swr);
                    avcodec_free_context(&f->adec);
                } else {
                    f->adec_frame = av_frame_alloc();
                }
            } else {
                avcodec_free_context(&f->adec);
            }
        }
    }

    RX_LOG("fmv", "opened (loose) %s: %dx%d codec=%s",
           fs_path, f->width, f->height, avcodec_get_name(vpar->codec_id));
    return f;
}

void recvx_fmv_set_audio_sink(recvx_fmv_t* f, recvx_fmv_audio_sink sink, void* opaque) {
    if (!f) return;
    f->audio_sink    = sink;
    f->audio_sink_op = opaque;
}

void recvx_fmv_close(recvx_fmv_t* f) {
    if (!f) return;
    if (f->aud_inter_buf) { free(f->aud_inter_buf); f->aud_inter_buf = NULL; }
    if (f->aud_buf)   { free(f->aud_buf); f->aud_buf = NULL; }
    if (f->pcm_buf)   { free(f->pcm_buf); f->pcm_buf = NULL; }
    if (f->adec_frame) av_frame_free(&f->adec_frame);
    if (f->swr)        swr_free(&f->swr);
    if (f->adec)       avcodec_free_context(&f->adec);
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
    if (f->fp) { fclose(f->fp); f->fp = NULL; }
    free(f);
}

const void* recvx_fmv_pixels(const recvx_fmv_t* f) {
    return (f && f->have_frame) ? f->rgba_frame->data[0] : NULL;
}
int    recvx_fmv_width (const recvx_fmv_t* f) { return f ? f->width  : 0; }
int    recvx_fmv_height(const recvx_fmv_t* f) { return f ? f->height : 0; }
double recvx_fmv_pts_s (const recvx_fmv_t* f) { return f ? f->cur_pts_s : 0.0; }

/* Parallel audio walker — reads sectors with its own cursor, parses the
 * MPEG-PS structure, extracts 0xBD private_stream_1 PES payloads, strips
 * the 4-byte `FF A0 00 00` Sofdec per-packet prefix, parses the one-time
 * `SShd` header for sample rate + channels, and emits the rest as raw
 * S16 LE PCM through the caller's audio sink.
 *
 * Called from advance() so audio delivery is naturally paced by the
 * video loop; SDL_QueueAudio absorbs any temporary bursts. */
static void pump_pss_audio(recvx_fmv_t* f) {
    if (!f->audio_sink || !f->aud_buf || f->aud_done) return;

    /* Keep audio at most ~1s ahead of video so SDL_QueueAudio doesn't
     * accumulate the entire movie on the first few advance() calls. */
    if (f->aud_header_seen && f->aud_rate > 0) {
        double audio_time = (double)f->aud_bytes_emitted /
                            (double)(f->aud_rate * f->aud_ch * 2);
        if (audio_time > f->cur_pts_s + 1.0) return;
    }

    /* Refill the scan buffer from the audio cursor. Keeps our reads
     * independent of avformat's cursor because iso_read is stateless
     * when we restore f->file_cursor afterwards. */
    while (f->aud_used < f->aud_cap) {
        int64_t remaining = (int64_t)f->file_size - f->aud_cursor;
        if (remaining <= 0) { f->aud_done = 1; break; }
        int want = f->aud_cap - f->aud_used;
        if (want > remaining) want = (int)remaining;

        int64_t save_cursor = f->file_cursor;
        f->file_cursor = f->aud_cursor;
        int r = iso_read_packet(f, f->aud_buf + f->aud_used, want);
        f->aud_cursor  = f->file_cursor;
        f->file_cursor = save_cursor;
        if (r <= 0) { f->aud_done = 1; break; }
        f->aud_used += r;
        /* One refill per call is enough — more and we'd starve video. */
        break;
    }

    int i = 0;
    while (i + 16 <= f->aud_used) {
        if (!(f->aud_buf[i] == 0 && f->aud_buf[i+1] == 0 && f->aud_buf[i+2] == 1)) {
            i++; continue;
        }
        uint8_t sid = f->aud_buf[i+3];

        if (sid == 0xBA) {
            /* MPEG-2 pack header: fixed 14 bytes + stuffing_length in the
             * low 3 bits of byte 13. */
            if (i + 14 > f->aud_used) break;
            int stuffing = f->aud_buf[i+13] & 7;
            if (i + 14 + stuffing > f->aud_used) break;
            i += 14 + stuffing;
            continue;
        }
        if (sid == 0xBB || sid == 0xBE) {
            if (i + 6 > f->aud_used) break;
            int sh_len = (f->aud_buf[i+4] << 8) | f->aud_buf[i+5];
            if (i + 6 + sh_len > f->aud_used) break;
            i += 6 + sh_len;
            continue;
        }

        /* Generic PES packet. */
        if (i + 6 > f->aud_used) break;
        int pes_len = (f->aud_buf[i+4] << 8) | f->aud_buf[i+5];
        if (i + 6 + pes_len > f->aud_used) break;  /* incomplete — wait */

        if (sid == 0xBD) {
            int hdr_data_len = f->aud_buf[i+8];
            int payload_off  = i + 9 + hdr_data_len;
            int payload_end  = i + 6 + pes_len;
            int payload_size = payload_end - payload_off;
            uint8_t* p       = f->aud_buf + payload_off;

            /* Skip the per-packet Sofdec marker. */
            if (payload_size >= 4 && p[0] == 0xFF && p[1] == 0xA0) {
                p += 4; payload_size -= 4;
            }

            /* First 0xBD packet carries an `SShd` header with fmt info. */
            if (!f->aud_header_seen && payload_size >= 24 &&
                p[0] == 'S' && p[1] == 'S' && p[2] == 'h' && p[3] == 'd') {
                f->aud_rate = (int)(p[12] | (p[13] << 8) |
                                    (p[14] << 16) | (p[15] << 24));
                f->aud_ch   = (int)(p[16] | (p[17] << 8) |
                                    (p[18] << 16) | (p[19] << 24));
                /* Block size = samples-per-channel-per-block, read from
                 * SShd offset 20. Total samples-per-channel in the file
                 * (10,099,200) divides cleanly by 512 but not by 1024,
                 * proving 512 is correct despite earlier experiments
                 * preferring 1024 (those were polluted by the device-rate
                 * and byte-drop bugs that have since been fixed). */
                int block_samples = (int)(p[20] | (p[21] << 8) |
                                          (p[22] << 16) | (p[23] << 24));
                if (block_samples <= 0) block_samples = 512;
                f->aud_block_bytes = block_samples * 2 * 2;
                f->aud_inter_buf   = (uint8_t*)malloc(f->aud_block_bytes);
                f->aud_inter_used  = 0;
                f->aud_ssbd_seen   = 0;
                f->aud_header_seen = 1;
                RX_LOG("fmv",
                       "PSS audio: %d Hz %d ch, planar %d samples/ch/block (%d-byte block-pair)",
                       f->aud_rate, f->aud_ch, block_samples, f->aud_block_bytes);
                p += 24; payload_size -= 24;
            }

            /* First payload also carries an SSbd chunk header (+ optional
             * padding between SShd and SSbd). Skip bytes up to and
             * including the 8-byte SSbd header so audio samples start
             * clean. */
            if (f->aud_header_seen && !f->aud_ssbd_seen) {
                int j = 0;
                while (j + 4 <= payload_size) {
                    if (p[j]=='S' && p[j+1]=='S' && p[j+2]=='b' && p[j+3]=='d') {
                        j += 8;  /* skip magic + size */
                        p += j;
                        payload_size -= j;
                        f->aud_ssbd_seen = 1;
                        break;
                    }
                    j++;
                }
                if (!f->aud_ssbd_seen) {
                    /* SSbd not in this packet — consume nothing, try next */
                    payload_size = 0;
                }

            }

            /* Cross-packet planar de-interleave. Each block-pair is
             * block_bytes bytes (first half L-samples, second half
             * R-samples). Accumulate bytes from consecutive PES packets
             * until the buffer holds a full pair, de-interleave into
             * LRLR stereo, and emit. Every byte is preserved across
             * packet boundaries in aud_inter_buf. */
            while (f->aud_header_seen && f->aud_ssbd_seen &&
                   payload_size > 0 && f->aud_inter_buf) {
                int space = f->aud_block_bytes - f->aud_inter_used;
                int take  = payload_size < space ? payload_size : space;
                memcpy(f->aud_inter_buf + f->aud_inter_used, p, take);
                f->aud_inter_used += take;
                p                 += take;
                payload_size      -= take;

                if (f->aud_inter_used == f->aud_block_bytes) {
                    int samples_per_ch = f->aud_block_bytes / (2 * 2);
                    int16_t* Lsrc = (int16_t*)(f->aud_inter_buf);
                    int16_t* Rsrc = (int16_t*)(f->aud_inter_buf + samples_per_ch * 2);
                    int16_t* out  = (int16_t*)malloc(f->aud_block_bytes);
                    for (int k = 0; k < samples_per_ch; ++k) {
                        out[k*2]   = Lsrc[k];
                        out[k*2+1] = Rsrc[k];
                    }
                    f->audio_sink(f->audio_sink_op, f->aud_rate,
                                  out, f->aud_block_bytes);
                    free(out);
                    f->aud_bytes_emitted += f->aud_block_bytes;
                    f->aud_inter_used = 0;

                    if (!f->aud_diag_done && f->cur_pts_s >= 5.0) {
                        double implied = f->aud_bytes_emitted /
                                         (f->cur_pts_s + 1.0);
                        RX_LOG("fmv",
                               "audio rate check: %lld B emitted @ PTS %.2fs => %.0f B/s",
                               (long long)f->aud_bytes_emitted,
                               f->cur_pts_s, implied);
                        f->aud_diag_done = 1;
                    }
                }
            }
        }
        i += 6 + pes_len;
    }

    if (i > 0) {
        memmove(f->aud_buf, f->aud_buf + i, f->aud_used - i);
        f->aud_used -= i;
    } else if (f->aud_used == f->aud_cap) {
        /* Pathological: giant unparseable packet. Reset to recover. */
        RX_LOG("fmv", "audio walker stuck, flushing buffer");
        f->aud_used = 0;
    }
}

static void drain_audio(recvx_fmv_t* f) {
    if (!f->adec) return;
    while (1) {
        int got = avcodec_receive_frame(f->adec, f->adec_frame);
        if (got != 0) break;
        int in_samples = f->adec_frame->nb_samples;
        /* worst-case output samples if rate changes (we don't); + slack */
        int out_cap = swr_get_out_samples(f->swr, in_samples);
        if (out_cap < in_samples) out_cap = in_samples;
        int want_bytes = out_cap * 2 /*ch*/ * 2 /*s16*/;
        if (want_bytes > f->pcm_cap) {
            f->pcm_buf = (uint8_t*)realloc(f->pcm_buf, want_bytes);
            f->pcm_cap = want_bytes;
        }
        uint8_t* out[1] = { f->pcm_buf };
        int got_samples = swr_convert(f->swr, out, out_cap,
                                      (const uint8_t**)f->adec_frame->extended_data,
                                      in_samples);
        if (got_samples > 0 && f->audio_sink) {
            f->audio_sink(f->audio_sink_op, f->audio_rate,
                          f->pcm_buf, got_samples * 4);
        }
    }
}

bool recvx_fmv_advance(recvx_fmv_t* f) {
    if (!f || f->eof) return false;
    /* Pump the PSS audio walker first so SDL has data queued before the
     * video frame even lands. If avformat discovered a standard audio
     * stream (MP2/AC3/etc.), the walker is a no-op — aud_buf is still
     * allocated but `audio_sink` drives the choice of path. */
    if (f->audio_idx < 0) pump_pss_audio(f);
    while (1) {
        int r = av_read_frame(f->fmt, f->pkt);
        if (r < 0) {
            if (r != AVERROR_EOF) {
                RX_LOG("fmv", "av_read_frame error: %d (pts=%.2f)", r, f->cur_pts_s);
            }
            f->eof = 1;
            return false;
        }
        if (f->pkt->stream_index == f->audio_idx && f->adec) {
            if (avcodec_send_packet(f->adec, f->pkt) == 0) {
                drain_audio(f);
            }
            av_packet_unref(f->pkt);
            continue;
        }
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
            int64_t pts = f->dec_frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = f->dec_frame->pts;
            if (pts != AV_NOPTS_VALUE) {
                AVRational tb = f->fmt->streams[f->video_idx]->time_base;
                f->cur_pts_s = (double)pts * av_q2d(tb);
            }
            int sws_result = sws_scale(f->sws,
                      (const uint8_t* const*)f->dec_frame->data,
                      f->dec_frame->linesize,
                      0, f->height,
                      f->rgba_frame->data, f->rgba_frame->linesize);
            if (sws_result <= 0) {
                RX_LOG("fmv", "sws_scale failed: %d (fmt=%s)", sws_result,
                       av_get_pix_fmt_name(f->dec_frame->format));
                return true;
            }
            f->have_frame = 1;
            return true;
        }
    }
}
