/* Entry points of the PC platform layer shared between its files. */
#ifndef PC_PLATFORM_H
#define PC_PLATFORM_H

/* Runs the game's V-blank interrupt handlers; called once per frame. */
void pc_vblank(void);

/* Waits for the next 60 Hz tick, then runs pc_vblank(). Setting the
 * CVX_NO_VSYNC environment variable turns the wait off (tests, benchmarks). */
void pc_wait_vblank(void);

/* Called once per V-blank: lets the window/input layer pump its events. */
extern void (*pc_frame_hook)(void);

/* Shows buffer id of a sceGsDBuffDc at once (pc_sdk.c). */
void pc_gs_show_dbuff(void *db, int id);

#endif
