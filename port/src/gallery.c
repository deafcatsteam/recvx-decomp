/*
 * gallery.c — visual TIM2 viewer over an AFS partition.
 *
 * Renders bg_viewer.png as a fixed UI background and each TIM2 entry of the
 * selected partition centered in the blue display area, with the entry's
 * "<partition>/NNNN" label rendered underneath in white using font.otf.
 *
 * Navigation:
 *   ← / →           prev / next entry
 *   Z (Cross)       next entry
 *   X (Circle)      prev entry
 *   ESC             quit
 *   Mouse click     left half = prev, right half = next (rough hit boxes
 *                   over the bg's painted arrows; tune later)
 *
 * Window: 1280x720, native — bg_viewer.png is exactly 1280x720 so no
 * scaling. The blue display area, arrow click rects, and text strip are
 * hardcoded percentages of the bg, defined as BG_* constants below.
 *
 * Skipped on non-TIM2 entries — partition listings include ADX, raw, etc.
 * The entry counter advances over them but renders a placeholder label.
 */

#include "recvx_port.h"
#include "recvx_afs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RECVX_HAVE_SDL2) && defined(RECVX_HAVE_GL)
#include <SDL.h>
#include <SDL_opengl.h>

#ifdef RECVX_HAVE_SDL2_IMAGE
#include <SDL_image.h>
#endif

#ifdef RECVX_HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

/* AFS partition filenames — must match main_pc.c's g_afs_filename order. */
static const char* k_afs_filename[7] = {
    "BGM1.AFS", "VOICE1.AFS", "MULTSPQ1.AFS", "ADV.AFS",
    "ITEM1.AFS", "MRY.AFS", "SYSTEM.AFS"
};

/* Hardcoded layout in 1280x720 bg_viewer.png coordinates. Tune these once
 * we have the asset on screen. */
#define BG_W            1280
#define BG_H             720
/* Blue display rectangle — where the TIM2 texture renders. Tuned to
 * the bg's painted bezel inset so the texture sits inside the visible
 * blue field, not over the gold border. */
#define BLUE_X           220
#define BLUE_Y           195
#define BLUE_W           620
#define BLUE_H           305
/* Painted arrow click hit boxes — generous rectangles around the small
 * green ◄ ► icons baked into the bg, ~mid-height of the blue rect. */
#define ARROW_W           80
#define ARROW_H           70
#define ARROW_L_X        205
#define ARROW_L_Y        315
#define ARROW_R_X        750
#define ARROW_R_Y        315
/* Black info strip beneath the blue rectangle. Caption anchored to
 * top-left with a generous left margin so it sits clear of the bg's
 * gold bezel. */
#define LABEL_X          175
#define LABEL_Y          528
#define LABEL_W          735
#define LABEL_H          150
#define LABEL_TEXT_X     (LABEL_X + 35)
#define LABEL_TEXT_Y     (LABEL_Y + 8)
#define LABEL_LINE_H     26

