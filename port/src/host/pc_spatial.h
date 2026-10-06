/*
 * 3D sound (sound_3d in cvx.ini): puts a mono sound at a direction around
 * the listener, for the voices whose position the game knows (its effects
 * placed in the room, see pc_sound3d.c).
 *
 *   headphones  binaural: the far ear hears the sound a little later (up to
 *               0.66 ms) and duller (the head's shadow), and a sound from
 *               behind is a little muffled. A spherical head model (Brown
 *               and Duda), no measured HRTF.
 *   speakers    plain stereo panning, but across the whole width, from the
 *               direction (the PS2's own pan only moves sounds a little).
 *
 * Off (the default), voices keep the pan the game gives them, as on PS2.
 */
#ifndef PC_SPATIAL_H
#define PC_SPATIAL_H

enum { PC_3D_OFF, PC_3D_HEADPHONES, PC_3D_SPEAKERS };

/* The mode, read from cvx.ini on the first call. */
int pc_spatial_mode(void);
void pc_spatial_set_mode(int mode);

#define PC_SPATIAL_DELAY 64 /* samples kept for the delay between the ears */

typedef struct {
    float ring[PC_SPATIAL_DELAY];
    int w;
    /* targets from the direction, and the values in use, which move
     * towards them a little at each sample so that nothing clicks */
    float target[6], cur[6]; /* delay left/right (samples), shadow alpha left/right, behind, unused */
    float shadow[2][2];      /* head shadow filter state per ear: last input, last output */
    float rear[2];           /* low-pass of the sound from behind, per ear */
    int primed;
} PcSpatial;

/* Direction of the sound: x right, y up, z forward (any length). */
void pc_spatial_aim(PcSpatial *s, const float dir[3]);
void pc_spatial_reset(PcSpatial *s);

/* One sample in (mono), the two ears out (headphones mode). */
void pc_spatial_run(PcSpatial *s, float in, float out[2]);

/* Speakers mode: left and right gains (constant power, 1 in the middle). */
void pc_spatial_pan(const float dir[3], float g[2]);

#endif
