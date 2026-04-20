/*
 * CRI ADX streaming via FFmpeg.
 *
 * adv.c's sound layer calls PlayAdx(slot, part, file) to start a BGM /
 * title-voice track from one of the mounted AFS partitions (PatId[part]).
 * sdfunc.c isn't compiled yet, so the real SCE voice mixer is absent —
 * we fake it here by reading the whole AFS entry, piping it through a
 * libavformat memory-AVIOContext, decoding with the built-in ADX codec
 * (AV_CODEC_ID_ADPCM_ADX), and pushing S16 stereo PCM to the current
 * backend's audio_queue. SDL plays it out to the device mixer.
 *
 * Slot model: adv.c uses slot 0 for title BGM. We support up to
 * ADX_SLOTS concurrent, but SDL's queue is one stereo stream, so only
 * the most-recent slot is audible — newer PlayAdx stops prior slots.
 * Good enough for title/menu; multi-channel mixing is a later phase.
 *
 * Life cycle: PlayAdx decodes the ENTIRE file into an S16 buffer up
 * front, then we feed SDL in chunks during each game frame via
 * recvx_adx_pump(). Menu BGMs are <1 MB decoded. If a future stream is
 * huge, switch to pull-based per-frame decode.
 */

#include "recvx_port.h"

#ifdef RECVX_HAVE_FFMPEG

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include <stdlib.h>
#include <string.h>

/* 0=BGM, 1=voice, 2..15=SE voice pool fed by the MSA loader. */
#define ADX_SLOTS 16

/* AFS helpers provided by port/src/afs/afs_mount.c. Declared here to
 * avoid pulling afs headers. */
extern int RequestReadInsideFile(unsigned int pat, unsigned int id, void* dst);
extern int GetInsideFileSize    (unsigned int pat, unsigned int id);

/* PatId[] mirrors the decomp's int PatId[4] — adv.c passes one of those
 * values (typically PatId[3] or PatId[0]) as `part`. The port-side
 * partition index is the same small 0..7 pat index used by
 * RequestReadInsideFile, so we forward `part` directly. */

typedef struct {
    unsigned char* file_buf;   /* raw AFS entry bytes */
    int            file_bytes;

    AVIOContext*   avio;
    unsigned char* avio_buf;
    int64_t        cursor;     /* bytes into file_buf */

    AVFormatContext* fmt;
    AVCodecContext*  dec;
    SwrContext*      swr;
    AVFrame*         frame;
    AVPacket*        pkt;

    int            src_rate;
    int            out_rate;   /* rate we opened SDL with */

    /* Fully-decoded S16LE stereo PCM; consumed in chunks by pump(). */
    unsigned char* pcm;
    int            pcm_len;
    int            pcm_pos;

    int            eof;
    float          volume;     /* 0..1, applied on pump to preserve raw PCM */
    int            active;
    int            loop;       /* 1 = wrap pcm_pos to 0 on completion */
    int            pcm_external;/* 1 = pcm buffer is caller-owned, don't free */
} adx_slot;

static adx_slot g_slots[ADX_SLOTS];
static int      g_audio_opened; /* backend audio_init already called? */
static int      g_out_rate;     /* canonical output sample rate (first PlayAdx) */

/* Memory-AVIO callback. FFmpeg reads bytes out of our in-memory file. */
static int mem_read(void* opaque, uint8_t* buf, int buf_size) {
    adx_slot* s = (adx_slot*)opaque;
    if (s->cursor >= s->file_bytes) return AVERROR_EOF;
    int64_t remaining = (int64_t)s->file_bytes - s->cursor;
    if (buf_size > remaining) buf_size = (int)remaining;
    memcpy(buf, s->file_buf + s->cursor, (size_t)buf_size);
    s->cursor += buf_size;
    return buf_size;
}

static int64_t mem_seek(void* opaque, int64_t offset, int whence) {
    adx_slot* s = (adx_slot*)opaque;
    if (whence == AVSEEK_SIZE) return s->file_bytes;
    int64_t target;
    switch (whence) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = s->cursor + offset; break;
        case SEEK_END: target = (int64_t)s->file_bytes + offset; break;
        default: return -1;
    }
    if (target < 0 || target > s->file_bytes) return -1;
    s->cursor = target;
    return target;
}

