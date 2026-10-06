/*
 * GPU renderer of the GS (OpenGL 3.3), chosen in cvx.ini (renderer =
 * opengl). The GS packets are still read by gs.c; the primitives are drawn
 * by the graphics card instead of the software pixel loops, optionally at a
 * higher resolution. See gs_gpu.c.
 */
#ifndef GS_GPU_H
#define GS_GPU_H

#include <stdint.h>

/* Set once gs_gpu_init succeeded: the GS draws through the GPU. */
extern int gs_gpu_on;

/* Starts the GPU renderer in the OpenGL context current on this thread
 * (the thread that drives the GS). getproc finds OpenGL functions by name.
 * scale (1 to 4) multiplies the PS2's resolution. Returns 0, with a line
 * in the log, if OpenGL 3.3 is missing: the software GS then goes on. */
int gs_gpu_init(void *(*getproc)(const char *name), int scale);

/* Draws the displayed picture, stretched to the rectangle x, y, w, h (from
 * the top left) of the window's frame buffer of size fb_w x fb_h; the rest
 * is black. smooth: linear filtering. */
void gs_gpu_present(int fb_w, int fb_h, int x, int y, int w, int h, int smooth);

/* Draws an RGBA picture (R in the low byte) over the rectangle x, y, w, h
 * of the window, after gs_gpu_present (replacement movies). changed: the
 * pixels are not those of the previous call. */
void gs_gpu_draw_picture(const uint32_t *rgba, int pw, int ph, int changed, int fb_w, int fb_h, int x, int y,
                         int w, int h);

#endif