/* TIM2 picture header — local copy that matches RECVX's `TIM2_PICTUREHEADER`
 * in include/ps2/veronica/prog/types.h. Note the ClutType / ImageType swap
 * vs Sony's published TIM2 1.0 spec — RECVX has ClutType at 0x12 and
 * ImageType at 0x13. Don't "correct" this back to the spec or the decoder
 * silently rejects every entry.
 *
 *   0x00 u32 TotalSize       — total picture size (header + image + clut)
 *   0x04 u32 ClutSize        — clut data byte count
 *   0x08 u32 ImageSize       — image data byte count
 *   0x0C u16 HeaderSize      — picture-header size (e.g. 0x30)
 *   0x0E u16 ClutColors      — palette entry count
 *   0x10 u8  PictFormat      — always 0 in PS2 builds
 *   0x11 u8  MipMapTextures  — mipmap level count
 *   0x12 u8  ClutType        — clut format + CSM flag (bit 7)   ← RECVX order
 *   0x13 u8  ImageType       — 3=BGRA32, 4=4bpp paletted, 5=8bpp paletted
 *   0x14 u16 ImageWidth
 *   0x16 u16 ImageHeight
 *   0x18 ...                 — GsTex0/Tex1/Regs/TexClut, ignored
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t TotalSize;
    uint32_t ClutSize;
    uint32_t ImageSize;
    uint16_t HeaderSize;
    uint16_t ClutColors;
    uint8_t  PictFormat;
    uint8_t  MipMapTextures;
    uint8_t  ClutType;
    uint8_t  ImageType;
    uint16_t ImageWidth;
    uint16_t ImageHeight;
    uint64_t GsTex0;
    uint64_t GsTex1;
    uint32_t GsRegs;
    uint32_t GsTexClut;
} tim2_pic_hdr;
#pragma pack(pop)

#define TIM2_MAX_W   1024
#define TIM2_MAX_H   1024

static uint8_t g_tim2_scratch[TIM2_MAX_W * TIM2_MAX_H * 4];

static inline uint8_t alpha_ps2_to_pc(uint8_t a) {
    int x = (int)a * 2;
    return (uint8_t)(x > 255 ? 255 : x);
}
static inline uint8_t idx_csm1_swap(uint8_t i) {
    return (uint8_t)((i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1));
}

/* Decode a TIM2 blob into RGBA8. Both bare TIM2 (picture header @ +16,
 * after the 16-byte file header) and the in-game EX layout (game-extended
 * 128-byte file header, picture header @ +128) start with "TIM2" magic
 * at offset 0. We try both candidate offsets and pick whichever produces
 * a sane ImageWidth/Height — RECVX's AFS entries use the EX layout but
 * dumps from third-party tools may strip it down to bare. */
static int gallery_tim2_decode(const void* blob, uint32_t blob_size,
                               int* out_w, int* out_h, uint8_t** out_pixels) {
    const uint8_t* pp = (const uint8_t*)blob;
    if (!pp || blob_size < 0x40) return 0;
    if (pp[0] != 'T' || pp[1] != 'I' || pp[2] != 'M' || pp[3] != '2') return 0;

    int hdr_off = -1;
    /* Prefer EX (RECVX's in-memory layout) — try it first. */
    if (blob_size >= 128 + sizeof(tim2_pic_hdr)) {
        const tim2_pic_hdr* p = (const tim2_pic_hdr*)(pp + 128);
        if (p->ImageWidth > 0 && p->ImageWidth <= TIM2_MAX_W &&
            p->ImageHeight > 0 && p->ImageHeight <= TIM2_MAX_H &&
            (p->ImageType == 3 || p->ImageType == 4 || p->ImageType == 5)) {
            hdr_off = 128;
        }
    }
    /* Fallback: bare TIM2 with 16-byte file header. */
    if (hdr_off < 0 && blob_size >= 16 + sizeof(tim2_pic_hdr)) {
        const tim2_pic_hdr* p = (const tim2_pic_hdr*)(pp + 16);
        if (p->ImageWidth > 0 && p->ImageWidth <= TIM2_MAX_W &&
            p->ImageHeight > 0 && p->ImageHeight <= TIM2_MAX_H &&
            (p->ImageType == 3 || p->ImageType == 4 || p->ImageType == 5)) {
            hdr_off = 16;
        }
    }
    if (hdr_off < 0) return 0;

    const tim2_pic_hdr* ph = (const tim2_pic_hdr*)(pp + hdr_off);
    int w = (int)ph->ImageWidth;
    int h = (int)ph->ImageHeight;

    const uint8_t* img  = pp + hdr_off + ph->HeaderSize;
    const uint8_t* clut = img + ph->ImageSize;

    /* Bounds check against blob end. */
    if ((size_t)(clut + ph->ClutSize - pp) > (size_t)blob_size) return 0;

    int image_type    = ph->ImageType;
    int csm1_swizzled = (ph->ClutType & 0x80) == 0;
    int clut_fmt      = ph->ClutType & 0x07;
    uint8_t* dst = g_tim2_scratch;

    if (image_type == 3) {
        for (int i = 0; i < w * h; ++i) {
            dst[i*4+0] = img[i*4+0];
            dst[i*4+1] = img[i*4+1];
            dst[i*4+2] = img[i*4+2];
            dst[i*4+3] = alpha_ps2_to_pc(img[i*4+3]);
        }
    } else if (image_type == 5) {
        if (clut_fmt != 3) return 0;
        for (int i = 0; i < w * h; ++i) {
            uint8_t idx = img[i];
            if (csm1_swizzled) idx = idx_csm1_swap(idx);
            const uint8_t* pal = clut + idx * 4;
            dst[i*4+0] = pal[0]; dst[i*4+1] = pal[1];
            dst[i*4+2] = pal[2]; dst[i*4+3] = alpha_ps2_to_pc(pal[3]);
        }
    } else if (image_type == 4) {
        if (clut_fmt != 3) return 0;
        int swizzle_4bpp = csm1_swizzled && (ph->ClutSize >= 128);
        for (int i = 0; i < w * h; ++i) {
            uint8_t byte   = img[i >> 1];
            uint8_t nibble = (i & 1) ? (uint8_t)(byte >> 4)
                                     : (uint8_t)(byte & 0x0F);
            if (swizzle_4bpp) nibble = idx_csm1_swap(nibble);
            const uint8_t* pal = clut + nibble * 4;
            dst[i*4+0] = pal[0]; dst[i*4+1] = pal[1];
            dst[i*4+2] = pal[2]; dst[i*4+3] = alpha_ps2_to_pc(pal[3]);
        }
    } else {
        return 0;
    }

    *out_w = w; *out_h = h; *out_pixels = dst;
    return 1;
}

