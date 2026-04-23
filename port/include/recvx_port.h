#ifndef RECVX_PORT_H
#define RECVX_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Logging
 * -------------------------------------------------------------------------- */
void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...)  recvx_log(tag, __VA_ARGS__)

/* --------------------------------------------------------------------------
 * Backend interface
 *
 * Every renderer (GL, Vulkan, etc.) implements the same vtable so we can
 * swap them at startup. Phase 0 ships GL + null backends only.
 * -------------------------------------------------------------------------- */
typedef struct recvx_backend_config {
    int  width;
    int  height;
    bool fullscreen;
    bool vsync;
    const char* window_title;
} recvx_backend_config;

typedef struct recvx_backend {
    const char* name;
    int  (*init)(const recvx_backend_config* cfg);
    void (*shutdown)(void);
    void (*begin_frame)(void);
    void (*end_frame)(void);
    bool (*pump_events)(void); /* false → quit requested */
    /* Phase 4: upload + draw an RGBA frame as a fullscreen textured quad.
     * Called between begin_frame and end_frame. The backend stretches
     * the source image to the window. */
    void (*draw_rgba)(const void* pixels, int w, int h);
    /* Phase 4: PCM audio sink. audio_init lazily opens the SDL device.
     * audio_queue appends interleaved S16 stereo samples at `rate` Hz.
     * Rate is fixed for the lifetime of the open device; a second
     * audio_init with a different rate closes and reopens. */
    void (*audio_init)(int sample_rate);
    void (*audio_queue)(const void* samples, int byte_count);
} recvx_backend;

const recvx_backend* recvx_backend_gl(void);
const recvx_backend* recvx_backend_null(void);

/* Globally-live backend pointer. Set by main_pc after backend->init so
 * subsystems (audio player, future mixers) can push PCM / surfaces
 * without threading the backend handle through every stub. */
void                 recvx_backend_set_current(const recvx_backend* b);
const recvx_backend* recvx_backend_current(void);

/* --------------------------------------------------------------------------
 * ISO reader
 * -------------------------------------------------------------------------- */
typedef struct recvx_iso recvx_iso_t;

recvx_iso_t* recvx_iso_open(const char* path);
void         recvx_iso_close(recvx_iso_t* iso);
int          recvx_iso_find(recvx_iso_t* iso, const char* path,
                            uint32_t* out_lba, uint32_t* out_size);
int          recvx_iso_read_sectors(recvx_iso_t* iso, uint32_t lba,
                                    uint32_t count, void* buf);

/* Selected as the global ISO once the port has parsed its CLI / config */
void         recvx_iso_set_global(recvx_iso_t* iso);
recvx_iso_t* recvx_iso_global(void);

/* Debug: log every entry in the directory at `dir_path` (use "" for root). */
void         recvx_iso_debug_listdir(recvx_iso_t* iso, const char* dir_path);

/* --------------------------------------------------------------------------
 * FMV (Sofdec replacement)
 * -------------------------------------------------------------------------- */
typedef struct recvx_fmv recvx_fmv_t;

recvx_fmv_t* recvx_fmv_open(const char* iso_path);
void         recvx_fmv_close(recvx_fmv_t* fmv);
bool         recvx_fmv_advance(recvx_fmv_t* fmv); /* false when finished */
/* Pointer to the current RGBA frame + its dimensions. Valid after a
 * successful advance(); NULL before the first decoded frame. */
const void*  recvx_fmv_pixels(const recvx_fmv_t* fmv);
int          recvx_fmv_width (const recvx_fmv_t* fmv);
int          recvx_fmv_height(const recvx_fmv_t* fmv);
double       recvx_fmv_pts_s (const recvx_fmv_t* fmv); /* current frame PTS */

/* Audio: the FMV decodes any muxed audio stream alongside video. The
 * caller registers a sink that gets invoked with interleaved S16 stereo
 * at `sample_rate` Hz each time a chunk lands during advance(). */
typedef void (*recvx_fmv_audio_sink)(void* opaque, int sample_rate,
                                     const void* s16_stereo, int byte_count);
void recvx_fmv_set_audio_sink(recvx_fmv_t* fmv,
                              recvx_fmv_audio_sink sink, void* opaque);

/* --------------------------------------------------------------------------
 * Input (Sega Ninja peripheral model).
 *
 * Game reads input via njGetPeripheral(port) which returns a PDS_PERIPHERAL*.
 * We maintain that struct here driven by SDL keyboard + gamepad events.
 * The backend calls recvx_input_set_key() from its event pump, and
 * recvx_input_new_frame() before each njUserMain() to compute press/release
 * edges from the previous frame's `on` snapshot.
 * -------------------------------------------------------------------------- */
