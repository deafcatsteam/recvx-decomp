/*
 * SCEI MSA (Multi-Stream Audio / MANATEE) sound-bank loader for system SE.
 *
 * On PS2, sdfunc.c's InitSoundDriver("MANATEE.DRV","COMMON.MLT") hands the
 * whole .MLT off to CRI MANATEE, which parses the container, walks the
 * sample bank, and exposes note/bank addressing for ExPlaySe. COMMON.MLT
 * holds the game's system SE bank (menu cursor beeps, title confirms,
 * inventory clicks).
 *
 * File geometry we care about (observed on RECVX COMMON.MLT, 58848 B):
 *
 *   0x0000  MLT master header (32 B)
 *           u32 header_size   = 0x20
 *           u32 block_offs[0] = 0x0630  (end of metadata tables)
 *           u32 block_offs[1] = 0x0660  (start of VAG body pool)
 *           u32 block_offs[2] = 0xde70  (end   of VAG body pool)
 *           u32 block_offs[3] = 0xe4e0  (start of secondary MLT — MIDI)
 *
 *   0x0020..0x062F  SCEI section tables: Vers, Head, Vagi, Smpl, Sset, Prog
 *     Each section has the common header:
 *         char magic[8]  = "SCEI" + 4-char tag (both stored LE-u32, so the
 *                          raw bytes on disk read "IECS<tag-reversed>")
 *         u32  size      (total section bytes including this header)
 *         u32  count     (number of entries, for table sections)
 *         u32  offsets[] (count+1 u32s — byte offsets into this section
 *                          for each entry; the +1th is an end sentinel)
 *         u8   entries[] (entry blob, packed back-to-back)
 *
 *   0x0660..0xDE6F  VAG body pool: raw PSX-ADPCM frames. Each Vagi entry
 *                   names an offset into this pool; sample_len is inferred
 *                   from the next entry's offset (or the pool end for the
 *                   last entry).
 *
 * Decode path per sample:
 *   1. Slice VAG bytes from the pool using Vagi body_offset + size.
 *   2. Inline PSX-ADPCM decode to S16 mono (14 samples per 16-byte frame,
 *      two per byte via nibble pairs, filtered through the 5-entry
 *      predictor LUT with a per-sample shift).
 *   3. Resolve per-sample source rate from the Smpl section (bytes [0x14]
 *      of each Smpl entry, LE u16 Hz). Falls back to MSA_SAMPLE_RATE
 *      default if Smpl is missing or the value is out of range.
 *   4. Linear-interpolation resample from that rate to g_out_rate,
 *      duplicated L/R into interleaved S16 stereo so the mixer's
 *      accumulator can sum without per-slot SRC.
 *
 * Playback path:
 *   recvx_msa_play_se(seNo, vol) → alloc voice slot in [2..15] from the
 *   ADX mixer, point it at the cached PCM buffer via
 *   recvx_adx_slot_play_pcm. No ownership transfer: the mixer just reads
 *   until pcm_len bytes have been consumed.
 *
 * SeNo routing:
 *   COMMON.MLT's Sset/Prog/Smpl tables all have one entry per Vagi with
 *   index-preserving references (Sset[N] → Prog[N] → Smpl[N] → Vagi[N]),
 *   so a flat `idx = SeNo % sample_count` mapping matches the authentic
 *   chain for this bank. Richer banks (MULTSPQ.AFS room SE) will need
 *   the full Sset→Prog→Smpl resolution.
 *
 * Limitations (deliberate — Stage 2):
 *   - No ADSR envelope / pitch / pan (PS2 SPU fields not applied).
 *   - Only COMMON.MLT (menu SE); MULTSPQ.AFS room banks come later.
 */

#include "recvx_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MSA_MAX_SAMPLES 32
#define MSA_SE_SLOT_FIRST 2
#define MSA_SE_SLOT_LAST  15

/* Source rate of the PSX-ADPCM VAG samples baked into COMMON.MLT. PS2
 * SPU native sample rate at pitch 0x1000 is 48000, but SE patches are
 * typically stored at a fraction of that; 22050 is the common RE-series
 * choice and what these beeps sound right at (tune by ear later). */
#define MSA_SAMPLE_RATE      22050
/* Rate we'd like the backend audio device opened at if nothing else has
 * opened it yet. Matches the ADX title voice native rate so subsequent
 * PlayAdx calls don't incur a resample when we init MSA first. */
#define MSA_DEVICE_RATE_HINT 48000

typedef struct msa_sample {
    int16_t* pcm;       /* interleaved S16 stereo @ mixer rate */
    int      bytes;     /* byte count of pcm[] */
} msa_sample;