/* Upload an RGBA buffer as a GL texture, returning the texture name.
 * Caller frees with glDeleteTextures. */
static GLuint gallery_make_tex(const void* rgba, int w, int h) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return id;
}

#ifdef RECVX_HAVE_SDL2_IMAGE
static GLuint gallery_load_png(const char* path, int* out_w, int* out_h) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) {
        RX_LOG("gallery", "IMG_Load(%s) failed: %s", path, IMG_GetError());
        return 0;
    }
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    if (!rgba) return 0;
    GLuint id = gallery_make_tex(rgba->pixels, rgba->w, rgba->h);
    *out_w = rgba->w; *out_h = rgba->h;
    SDL_FreeSurface(rgba);
    return id;
}
#endif

#ifdef RECVX_HAVE_SDL2_TTF
static TTF_Font* gallery_load_font(const char* path, int pt_size) {
    TTF_Font* f = TTF_OpenFont(path, pt_size);
    if (!f) RX_LOG("gallery", "TTF_OpenFont(%s) failed: %s", path, TTF_GetError());
    return f;
}

/* Render an UTF-8 string to a fresh GL texture (white text, transparent
 * background). Caller frees the GL texture. Returns 0 on failure. */
static GLuint gallery_text_to_tex(TTF_Font* f, const char* text,
                                  int* out_w, int* out_h) {
    if (!f || !text) return 0;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text, white);
    if (!surf) { RX_LOG("gallery", "TTF_Render: %s", TTF_GetError()); return 0; }
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba) return 0;
    GLuint id = gallery_make_tex(rgba->pixels, rgba->w, rgba->h);
    *out_w = rgba->w; *out_h = rgba->h;
    SDL_FreeSurface(rgba);
    return id;
}
#endif

/* Draw a textured quad at (x,y) with size (w,h), multiplied by `tint`. */
static void gallery_draw_quad(GLuint tex, int x, int y, int w, int h,
                              float r, float g, float b, float a) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f((float)x,         (float)y);
    glTexCoord2f(1, 0); glVertex2f((float)(x + w),   (float)y);
    glTexCoord2f(1, 1); glVertex2f((float)(x + w),   (float)(y + h));
    glTexCoord2f(0, 1); glVertex2f((float)x,         (float)(y + h));
    glEnd();
}

