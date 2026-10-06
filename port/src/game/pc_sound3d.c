/*
 * 3D sound, the game's side (sound_3d in cvx.ini, see host/pc_spatial.h).
 *
 * The game places its effects in the room itself: from the camera and the
 * sound's position it works out a pan and a volume (Get3DSoundParameter in
 * sdfunc.c), and the pan alone reaches the sound driver. The pan cannot
 * tell in front from behind, nor above from below, so the places that
 * start or move a placed sound (the player's steps and weapons, enemies,
 * objects, events) also give its position, through pc_snd_at, under
 * #ifdef PLATFORM_PC. The next sound request takes it over (ExPlaySe and
 * ExPlayMidi in sdc.c) as a direction from the camera, which goes with the
 * request to the driver (ps2_sg_sd.c, pc_snddrv_dir) and the synthesizer.
 * Requests made without one, like the menus' sounds and the music, keep
 * the game's pan.
 */
#include "ps2_dummy.h"
#include "ps2_NaMatrix.h"
#include "main.h"

static int pending;
static NJS_POINT3 pending_pos;
static unsigned int pending_frame;

void pc_snd_at(const float *pos)
{
    pending = pos != NULL;
    if (pos != NULL) {
        pending_pos.x = pos[0];
        pending_pos.y = pos[1];
        pending_pos.z = pos[2];
    }
    pending_frame = Ps2_sys_cnt;
}

int pc_snd_take(float dir[3])
{
    NJS_POINT3 pd;
    int have = pending && pending_frame == Ps2_sys_cnt;

    pending = 0;
    dir[0] = dir[1] = 0.0f;
    dir[2] = 1.0f;
    if (!have)
        return 0;
    /* In the camera's space (as in Get3DSoundParameter) x is right, y up
     * and the camera looks down -z. */
    njCalcPoint(cam.mtx, &pending_pos, &pd);
    dir[0] = pd.x;
    dir[1] = pd.y;
    dir[2] = -pd.z;
    return 1;
}