static struct {
    int        initialized;
    int        mixer_rate;
    int        force_rate;   /* CLI override; 0 = use Smpl-derived rate */
    msa_sample samples[MSA_MAX_SAMPLES];
    int        sample_count;
} g_msa;

void recvx_msa_set_force_rate(int hz) {
    g_msa.force_rate = (hz > 0) ? hz : 0;
}

/* ==========================================================================
 * SCEI section walker
 * ==========================================================================
 *
 * Magic bytes on disk for "SCEI" + "Vagi" are stored as two little-endian
 * u32s, so the raw byte sequence reads "IECS" "igaV". We match that
 * literal pattern — no endian gymnastics needed for a pure byte compare.
 */
static int msa_tag_eq(const unsigned char* p, const char tag[4]) {
    /* p+0..3 holds "IECS"; p+4..7 holds tag reversed. */
    if (p[0]!='I'||p[1]!='E'||p[2]!='C'||p[3]!='S') return 0;
    return p[4]==tag[3] && p[5]==tag[2] && p[6]==tag[1] && p[7]==tag[0];
}

static uint32_t rd_u32_le(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

static uint16_t rd_u16_le(const unsigned char* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}

/* Walk the SCEI container from `start` looking for the 4-char tag.
 * `search_limit` bounds the walk. Returns section start on hit. */
static const unsigned char* msa_find_section(const unsigned char* buf,
                                             int buf_size,
                                             int start, int search_limit,
                                             const char tag[4]) {
    int off = start;
    if (search_limit > buf_size) search_limit = buf_size;
    while (off + 16 <= search_limit) {
        if (buf[off]=='I' && buf[off+1]=='E' && buf[off+2]=='C' && buf[off+3]=='S') {
            if (msa_tag_eq(buf + off, tag)) return buf + off;
            uint32_t sec_size = rd_u32_le(buf + off + 8);
            if (sec_size < 16 || off + (int)sec_size > search_limit) break;
            off += (int)sec_size;
        } else {
            off += 4;
        }
    }
    return NULL;
}

/* ==========================================================================
 * PSX-ADPCM decoder (16-byte frame → 28 S16 mono samples)
 * ==========================================================================
 *
 * Frame layout (all 16 bytes):
 *     b[0] = (predict << 4) | shift     predict in [0..4], shift in [0..12]
 *     b[1] = flags                       bit0=end,bit1=loop,bit2=start
 *     b[2..15] = 14 nibble-packed samples (low nibble first)
 *
 * Per sample: s = sign_extend(nibble << 12) >> shift;
 *             out = clamp(s + (h1*cp + h2*cn) >> 6);
 *
 * Coefficient tables are the canonical PSX SPU filter set.
 */
static const int vag_coef_pos[5] = { 0,  60, 115,  98, 122 };
static const int vag_coef_neg[5] = { 0,   0, -52, -55, -60 };

static int psx_adpcm_decode(const unsigned char* src, int src_bytes,
                            int16_t* dst, int dst_cap_samples) {
    int frames = src_bytes / 16;
    int out = 0;
    int h1 = 0, h2 = 0;

    for (int f = 0; f < frames && out + 28 <= dst_cap_samples; ++f) {
        const unsigned char* F = src + f * 16;
        int predict = (F[0] >> 4) & 0x0F;
        int shift   =  F[0]       & 0x0F;
        int flags   =  F[1];
        (void)flags;                        /* loop flags unused for SE */
        if (predict > 4) predict = 0;
        if (shift   > 12) shift = 9;        /* out-of-range → SPU soft-clamp */
        int cp = vag_coef_pos[predict];
        int cn = vag_coef_neg[predict];

        for (int i = 0; i < 14; ++i) {
            unsigned char b = F[2 + i];
            for (int half = 0; half < 2; ++half) {
                int n = half == 0 ? (b & 0x0F) : ((b >> 4) & 0x0F);
                int s = (int)((int16_t)((n << 12)));   /* sign-extend 4→16 */
                s >>= shift;
                int p = (h1 * cp + h2 * cn) >> 6;
                int sample = s + p;
                if (sample >  32767) sample =  32767;
                if (sample < -32768) sample = -32768;
                h2 = h1;
                h1 = sample;
                dst[out++] = (int16_t)sample;
            }
        }
    }
    return out;
}

/* ==========================================================================
 * Linear-interp mono→stereo resampler with volume scaling.
 * ==========================================================================
 *
 * Adequate for short menu SE. Swapping in a higher-order filter later
 * is contained to this one function. Volume here is applied to the
 * cached buffer (not in the mixer pump) because SE voices share the
 * same cached PCM across triggers — per-voice volume comes via the
 * slot->volume field on playback.
 */
static int16_t* mono_to_stereo_resample(const int16_t* src, int src_n,
                                        int src_rate, int dst_rate,
                                        int* out_bytes) {
    if (src_n <= 0 || src_rate <= 0 || dst_rate <= 0) {
        *out_bytes = 0;
        return NULL;
    }
    int64_t dst_n = ((int64_t)src_n * dst_rate + src_rate - 1) / src_rate;
    if (dst_n <= 0) dst_n = 1;
    int16_t* out = (int16_t*)malloc((size_t)(dst_n * 2 * sizeof(int16_t)));
    if (!out) { *out_bytes = 0; return NULL; }
    double ratio = (double)src_rate / (double)dst_rate;
    for (int64_t i = 0; i < dst_n; ++i) {
        double sp   = (double)i * ratio;
        int    i0   = (int)sp;
        double frac = sp - (double)i0;
        int    i1   = i0 + 1;
        if (i0 >= src_n) i0 = src_n - 1;
        if (i1 >= src_n) i1 = src_n - 1;
        double v = (1.0 - frac) * (double)src[i0] + frac * (double)src[i1];
        int s = (int)v;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        out[i*2 + 0] = (int16_t)s;
        out[i*2 + 1] = (int16_t)s;
    }
    *out_bytes = (int)(dst_n * 2 * sizeof(int16_t));
    return out;
}

/* ==========================================================================
 * COMMON.MLT loader
 * ==========================================================================
 *
 * Reads the whole file, parses MLT header for body-pool boundaries,
 * finds the Vagi section to get sample offsets, then slices+decodes
 * each sample into g_msa.samples.
 */
static unsigned char* load_file(const char* path, int* out_size) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { *out_size = 0; return NULL; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0) { fclose(fp); *out_size = 0; return NULL; }
    unsigned char* buf = (unsigned char*)malloc((size_t)n);
    if (!buf) { fclose(fp); *out_size = 0; return NULL; }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    if ((long)got != n) { free(buf); *out_size = 0; return NULL; }
    *out_size = (int)n;
    return buf;
}