static void slot_free(adx_slot* s) {
    if (s->frame)  av_frame_free(&s->frame);
    if (s->pkt)    av_packet_free(&s->pkt);
    if (s->swr)    swr_free(&s->swr);
    if (s->dec)    avcodec_free_context(&s->dec);
    /* avformat_close_input frees the AVFormatContext but leaves the
     * AVIOContext (our custom pb) untouched; we own it here. */
    if (s->fmt)    avformat_close_input(&s->fmt);
    if (s->avio) {
        /* FFmpeg may have reallocated the internal buffer — free via
         * the context's current buffer pointer, not the one we passed in. */
        if (s->avio->buffer) av_freep(&s->avio->buffer);
        avio_context_free(&s->avio);
    }
    if (s->pcm && !s->pcm_external) free(s->pcm);
    if (s->file_buf) free(s->file_buf);
    memset(s, 0, sizeof(*s));
    s->volume = 1.0f;
}

static int decode_all_pcm(adx_slot* s, int target_rate) {
    s->frame = av_frame_alloc();
    s->pkt   = av_packet_alloc();
    if (!s->frame || !s->pkt) return -1;

    /* Output always S16 stereo @ target_rate — so every slot's pcm buffer
     * is directly mix-compatible without per-slot SRC in the pump. */
    int out_ch  = 2;
    int out_fmt = AV_SAMPLE_FMT_S16;

    s->src_rate = s->dec->sample_rate;
    s->out_rate = target_rate;

    AVChannelLayout in_layout;
    if (s->dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&in_layout, s->dec->ch_layout.nb_channels);
    } else {
        av_channel_layout_copy(&in_layout, &s->dec->ch_layout);
    }
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, out_ch);

    int rc = swr_alloc_set_opts2(&s->swr,
        &out_layout, out_fmt, target_rate,
        &in_layout, s->dec->sample_fmt, s->dec->sample_rate,
        0, NULL);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if (rc < 0 || swr_init(s->swr) < 0) return -1;

    int pcm_cap = 1 << 16;
    s->pcm = (unsigned char*)malloc(pcm_cap);
    if (!s->pcm) return -1;
    s->pcm_len = 0;

    while (av_read_frame(s->fmt, s->pkt) >= 0) {
        if (s->pkt->stream_index != 0) { av_packet_unref(s->pkt); continue; }
        int ret = avcodec_send_packet(s->dec, s->pkt);
        av_packet_unref(s->pkt);
        if (ret < 0) continue;

        while ((ret = avcodec_receive_frame(s->dec, s->frame)) >= 0) {
            int max_out = swr_get_out_samples(s->swr, s->frame->nb_samples);
            int out_bytes = max_out * out_ch * 2; /* S16 */
            if (s->pcm_len + out_bytes > pcm_cap) {
                while (s->pcm_len + out_bytes > pcm_cap) pcm_cap <<= 1;
                unsigned char* np = (unsigned char*)realloc(s->pcm, pcm_cap);
                if (!np) return -1;
                s->pcm = np;
            }
            uint8_t* dst[1] = { s->pcm + s->pcm_len };
            int got = swr_convert(s->swr, dst, max_out,
                                  (const uint8_t**)s->frame->extended_data,
                                  s->frame->nb_samples);
            if (got > 0) s->pcm_len += got * out_ch * 2;
            av_frame_unref(s->frame);
        }
    }
    /* Flush */
    avcodec_send_packet(s->dec, NULL);
    while (avcodec_receive_frame(s->dec, s->frame) >= 0) {
        int max_out = swr_get_out_samples(s->swr, s->frame->nb_samples);
        int out_bytes = max_out * out_ch * 2;
        if (s->pcm_len + out_bytes > pcm_cap) {
            while (s->pcm_len + out_bytes > pcm_cap) pcm_cap <<= 1;
            unsigned char* np = (unsigned char*)realloc(s->pcm, pcm_cap);
            if (!np) return -1;
            s->pcm = np;
        }
        uint8_t* dst[1] = { s->pcm + s->pcm_len };
        int got = swr_convert(s->swr, dst, max_out,
                              (const uint8_t**)s->frame->extended_data,
                              s->frame->nb_samples);
        if (got > 0) s->pcm_len += got * out_ch * 2;
        av_frame_unref(s->frame);
    }
    return s->pcm_len > 0 ? 0 : -1;
}

