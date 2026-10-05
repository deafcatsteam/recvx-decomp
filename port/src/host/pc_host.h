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

#endif