/* part == -1 means scan ALL partitions; 0..6 means a specific one. */
int run_gallery(const recvx_backend* backend, int part,
                const char* gamedata_dir) {
    if (part > 6) {
        fprintf(stderr, "--gallery: partition must be -1 (all) or 0..6\n");
        return 1;
    }

#ifdef RECVX_HAVE_SDL2_IMAGE
    if (IMG_Init(IMG_INIT_PNG) == 0) {
        RX_LOG("gallery", "IMG_Init failed: %s", IMG_GetError());
    }
#endif
#ifdef RECVX_HAVE_SDL2_TTF
    if (TTF_Init() != 0) {
        RX_LOG("gallery", "TTF_Init failed: %s", TTF_GetError());
    }
#endif

    /* Resolve asset paths — try several CWDs the user might launch from:
     *   ./build/Debug/recvx_pc.exe   (cwd = port/)
     *   ./recvx_pc.exe               (cwd = port/build/Debug/)
     *   ./port/build/...             (cwd = repo root) */
    static const char* k_bg_paths[] = {
        "src/custom/bg_viewer.png",
        "../../src/custom/bg_viewer.png",
        "port/src/custom/bg_viewer.png",
        "../port/src/custom/bg_viewer.png",
        NULL
    };
    static const char* k_font_paths[] = {
        "src/custom/RobotoMono-Light.ttf",
        "../../src/custom/RobotoMono-Light.ttf",
        "port/src/custom/RobotoMono-Light.ttf",
        "../port/src/custom/RobotoMono-Light.ttf",
        /* Fall back to the older .otf if the new font wasn't dropped in. */
        "src/custom/font.otf",
        "../../src/custom/font.otf",
        "port/src/custom/font.otf",
        "../port/src/custom/font.otf",
        NULL
    };

    /* --- Load assets --- */
    int bg_w = 0, bg_h = 0;
    GLuint bg_tex = 0;
#ifdef RECVX_HAVE_SDL2_IMAGE
    for (int i = 0; k_bg_paths[i] && !bg_tex; ++i) {
        bg_tex = gallery_load_png(k_bg_paths[i], &bg_w, &bg_h);
        if (bg_tex) RX_LOG("gallery", "bg loaded from %s (%dx%d)",
                           k_bg_paths[i], bg_w, bg_h);
    }
#endif
    if (!bg_tex) {
        RX_LOG("gallery", "WARN: bg_viewer.png not loaded — running plain");
    }

#ifdef RECVX_HAVE_SDL2_TTF
    TTF_Font* font = NULL;
    for (int i = 0; k_font_paths[i] && !font; ++i) {
        font = gallery_load_font(k_font_paths[i], 22);
        if (font) RX_LOG("gallery", "font loaded from %s", k_font_paths[i]);
    }
#else
    void* font = NULL;
#endif

    /* --- Build slide list ----------------------------------------------
     * RECVX's ADV.AFS / ITEM1.AFS / etc. wrap multiple TIM2 textures
     * inside ADV resource packs (entry header = u32 size + entry table
     * + payload). Raw TIM2 magic isn't at offset 0 of the AFS entry —
     * it's at one of N inner offsets. So instead of trying to decode
     * each AFS entry as a single TIM2, we SCAN every entry for the
     * 4-byte "TIM2" magic and decode each occurrence as its own slide.
     * Dud entries (audio, msg tables, raw data) just produce zero hits. */
    typedef struct {
        int       afs_part;      /* 0..6 partition index */
        unsigned  afs_idx;       /* entry within partition */
        unsigned  sub_idx;       /* TIM2 index within that entry */
        uint32_t  byte_offset;   /* offset of TIM2 magic within entry */
        uint32_t  entry_size;    /* byte size of containing AFS entry */
        int       w, h;
        GLuint    tex;
    } slide_t;

    slide_t* slides     = NULL;
    int      slide_cap  = 0;
    int      slide_n    = 0;
    int      part_first = (part >= 0) ? part : 0;
    int      part_last  = (part >= 0) ? part : 6;

    for (int p = part_first; p <= part_last; ++p) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", gamedata_dir, k_afs_filename[p]);
        recvx_afs_t* a = recvx_afs_open(path);
        if (!a) {
            RX_LOG("gallery", "skip partition %d (%s): not openable", p, k_afs_filename[p]);
            continue;
        }
        unsigned n = recvx_afs_count(a);
        int part_hits = 0;
        for (unsigned i = 0; i < n; ++i) {
            uint32_t sz = recvx_afs_entry_size(a, i);
            if (sz < 64) continue;
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) continue;
            if (recvx_afs_read(a, i, buf) != sz) { free(buf); continue; }

            /* Scan for "TIM2" magic at every 4-byte boundary. ADV resource
             * packs align inner pointers to 4 bytes, and bare TIM2 entries
             * have magic at offset 0 — both covered. */
            unsigned sub = 0;
            for (uint32_t off = 0; off + 0x40 < sz; off += 4) {
                if (buf[off]   != 'T' || buf[off+1] != 'I' ||
                    buf[off+2] != 'M' || buf[off+3] != '2') continue;
                int w = 0, h = 0;
                uint8_t* pix = NULL;
                if (!gallery_tim2_decode(buf + off, sz - off, &w, &h, &pix)) continue;
                if (slide_n == slide_cap) {
                    slide_cap = slide_cap ? slide_cap * 2 : 64;
                    slides = (slide_t*)realloc(slides, slide_cap * sizeof(slide_t));
                }
                slide_t* s = &slides[slide_n++];
                s->afs_part    = p;
                s->afs_idx     = i;
                s->sub_idx     = sub++;
                s->byte_offset = off;
                s->entry_size  = sz;
                s->w           = w;
                s->h           = h;
                s->tex         = gallery_make_tex(pix, w, h);
                part_hits++;
            }
            free(buf);
        }
        RX_LOG("gallery", "%-13s: %u entries, %d TIM2 slides",
               k_afs_filename[p], n, part_hits);
        recvx_afs_close(a);
    }

    RX_LOG("gallery", "total slides: %d", slide_n);
    if (slide_n == 0) {
        RX_LOG("gallery", "no decodable TIM2 textures found");
    }

    int cur = 0;

    /* --- Render loop ---
     * NOTE: don't call backend->pump_events() here — it drains the SDL
     * event queue into recvx_input which we don't read in gallery mode.
     * We poll SDL directly and detect SDL_QUIT ourselves. */
    int quit = 0;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
            if (ev.type == SDL_KEYDOWN && slide_n > 0) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) quit = 1;
                if (k == SDLK_LEFT  || k == SDLK_x || k == SDLK_a)
                    cur = (cur - 1 + slide_n) % slide_n;
                if (k == SDLK_RIGHT || k == SDLK_z || k == SDLK_d)
                    cur = (cur + 1) % slide_n;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                quit = 1;
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN &&
                ev.button.button == SDL_BUTTON_LEFT && slide_n > 0) {
                /* Convert window-coords to logical 1280x720 since the
                 * window may have been resized by the user. */
                int win_w = 0, win_h = 0;
                SDL_Window* w = SDL_GL_GetCurrentWindow();
                if (w) SDL_GetWindowSize(w, &win_w, &win_h);
                if (win_w <= 0) win_w = BG_W;
                if (win_h <= 0) win_h = BG_H;
                int mx = ev.button.x * BG_W / win_w;
                int my = ev.button.y * BG_H / win_h;
                /* Hit-box on the painted ◄ ► arrows. */
                if (mx >= ARROW_L_X && mx <= ARROW_L_X + ARROW_W &&
                    my >= ARROW_L_Y && my <= ARROW_L_Y + ARROW_H) {
                    cur = (cur - 1 + slide_n) % slide_n;
                } else if (mx >= ARROW_R_X && mx <= ARROW_R_X + ARROW_W &&
                           my >= ARROW_R_Y && my <= ARROW_R_Y + ARROW_H) {
                    cur = (cur + 1) % slide_n;
                } else if (mx < BG_W / 2) {
                    /* Half-screen fallback for clicks outside arrow boxes. */
                    cur = (cur - 1 + slide_n) % slide_n;
                } else {
                    cur = (cur + 1) % slide_n;
                }
            }
        }

        backend->begin_frame();

        /* 1280x720 ortho — top-left origin so PNG/text coords are screen
         * pixels. Z range -1..1 covers our flat 2D draws. */
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0.0, (double)BG_W, (double)BG_H, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        /* --- Background --- */
        if (bg_tex) {
            gallery_draw_quad(bg_tex, 0, 0, BG_W, BG_H, 1, 1, 1, 1);
        } else {
            /* Solid blue field as fallback. */
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.07f, 0.10f, 0.30f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0, 0); glVertex2f(BG_W, 0);
            glVertex2f(BG_W, BG_H); glVertex2f(0, BG_H);
            glEnd();
            glEnable(GL_TEXTURE_2D);
        }

        /* --- Texture in blue area, fit-aspect --- */
        if (slide_n > 0) {
            const slide_t* s = &slides[cur];
            float sx = (float)BLUE_W / (float)s->w;
            float sy = (float)BLUE_H / (float)s->h;
            float k  = sx < sy ? sx : sy;
            int rw = (int)(s->w * k);
            int rh = (int)(s->h * k);
            int rx = BLUE_X + (BLUE_W - rw) / 2;
            int ry = BLUE_Y + (BLUE_H - rh) / 2;
            gallery_draw_quad(s->tex, rx, ry, rw, rh, 1, 1, 1, 1);
        }

        /* --- Caption: multi-line, anchored to top-left of black strip --- */