/* Abstract scan-code enum so the backend doesn't leak SDL into the port. */
typedef enum recvx_key {
    RX_KEY_UP, RX_KEY_DOWN, RX_KEY_LEFT, RX_KEY_RIGHT,
    RX_KEY_ACTION,   /* Cross / OK   */
    RX_KEY_CANCEL,   /* Circle       */
    RX_KEY_AIM,      /* Square       */
    RX_KEY_MENU,     /* Triangle     */
    RX_KEY_L1,
    RX_KEY_R1,
    RX_KEY_L2,
    RX_KEY_R2,
    RX_KEY_START,
    RX_KEY_SELECT,
    RX_KEY__COUNT
} recvx_key;

void recvx_input_set_key(recvx_key k, bool pressed);
void recvx_input_set_stick(int axis_x, int axis_y);  /* -128..127 */
void recvx_input_new_frame(void);  /* compute edges; call once per njUserMain */

/* Diagnostic snapshot of the Sega Ninja peripheral "on" bitmap for the
 * main loop's input-change log. Bits follow PDD_DGT_* from sg_pad.h
 * (e.g. bit 3 = Start, bit 5 = KD). Not part of the decomp's ABI. */
uint32_t recvx_input_buttons(void);

/* Auto-repeat snapshot for Pad[].Rept. Mirrors the onon register from
 * ps2_sg_pad.c's Pad_set — fires once on rising edge, then every other
 * frame while held. CheckButton uses this for UP/DOWN cursor nav. */
uint32_t recvx_input_rept(void);

/* --------------------------------------------------------------------------
 * 2D gfx API for game nj* draw primitives.
 *
 * The game draws in PS2 screen-space: origin top-left, 640x480 logical.
 * Backend implements these; game_texture_stubs.c + friends call them from
 * njQuadTextureStart / njSetQuadTexture / njDrawQuadTexture / njDrawPolygon /
 * njTextureFilterMode. Keeping the game side free of GL headers means game
 * target builds only against KATANA + compat; all GL calls live in backend.
 *
 * Texture slots are a flat 0..63 array that mirrors the low-4GiB NJS_TEXMEMLIST
 * pool in game_texture_stubs.c. The game references textures by NJS_TEXMEMLIST
 * pointer; we convert that to a slot index = (ptr - pool_base)/sizeof(entry).
 * -------------------------------------------------------------------------- */
#define RX_GFX_TEX_SLOTS 64

void recvx_gfx_begin_2d(int target_w, int target_h);
void recvx_gfx_end_2d(void);

/* Uploads RGBA8 pixels to the backend texture for this slot. Creates the
 * GL texture on first call, re-uploads on size change or re-decode. */
void recvx_gfx_tex_upload(int slot, const void* rgba, int w, int h);

/* Textured quad in screen-space. color is ARGB32 (A in top byte);
 * trans enables alpha blending. z is unused for 2D but passed through. */
void recvx_gfx_draw_quad(int slot,
                         float x1, float y1, float x2, float y2,
                         float u1, float v1, float u2, float v2,
                         float z, uint32_t color, int trans);

/* Vertex-colored polygon (fan). Each vertex is {x,y,z,ARGB}. */
typedef struct recvx_gfx_vtx {
    float    x, y, z;
    uint32_t color;
} recvx_gfx_vtx;
void recvx_gfx_draw_polygon(const recvx_gfx_vtx* verts, int count, int trans);

/* 0 = nearest, 1 = linear. Matches njTextureFilterMode conventions. */
void recvx_gfx_set_filter(int mode);

/* Frame pacer. Sleep + spin until 1/target_hz seconds elapsed since the
 * previous call. Pass 0 to just reset the clock (no wait). Used by the
 * game loop to cap njUserMain at PS2 NTSC 60 Hz even on high-refresh
 * monitors where SDL vsync alone lets the loop run at 144+ Hz. */
void recvx_backend_pace(int target_hz);

/* --------------------------------------------------------------------------
 * ADX audio player (CRI ADX streaming decoded via FFmpeg).
 *
 * adv.c calls PlayAdx(slot, part, file) where `part` is a PatId[] index
 * (0=BGM, 1=VOICE, 2=SE, 3=ADV) and `file` is the AFS inside-file id.
 * The stubs in stub_game.c route into these. We read the whole AFS
 * entry into memory, decode with libavformat+libavcodec's ADPCM_ADX,
 * and push the PCM to the current backend's audio_init/audio_queue.
 * -------------------------------------------------------------------------- */
