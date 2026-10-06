/*
 * Texture packs (textures in cvx.ini), for the GPU renderer: each texture
 * the game draws with is known by a fingerprint of its decoded picture
 * (its size and colours, wherever it sits in GS memory), which names its
 * file:
 *
 *   textures/dump/256x128_0123456789abcdef.png     exported (textures = dump)
 *   textures/replace/.../0123456789abcdef.png       replacement (hd or dump)
 *
 * A replacement can have any size (2x, 4x... the original) and any name
 * ending with the fingerprint, in any folder under textures/replace. Its
 * alpha goes from 0 (transparent) to 255 (opaque), like the exported files;
 * the GS's 128 for opaque is put back on loading. See gs_gpu.c for how it
 * is drawn (mipmaps, anisotropic filtering).
 */
#ifndef GS_TEXREP_H
#define GS_TEXREP_H

#include <stdint.h>

enum { TEXREP_OFF, TEXREP_HD, TEXREP_DUMP };

/* From cvx.ini (textures = original, hd or dump; default hd), read once;
 * the folder is "textures" or CVX_TEXTURES. */
int texrep_mode(void);
void texrep_set(int mode, const char *dir); /* for the tests */

/* The fingerprint of a decoded texture (RGBA, R in the low byte). */
uint64_t texrep_hash(const uint32_t *rgba, int w, int h);

/* Writes the texture to textures/dump once (mode dump), unless it has a
 * replacement. slot: where it comes from in GS memory; a slot whose picture
 * keeps changing (a movie, an animation drawn by the game) stops being
 * exported after a few pictures. */
void texrep_dump(uint64_t hash, const uint32_t *rgba, int w, int h, uint64_t slot);

/* Whether there is anything to do: textures to export or replacements. */
int texrep_busy(void);

/* Whether a replacement exists for hash. */
int texrep_has(uint64_t hash);

/* Reads the replacement: RGBA with the GS's alpha (128 opaque), malloc'ed,
 * or NULL. */
uint32_t *texrep_load(uint64_t hash, int *w, int *h);

/* F9: looks at the folder again (gs_gpu.c then drops what it loaded). The
 * window asks, the GS thread does it at its next texture. */
extern volatile int texrep_reload_asked;
int texrep_reload_pending(void);

#endif
