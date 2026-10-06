/* Window and input layer of the PC port (see pc_window.c). */
#ifndef PC_WINDOW_H
#define PC_WINDOW_H

/* Opens the window and starts feeding input to the game. Returns 0 when the
 * game runs without a window. */
int pc_window_open(void);

/* The keyboard settings: the cvx.ini setting of action i (key_up, ...) and
 * its default keys, or NULL after the last one. */
const char *pc_window_key_setting(int i, const char **keys);

/* The settings window shown before the game (pc_launcher.c), when cvx.ini
 * asks for it (launcher, on by default), the game is started with --config
 * or Shift held, or the disc image is missing. The settings are saved into
 * cvx.ini. Returns 0 when the player quit instead of playing. */
int pc_launcher_run(int argc, char *argv[]);

#endif