int recvx_msa_init(const char* mlt_path) {
    if (g_msa.initialized) return 0;

    int mixer_rate = recvx_adx_ensure_audio_open(MSA_DEVICE_RATE_HINT);
    if (mixer_rate <= 0) {
        RX_LOG("msa", "audio device not available — SE disabled");
        return -1;
    }
    g_msa.mixer_rate = mixer_rate;

    int file_size = 0;
    unsigned char* buf = load_file(mlt_path, &file_size);
    if (!buf) {
        RX_LOG("msa", "failed to open MLT: %s", mlt_path);
        return -1;
    }
    RX_LOG("msa", "loaded %s (%d bytes)", mlt_path, file_size);

    if (file_size < 0x20) {
        RX_LOG("msa", "MLT too small: %d bytes", file_size);
        free(buf);
        return -1;
    }
    uint32_t hdr_size      = rd_u32_le(buf + 0x00);
    uint32_t block_offs[4] = {
        rd_u32_le(buf + 0x04),  /* end of metadata */
        rd_u32_le(buf + 0x08),  /* body start */
        rd_u32_le(buf + 0x0C),  /* body end   */
        rd_u32_le(buf + 0x10),  /* next MLT (MIDI) */
    };
    RX_LOG("msa", "MLT hdr_size=%u body=[0x%04x..0x%04x] next=0x%04x",
           hdr_size, block_offs[1], block_offs[2], block_offs[3]);

    if ((int)block_offs[1] >= file_size || (int)block_offs[2] > file_size ||
        block_offs[2] <= block_offs[1]) {
        RX_LOG("msa", "MLT block offsets out of range");
        free(buf);
        return -1;
    }

    /* Locate Vagi in the metadata region (before body pool starts). */
    const unsigned char* vagi = msa_find_section(buf, file_size,
                                                 (int)hdr_size,
                                                 (int)block_offs[1],
                                                 "Vagi");
    if (!vagi) {
        RX_LOG("msa", "no Vagi section found");
        free(buf);
        return -1;
    }
    uint32_t vagi_size  = rd_u32_le(vagi + 8);
    uint32_t vagi_count = rd_u32_le(vagi + 12);
    RX_LOG("msa", "Vagi: size=%u count=%u", vagi_size, vagi_count);

    if (vagi_count == 0 || vagi_count > MSA_MAX_SAMPLES) {
        RX_LOG("msa", "Vagi count %u out of range", vagi_count);
        free(buf);
        return -1;
    }

    /* Entry layout (8 B): {u8 zero; u8 center_note; u8 zero; u8 max_vel;
     *                     u32 body_offset_le}. The section offset table
     *                     at vagi[16..] points to per-entry starts in
     *                     Vagi coords, but in practice entries are
     *                     packed 8 B apart starting at a fixed offset.
     *                     We read them directly from the tail of the
     *                     section via the offset table.
     */
    uint32_t entry0_off = rd_u32_le(vagi + 16);
    int ent_bytes       = 8;
    if (vagi_count > 1) {
        uint32_t entry1_off = rd_u32_le(vagi + 16 + 4);
        ent_bytes = (int)(entry1_off - entry0_off);
        if (ent_bytes < 8 || ent_bytes > 64) ent_bytes = 8;
    }

    /* The Vagi table's "entry 0 offset" field points to a cell that in
     * COMMON.MLT is preceded by 4 B of zero padding; the real 8 B entry
     * records start 4 B later. Detect that zero-pad and skip past it. */
    uint32_t body_span = block_offs[2] - block_offs[1];
    const unsigned char* entries = vagi + entry0_off;
    if (rd_u32_le(entries) == 0) entries += 4;

    uint32_t body_offsets[MSA_MAX_SAMPLES + 1];
    for (uint32_t i = 0; i < vagi_count; ++i) {
        body_offsets[i] = rd_u32_le(entries + i * ent_bytes + 4);
    }
    body_offsets[vagi_count] = body_span;   /* end sentinel */

    /* ---- Smpl section parse (per-sample rate / center note) -------------
     *
     * Each Smpl entry observed as 42 B on COMMON.MLT:
     *   off 0x00 u16 vagi_id              (0..9)
     *   off 0x02 u16 flags/pad            (0x4001 constant in this bank)
     *   off 0x04 u16 velocity_max         (0x007F)
     *   off 0x06..0x13 ADSR / tune fields (see hex-dump analysis)
     *   off 0x0B  u8  center_note         (0x3C = MIDI 60 = C5)
     *   off 0x14 u16 sample_rate_hz       (0x5FE6 = 24550 Hz observed)
     *   off 0x16..0x29 per-octave pitch shifts + reserved
     *
     * The sample_rate field at 0x14 is the key output: it overrides our
     * guessed 22050 Hz default so beeps resample at the correct pitch.
     * Sanity-bounded to [4000..96000] Hz.
     */
    int smpl_rate[MSA_MAX_SAMPLES];
    int smpl_note[MSA_MAX_SAMPLES];
    for (int i = 0; i < MSA_MAX_SAMPLES; ++i) {
        smpl_rate[i] = 0;
        smpl_note[i] = 60;
    }

    const unsigned char* smpl = msa_find_section(buf, file_size,
                                                 (int)hdr_size,
                                                 (int)block_offs[1],
                                                 "Smpl");
    if (smpl) {
        uint32_t smpl_size  = rd_u32_le(smpl + 8);
        uint32_t smpl_count = rd_u32_le(smpl + 12);
        uint32_t smpl_e0    = rd_u32_le(smpl + 16);
        int smpl_ent = 42;  /* observed entry stride */
        if (smpl_count > 1) {
            uint32_t smpl_e1 = rd_u32_le(smpl + 16 + 4);
            int stride = (int)(smpl_e1 - smpl_e0);
            if (stride >= 22 && stride <= 128) smpl_ent = stride;
        }
        RX_LOG("msa", "Smpl: size=%u count=%u stride=%d",
               smpl_size, smpl_count, smpl_ent);

        if (smpl_count > 0) {
            const unsigned char* e0 = smpl + smpl_e0;
            char hex[160]; int n = 0;
            int dump = smpl_ent < 42 ? smpl_ent : 42;
            for (int b = 0; b < dump && n + 3 < (int)sizeof(hex); ++b)
                n += snprintf(hex + n, sizeof(hex) - n, "%02x ", e0[b]);
            RX_LOG("msa", "Smpl[0] raw: %s", hex);
            /* Candidate rate-field probes so we can A/B which offset is
             * the true sample rate if 0x14 turns out wrong by ear. */
            RX_LOG("msa", "Smpl[0] candidates: u16@0x0E=%u u16@0x10=%u "
                   "u16@0x12=%u u16@0x14=%u u16@0x16=%u",
                   rd_u16_le(e0 + 0x0E), rd_u16_le(e0 + 0x10),
                   rd_u16_le(e0 + 0x12), rd_u16_le(e0 + 0x14),
                   rd_u16_le(e0 + 0x16));
        }

        for (uint32_t i = 0; i < smpl_count; ++i) {
            const unsigned char* e = smpl + smpl_e0 + i * smpl_ent;
            uint16_t vid  = rd_u16_le(e + 0x00);
            uint16_t rate = rd_u16_le(e + 0x14);
            uint8_t  note = e[0x0B];
            if (vid < MSA_MAX_SAMPLES && rate >= 4000) {
                smpl_rate[vid] = rate;
                smpl_note[vid] = note ? note : 60;
                RX_LOG("msa", "Smpl[%u] → vagi=%u rate=%u Hz note=%u",
                       i, vid, rate, note);
            } else if (vid < MSA_MAX_SAMPLES) {
                RX_LOG("msa", "Smpl[%u] vagi=%u rate=%u out of range — ignored",
                       i, vid, rate);
            }
        }
    } else {
        RX_LOG("msa", "no Smpl section — using default 22050 Hz for all");
    }

    g_msa.sample_count = 0;
    for (uint32_t i = 0; i < vagi_count; ++i) {
        uint32_t off = body_offsets[i];
        uint32_t end = body_offsets[i + 1];
        if (end <= off || end > body_span) {
            RX_LOG("msa", "sample %u bad range [%u..%u]", i, off, end);
            continue;
        }
        const unsigned char* vag = buf + block_offs[1] + off;
        int vag_bytes = (int)(end - off);

        int max_samples = (vag_bytes / 16) * 28;
        int16_t* mono = (int16_t*)malloc((size_t)max_samples * sizeof(int16_t));
        if (!mono) continue;
        int decoded = psx_adpcm_decode(vag, vag_bytes, mono, max_samples);

        /* Resolution order: CLI --msa-rate override → Smpl byte 0x14 →
         * RE-series default. The CLI path lets us eyeball different rate
         * guesses without rebuilding while we're still pinning down which
         * Smpl field actually stores the authentic rate. */
        int src_rate;
        if      (g_msa.force_rate > 0) src_rate = g_msa.force_rate;
        else if (smpl_rate[i] > 0)     src_rate = smpl_rate[i];
        else                           src_rate = MSA_SAMPLE_RATE;

        int out_bytes = 0;
        int16_t* stereo = mono_to_stereo_resample(mono, decoded,
                                                  src_rate,
                                                  g_msa.mixer_rate,
                                                  &out_bytes);
        free(mono);
        if (!stereo) {
            RX_LOG("msa", "sample %u resample failed", i);
            continue;
        }

        g_msa.samples[g_msa.sample_count].pcm   = stereo;
        g_msa.samples[g_msa.sample_count].bytes = out_bytes;
        RX_LOG("msa", "sample %u: vag=0x%04x bytes=%d decoded=%d src=%dHz pcm_bytes=%d",
               i, off, vag_bytes, decoded, src_rate, out_bytes);
        ++g_msa.sample_count;
    }

    free(buf);
    g_msa.initialized = 1;
    RX_LOG("msa", "init ok: %d samples cached @ %d Hz",
           g_msa.sample_count, g_msa.mixer_rate);
    return 0;
}

