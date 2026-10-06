/*
 * Host services for the platform layer. These are compiled without the
 * game's settings (see port_prefix.h), so they can use the system headers.
 */
#ifndef PC_HOST_H
#define PC_HOST_H

#include <stdint.h>

/* Monotonic time in nanoseconds. */
int64_t pc_host_time_ns(void);

/* Sleeps for about ns nanoseconds. */
void pc_host_sleep_ns(int64_t ns);

/* Starts the diagnostics thread (see pc_diag.c); call from the main thread. */
void pc_diag_start(void);

/* Counts V-blanks reached by the game, for the diagnostics. */
extern volatile uint32_t pc_diag_vblanks;

/* Time spent waiting for the next V-blank and showing frames, for the
 * diagnostics (reset every second). */
extern volatile int64_t pc_diag_wait_ns, pc_diag_show_ns;

/* Copies the console output into cvx_log.txt and keeps the console open when
 * the program ends unexpectedly (see pc_log.c). Call first in main(). */
void pc_log_start(void);

/* Called by the crash handler once it has printed its report. */
void pc_log_crash_done(void);

/* Set when the player closes the window (no need to keep the console open). */
extern volatile int pc_quit_requested;

/* Set while the fast-forward key is held: frames are not paced. */
extern volatile int pc_turbo;

/* ---- Movies: MPEG video decoder (pc_video.c, FFmpeg) ---- */

typedef struct PcVideo PcVideo;

/* Returns NULL when the port is built without a decoder. */
PcVideo *pc_video_open(void);
void pc_video_close(PcVideo *v);

/* Feeds bytes of the video elementary stream; size 0 marks its end. Returns
 * the number of decoded frames waiting. */
int pc_video_feed(PcVideo *v, const uint8_t *data, int size);
int pc_video_ready(PcVideo *v);

/* Set when enough frames wait: stop feeding for now. */
int pc_video_full(PcVideo *v);

/* Takes the oldest decoded frame (RGBA, valid until the next call), or NULL. */
const uint32_t *pc_video_take(PcVideo *v, int *w, int *h);

double pc_video_fps(PcVideo *v);

/* ---- Replacement movies (pc_video.c, FFmpeg) ----
 * A video file made by the player (an upscaled movie, in MP4, MKV, WebM...)
 * shown instead of the picture of a game movie. */

typedef struct PcHdMovie PcHdMovie;

/* Returns NULL when the file cannot be played. */
PcHdMovie *pc_hdmovie_open(const char *path);
void pc_hdmovie_close(PcHdMovie *m);

/* The picture to show t seconds from the start (RGBA, valid until the next
 * call), or NULL before the first one. *changed is set when it is not the
 * picture returned last time. */
const uint32_t *pc_hdmovie_frame(PcHdMovie *m, double t, int *w, int *h, int *changed);

/* ---- Picture shown over the game's screen (pc_host.c) ----
 * Used for replacement movies: drawn by the window at its own resolution
 * over the area (x, y, cw, ch) of the game's 640x448 picture. serial
 * changes whenever the pixels do. rgba NULL: nothing is shown. */
typedef struct {
    const uint32_t *rgba;
    int w, h;
    int x, y, cw, ch;
    unsigned int serial;
} PcOverlay;

extern PcOverlay pc_overlay;

/* ---- Sound output (pc_audio.c, played by the window's audio device) ---- */

#define PC_AUDIO_RATE 48000

/* Queues 16-bit stereo samples (interleaved) at the given rate. */
void pc_audio_push(const int16_t *lr, int frames, int rate);

/* Fills out with the next frames: the mixer hook plus the queue. Called by
 * the audio device's thread. */
void pc_audio_pull(int16_t *out, int frames);

/* Adds the game's sounds (pc_sound.c) into lr, in 16-bit sample units. It is
 * called with the audio lock held. */
extern void (*pc_audio_mix_hook)(float *lr, int frames);

/* Taken by the game thread while it changes what the mixer reads. */
void pc_audio_lock(void);
void pc_audio_unlock(void);

/* Frames queued and not played yet. */
int pc_audio_queued(void);

/* Drops everything queued. */
void pc_audio_clear(void);

/* Set by the window when an audio device is open. */
extern volatile int pc_audio_open;

#endif
