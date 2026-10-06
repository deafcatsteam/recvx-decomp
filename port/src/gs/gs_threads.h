/*
 * Splits the rows of a primitive between worker threads. Pixels of one
 * primitive never overlap, so its rows can be drawn in any order; the call
 * returns once every row is done, so primitives stay in order.
 */
#ifndef GS_THREADS_H
#define GS_THREADS_H

/* Draws rows [y0, y1). */
typedef void (*GsBandFn)(void *ctx, int y0, int y1);

/* Runs fn over [begin, end) in bands, on several threads when work (an
 * estimate of the pixel count) is large enough. CVX_GS_THREADS sets the
 * number of threads (1 turns this off). */
void gs_parallel(GsBandFn fn, void *ctx, int begin, int end, int work);

/* Number of threads drawing (1 when the work is not split). */
int gs_thread_count(void);

#endif