int recvx_adx_play_ex(int slot, int part, int file, int loop) {
    if (slot < 0 || slot >= ADX_SLOTS) return -1;
    const recvx_backend* be = recvx_backend_current();
    if (!be || !be->audio_init || !be->audio_queue) {
        RX_LOG("adx", "no backend audio — skip play");
        return -1;
    }

    /* PlayAdx is allowed to retrigger the same slot — stop first. */
    recvx_adx_stop(slot);
    adx_slot* s = &g_slots[slot];

    int size = GetInsideFileSize((unsigned)part, (unsigned)file);
    if (size <= 0) {
        RX_LOG("adx", "file size 0 for part=%d id=%d", part, file);
        return -1;
    }
    s->file_buf = (unsigned char*)malloc((size_t)size);
    if (!s->file_buf) return -1;
    if (RequestReadInsideFile((unsigned)part, (unsigned)file, s->file_buf) != 0) {
        free(s->file_buf); s->file_buf = NULL;
        return -1;
    }
    s->file_bytes = size;
    s->cursor = 0;
    s->volume = 1.0f;
    s->loop   = loop ? 1 : 0;

    const int avio_cap = 4096;
    s->avio_buf = (unsigned char*)av_malloc(avio_cap);
    if (!s->avio_buf) { slot_free(s); return -1; }
    s->avio = avio_alloc_context(s->avio_buf, avio_cap, 0, s,
                                 mem_read, NULL, mem_seek);
    if (!s->avio) { slot_free(s); return -1; }

    s->fmt = avformat_alloc_context();
    if (!s->fmt) { slot_free(s); return -1; }
    s->fmt->pb = s->avio;

    /* ADX files lack a recognizable magic FFmpeg probes for in default
     * configurations — force the demuxer. */
    const AVInputFormat* ifmt = av_find_input_format("adx");
    if (!ifmt) ifmt = av_find_input_format("ADX");
    if (avformat_open_input(&s->fmt, NULL, ifmt, NULL) != 0) {
        RX_LOG("adx", "avformat_open_input failed");
        slot_free(s);
        return -1;
    }
    if (avformat_find_stream_info(s->fmt, NULL) < 0) {
        RX_LOG("adx", "find_stream_info failed");
        slot_free(s);
        return -1;
    }
    if (s->fmt->nb_streams < 1) { slot_free(s); return -1; }

    AVCodecParameters* par = s->fmt->streams[0]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        RX_LOG("adx", "no decoder for codec_id=%d", par->codec_id);
        slot_free(s);
        return -1;
    }
    s->dec = avcodec_alloc_context3(codec);
    if (!s->dec) { slot_free(s); return -1; }
    if (avcodec_parameters_to_context(s->dec, par) < 0 ||
        avcodec_open2(s->dec, codec, NULL) < 0) {
        RX_LOG("adx", "codec open failed");
        slot_free(s);
        return -1;
    }

    /* Lock the canonical output rate on the first-ever PlayAdx. Every
     * later slot resamples into g_out_rate so the mixer can sum without
     * per-slot SRC. */
    if (!g_audio_opened) {
        g_out_rate = s->dec->sample_rate;
        be->audio_init(g_out_rate);
        g_audio_opened = 1;
    }

    if (decode_all_pcm(s, g_out_rate) < 0) {
        RX_LOG("adx", "decode produced no PCM");
        slot_free(s);
        return -1;
    }

    s->active = 1;
    RX_LOG("adx", "play slot=%d part=%d file=%d bytes=%d src_rate=%d out=%d pcm=%d loop=%d",
           slot, part, file, size, s->src_rate, s->out_rate, s->pcm_len, s->loop);
    return 0;
}

int recvx_adx_play(int slot, int part, int file) {
    return recvx_adx_play_ex(slot, part, file, 0);
}

/* Called once per game frame. Software-mixes every active slot into a
 * shared accumulator and pushes a single chunk to the backend's audio
 * queue. All slots share g_out_rate so we can sum S16 samples directly.
 *
 * Per-slot: wraps pcm_pos to 0 on completion if s->loop, otherwise
 * deactivates the slot once its buffer is drained.
 */
void recvx_adx_pump(void) {
    if (!g_audio_opened) return;
    const recvx_backend* be = recvx_backend_current();
    if (!be || !be->audio_queue) return;

    /* ~1/15s per pump @ 60fps = ~4 frames buffered — smooth but low
     * latency for StopAdx responsiveness. */
    int chunk = g_out_rate * 2 * 2 / 15;       /* bytes, stereo S16 */
    chunk &= ~3;                                /* align to a stereo frame */
    int samples = chunk / 2;                    /* int16 count */

    int32_t* accum = (int32_t*)calloc((size_t)samples, sizeof(int32_t));
    if (!accum) return;

    int any_active = 0;
    for (int i = 0; i < ADX_SLOTS; ++i) {
        adx_slot* s = &g_slots[i];
        if (!s->active || !s->pcm || s->pcm_len <= 0) continue;

        float g = s->volume;
        int written = 0;
        while (written < chunk && s->active) {
            int avail = s->pcm_len - s->pcm_pos;
            if (avail <= 0) {
                if (s->loop) { s->pcm_pos = 0; continue; }
                s->active = 0;
                break;
            }
            int take = chunk - written;
            if (take > avail) take = avail;

            int16_t* src       = (int16_t*)(s->pcm + s->pcm_pos);
            int      off_samp  = written / 2;
            int      n_samp    = take / 2;
            for (int n = 0; n < n_samp; ++n) {
                accum[off_samp + n] += (int32_t)(src[n] * g);
            }
            s->pcm_pos += take;
            written    += take;
        }
        if (written > 0) any_active = 1;
    }

    if (any_active) {
        int16_t* out = (int16_t*)malloc((size_t)chunk);
        if (!out) { free(accum); return; }
        for (int n = 0; n < samples; ++n) {
            int32_t v = accum[n];
            if (v > 32767)  v = 32767;
            else if (v < -32768) v = -32768;
            out[n] = (int16_t)v;
        }
        be->audio_queue(out, chunk);
        free(out);
    }
    free(accum);
}

