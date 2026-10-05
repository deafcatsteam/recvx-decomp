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

#endif