#ifdef RECVX_HAVE_SDL2_TTF
        if (font) {
            char lines[4][256];
            int  nlines = 0;
            if (slide_n > 0) {
                const slide_t* s = &slides[cur];
                snprintf(lines[nlines++], 256,
                         "%d / %d   %s / %04u   sub %u",
                         cur + 1, slide_n,
                         k_afs_filename[s->afs_part], s->afs_idx, s->sub_idx);
                snprintf(lines[nlines++], 256,
                         "size: %dx%d   offset: 0x%X   entry: %u B",
                         s->w, s->h, s->byte_offset, s->entry_size);
                snprintf(lines[nlines++], 256,
                         "path: %s/%s @ entry %u",
                         gamedata_dir, k_afs_filename[s->afs_part], s->afs_idx);
                snprintf(lines[nlines++], 256,
                         "<-/X prev   ->/Z next   ESC quit");
            } else {
                snprintf(lines[nlines++], 256, "(no TIM2 textures found)");
            }
            for (int i = 0; i < nlines; ++i) {
                int tw = 0, th = 0;
                GLuint t = gallery_text_to_tex(font, lines[i], &tw, &th);
                if (t) {
                    gallery_draw_quad(t, LABEL_TEXT_X,
                                      LABEL_TEXT_Y + i * LABEL_LINE_H,
                                      tw, th, 1, 1, 1, 1);
                    glDeleteTextures(1, &t);
                }
            }
        }
#endif

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        backend->end_frame();
    }

    /* --- Cleanup --- */
    for (int i = 0; i < slide_n; ++i) {
        if (slides[i].tex) glDeleteTextures(1, &slides[i].tex);
    }
    free(slides);
    if (bg_tex) glDeleteTextures(1, &bg_tex);
#ifdef RECVX_HAVE_SDL2_TTF
    if (font) TTF_CloseFont(font);
    TTF_Quit();
#endif
#ifdef RECVX_HAVE_SDL2_IMAGE
    IMG_Quit();
#endif
    return 0;
}

#else /* SDL2 / GL not available */

int run_gallery(const recvx_backend* backend, int part,
                const char* gamedata_dir) {
    (void)backend; (void)part; (void)gamedata_dir;
    fprintf(stderr, "gallery: SDL2 + OpenGL required, neither was compiled in.\n");
    return 1;
}

#endif