void recvx_adx_stop(int slot) {
    if (slot < 0 || slot >= ADX_SLOTS) return;
    adx_slot* s = &g_slots[slot];
    if (!s->active && !s->file_buf) return;
    slot_free(s);
}

void recvx_adx_stop_all(void) {
    for (int i = 0; i < ADX_SLOTS; ++i) recvx_adx_stop(i);
}

void recvx_adx_set_volume(int slot, float volume) {
    if (slot < 0 || slot >= ADX_SLOTS) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    g_slots[slot].volume = volume;
}

int recvx_adx_ensure_audio_open(int default_rate) {
    const recvx_backend* be = recvx_backend_current();
    if (!be || !be->audio_init || !be->audio_queue) return -1;
    if (!g_audio_opened) {
        if (default_rate <= 0) default_rate = 32000;
        g_out_rate = default_rate;
        be->audio_init(g_out_rate);
        g_audio_opened = 1;
        RX_LOG("adx", "audio device opened lazily at %d Hz", g_out_rate);
    }
    return g_out_rate;
}

/* Non-owning playback. The caller (msa_parser) keeps a persistent decoded
 * PCM pool — we just point the slot at it and drive it through the mixer. */
int recvx_adx_slot_play_pcm(int slot, const void* pcm, int byte_count, float volume) {
    if (slot < 0 || slot >= ADX_SLOTS) return -1;
    if (!pcm || byte_count <= 0) return -1;
    if (recvx_adx_ensure_audio_open(0) < 0) return -1;

    adx_slot* s = &g_slots[slot];
    slot_free(s);
    s->pcm          = (unsigned char*)pcm;
    s->pcm_len      = byte_count;
    s->pcm_pos      = 0;
    s->pcm_external = 1;
    s->loop         = 0;
    s->volume       = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    s->active       = 1;
    return 0;
}

int recvx_adx_alloc_free_slot(int first, int last) {
    if (first < 0) first = 0;
    if (last >= ADX_SLOTS) last = ADX_SLOTS - 1;
    for (int i = first; i <= last; ++i) {
        if (!g_slots[i].active) return i;
    }
    /* All busy — evict the oldest by position fraction (biggest pcm_pos/pcm_len). */
    int best = first;
    float best_frac = -1.0f;
    for (int i = first; i <= last; ++i) {
        adx_slot* s = &g_slots[i];
        float f = s->pcm_len > 0 ? (float)s->pcm_pos / (float)s->pcm_len : 1.0f;
        if (f > best_frac) { best_frac = f; best = i; }
    }
    return best;
}

#else /* FFmpeg unavailable — build no-op fallback so the port still links. */

int  recvx_adx_play      (int slot, int part, int file) {
    (void)slot;(void)part;(void)file;
    RX_LOG("adx", "play ignored — FFmpeg not present");
    return -1;
}
int  recvx_adx_play_ex   (int slot, int part, int file, int loop) {
    (void)slot;(void)part;(void)file;(void)loop;
    return -1;
}
void recvx_adx_stop      (int slot)  { (void)slot; }
void recvx_adx_stop_all  (void)      { }
void recvx_adx_set_volume(int slot, float volume) { (void)slot;(void)volume; }
void recvx_adx_pump      (void)      { }
int  recvx_adx_ensure_audio_open(int default_rate) { (void)default_rate; return -1; }
int  recvx_adx_slot_play_pcm(int slot, const void* pcm, int byte_count, float volume) {
    (void)slot;(void)pcm;(void)byte_count;(void)volume; return -1;
}
int  recvx_adx_alloc_free_slot(int first, int last) { (void)first;(void)last; return -1; }

#endif