void recvx_msa_shutdown(void) {
    for (int i = 0; i < g_msa.sample_count; ++i) {
        if (g_msa.samples[i].pcm) free(g_msa.samples[i].pcm);
        g_msa.samples[i].pcm   = NULL;
        g_msa.samples[i].bytes = 0;
    }
    g_msa.sample_count = 0;
    g_msa.initialized  = 0;
}

int recvx_msa_play_se(int se_no, int volume) {
    if (!g_msa.initialized || g_msa.sample_count <= 0) return -1;
    int idx = se_no;
    if (idx < 0) idx = -idx;
    idx %= g_msa.sample_count;

    int slot = recvx_adx_alloc_free_slot(MSA_SE_SLOT_FIRST, MSA_SE_SLOT_LAST);
    if (slot < 0) return -1;

    /* PS2 SCE audio convention: Volume=0 at the public API level means
     * "use the patch's default level", not "mute". Callers like
     * CallSystemSeBasic(3, 0, 0) in adv.c rely on this. Map 0 → ~0.78
     * (100/127) to match a typical default. Explicit mutes come as
     * negative values if at all. */
    float vol;
    if      (volume <= 0)   vol = 100.0f / 127.0f;
    else if (volume >= 127) vol = 1.0f;
    else                    vol = (float)volume / 127.0f;

    const msa_sample* s = &g_msa.samples[idx];
    return recvx_adx_slot_play_pcm(slot, s->pcm, s->bytes, vol);
}