int  recvx_adx_play      (int slot, int part, int file);
/* Same as play() but with a loop flag: 1 = wrap to 0 on reaching end,
 * used for BGM so it keeps running until StopBgm / re-triggered. */
int  recvx_adx_play_ex   (int slot, int part, int file, int loop);
void recvx_adx_stop      (int slot);
void recvx_adx_set_volume(int slot, float volume);  /* 0.0..1.0 */
void recvx_adx_stop_all  (void);
/* Drain a chunk of decoded PCM from each active slot to the backend
 * audio queue. All slots mix into a single stereo stream. Call once
 * per frame from the game loop. */
void recvx_adx_pump      (void);

/* Lazily open the backend audio device at `default_rate` if it isn't
 * already. No-op if already open (returns the existing rate). Used by
 * the SE path so menu beeps work before any PlayAdx has locked the
 * rate. Returns the effective output sample rate, or -1 on failure. */
int  recvx_adx_ensure_audio_open(int default_rate);

/* Play pre-decoded S16 stereo PCM at the mixer's output rate on `slot`.
 * Buffer is non-owning — caller must keep it alive until the slot
 * completes or is stopped. Intended for short cached samples like
 * system SE (shared decoded PCM pool). Returns 0 on success. */
int  recvx_adx_slot_play_pcm(int slot, const void* s16_stereo, int byte_count,
                             float volume);

/* Pick an inactive slot in [first, last] and return its index, or -1
 * if every slot in that range is busy. Used to rotate SE voices. */
int  recvx_adx_alloc_free_slot(int first, int last);

/* --------------------------------------------------------------------------
 * SCEI MSA (CRI MANATEE sound bank) — system SE from COMMON.MLT
 *
 * sdfunc.c's CallSystemSe*() resolves an SeNo through a Sset → Prog →
 * Smpl → Vagi chain inside COMMON.MLT and plays the selected PSX-ADPCM
 * sample via MANATEE. We replicate just enough of that pipeline to play
 * audible menu beeps: load + decode all Vagi samples once at init, then
 * rotate through the voice pool (ADX slots 2..15) on each CallSystemSe.
 * -------------------------------------------------------------------------- */
int  recvx_msa_init(const char* mlt_path);
void recvx_msa_shutdown(void);

/* Override the per-sample source rate extracted from Smpl entries. Must be
 * called before recvx_msa_init. Hz > 0 forces that value for every sample;
 * Hz == 0 restores Smpl-driven rates. Used by main_pc's --msa-rate flag
 * to let us A/B different rate guesses without rebuilding. */
void recvx_msa_set_force_rate(int hz);

/* Override the SeNo → sample-index mapping for a single SeNo.
 *
 * Context: the COMMON.MLT Sset/Prog/Smpl chain is on-disk diagonal
 * (Sset[N].prog_id=N, Prog[N].smpl_id=N, Smpl[N].vagi_id=N) but the real
 * CRI MANATEE runtime reorders some menu SEs via logic we don't have
 * source for. The user-audible correct mapping for RECVX menu beeps is
 *   SeNo 0 (cancel)       → sample 3
 *   SeNo 2 (cursor-move)  → sample 0
 *   SeNo 3 (press-start)  → sample 2
 * and that is baked as the default in recvx_msa_init. This API + the
 * `--se-remap N:M,N:M,...` CLI flag let future banks (MULTSPQ, etc.)
 * override without rebuilding.
 *
 * sample_idx < 0 → clear any existing remap for this SeNo (identity). */
void recvx_msa_set_remap(int se_no, int sample_idx);

/* Play system SE. se_no indexes the Sset table (0..9 for COMMON.MLT).
 * volume is PS2 convention 0..127. Returns 0 on success. */
int  recvx_msa_play_se(int se_no, int volume);

/* Audition a specific cached sample by direct index (0..sample_count-1),
 * bypassing the SeNo remap. Used by the F1..F10 debug keybinds in the GL
 * backend so a user can identify each sample by ear and then set the
 * correct remap via --se-remap or recvx_msa_set_remap. Returns 0 on
 * success, -1 if idx is out of range or MSA isn't initialized. */
int  recvx_msa_play_sample(int sample_idx, int volume);

/* Count of cached samples (from COMMON.MLT Vagi). 0 if MSA not initialized. */
int  recvx_msa_sample_count(void);

/* --------------------------------------------------------------------------
 * Gamedata directory. Set by main_pc from the --gamedata CLI flag and
 * read by any subsystem that needs to open loose files (COMMON.MLT,
 * MANATEE.DRV, etc.) from outside the AFS archives.
 * -------------------------------------------------------------------------- */
const char* recvx_gamedata_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* RECVX_PORT_H */
