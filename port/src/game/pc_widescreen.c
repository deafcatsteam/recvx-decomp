/*
 * 16:9 widescreen (widescreen = yes in cvx.ini).
 *
 * The PS2 picture stays 640x448, but in game the window shows it stretched
 * to 16:9 instead of 4:3. To make up for it, the 3D is drawn 3/4 as wide
 * (the Ninja aspect, see pc_set_wide in ps2_NaView.c), so that it keeps its
 * shape and shows more on the sides, and the view volume, which decides
 * what is drawn and where polygons are cut, is made 4/3 as wide. The text
 * is narrowed the same way, around the middle of the screen, so that it
 * keeps its shape too, where the 4:3 picture would have it.
 *
 * Only the game itself is shown in 16:9: its rooms, real-time events and
 * door openings. The menus (items, map, files, saves, options), the
 * title, the movies and the game over screen are 2D pictures made for 4:3:
 * they are shown in 4:3 in the middle of the window, as before.
 */
#include "ps2_dummy.h"
#include "main.h"

#include "../host/pc_host.h"

/* Game tasks (bhSysTaskJumpTab, main.c), one bit each in sys->tk_flg,
 * stopped while their bit of sys->ts_flg is set. */
#define TASK_TITLE 0x10
#define TASK_OPENING 0x20
#define TASK_GAME 0x80
#define TASK_EVENT 0x100
#define TASK_ITEMSELECT 0x200
#define TASK_MAP 0x400
#define TASK_DOORDEMO 0x800
#define TASK_MOVIE 0x1000
#define TASK_ENDING 0x2000
#define TASK_GAMEOVER 0x4000
#define TASK_TYPEWRITER 0x8000
#define TASK_OPTION 0x10000
#define TASK_COMPEVENT 0x20000

#define TASKS_3D (TASK_GAME | TASK_EVENT | TASK_DOORDEMO | TASK_COMPEVENT)
#define TASKS_4_3 (TASK_TITLE | TASK_OPENING | TASK_ITEMSELECT | TASK_MAP | TASK_MOVIE | TASK_ENDING | \
                   TASK_GAMEOVER | TASK_TYPEWRITER | TASK_OPTION)

int pc_widescreen_on(unsigned int tk_flg, unsigned int ts_flg)
{
    unsigned int running = tk_flg & ~ts_flg;

    return (running & TASKS_3D) != 0 && (running & TASKS_4_3) == 0;
}

/* At the start of each frame of the game. */
void pc_widescreen_frame(void)
{
    static int enabled = -1;

    if (enabled < 0)
        enabled = pc_config_yes("widescreen", 0);
    pc_set_wide(enabled && pc_widescreen_on(sys->tk_flg, sys->ts_flg) ? 0.75f : 1.0f);
}

void pc_wide_text(float *xy, int n)
{
    int i;

    if (pc_wide_x == 1.0f)
        return;
    for (i = 0; i < n; i++)
        xy[i * 2] = 320.0f + (xy[i * 2] - 320.0f) * pc_wide_x;
}
