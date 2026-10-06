/*
 * GPU renderer of the GS (OpenGL 3.3 core).
 *
 * gs.c still reads every GS packet and keeps GS memory; with gs_gpu_on set
 * it hands each primitive to gs_gpu_prim instead of drawing it in software.
 * Here primitives are gathered while their settings stay the same and drawn
 * by the graphics card into textures that stand for the frame and depth
 * buffers of GS memory ("targets"), at scale times the PS2's resolution.
 * One shader does what the GS pixel pipeline does (texture functions, fog,
 * alpha test, and the blending that OpenGL's own cannot do), with integer
 * maths like the software GS, so at scale 1 the pictures match.
 *
 * GS memory and the targets are kept coherent page by page (8 KB, as in
 * gs_mem.h): each page is either up to date in GS memory, or newest in the
 * target that drew it last (owner[]). Before the CPU reads or writes a page
 * (image transfers, CLUTs, textures decoded from memory), gs.c calls
 * gs_gpu_sync, which copies the page back from its target. Before a target
 * is drawn or read, the pages it holds that changed in memory since are
 * copied into it (sync_target). A texture whose memory is a target drawn in
 * the same format (a picture drawn, then read back as a texture: shadows,
 * blur, fades) is read straight from the target, at full resolution.
 */
#include "gs_gpu.h"

#include "gs_gl.h"
#include "gs_mem.h"
#include "gs_priv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GS_GL_DEFINE(name, ret, args) PFN_##name p_##name;
GS_GL_FUNCTIONS(GS_GL_DEFINE)
#undef GS_GL_DEFINE

int gs_gpu_on;
static int scale = 1;
static int force_exact; /* CVX_GPU_EXACT: blending always by the shader (slower; for checking it) */

/* ---- Shaders --------------------------------------------------------------- */

static const char draw_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"   /* x, y in GS pixels, z */
    "layout(location = 1) in vec4 a_color;\n" /* 0..255 */
    "layout(location = 2) in vec4 a_tex;\n"   /* U, V in 1/16 texel, Q, fog */
    "uniform vec2 u_size;\n"                  /* the target, in GS pixels */
    "out vec4 v_color;\n"
    "flat out vec4 v_flat;\n"
    "out vec3 v_uvq;\n"
    "out float v_fog;\n"
    "out float v_z;\n"
    "void main() {\n"
    /* GS pixel x is sampled at x; OpenGL samples pixel centres (+0.5). */
    "    gl_Position = vec4((a_pos.xy + 0.5) / u_size * 2.0 - 1.0, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_flat = a_color;\n"
    "    v_uvq = a_tex.xyz;\n"
    "    v_fog = a_tex.w;\n"
    "    v_z = a_pos.z;\n"
    "}\n";

static const char draw_fs[] =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "flat in vec4 v_flat;\n"
    "in vec3 v_uvq;\n"
    "in float v_fog;\n"
    "in float v_z;\n"
    "layout(location = 0, index = 0) out vec4 o_color;\n"
    "layout(location = 0, index = 1) out vec4 o_factor;\n"
    "uniform sampler2D u_tex;\n"   /* the texture (decoded, or a target) */
    "uniform sampler2D u_dst;\n"   /* copy of the target, for u_exact */
    "uniform int u_iip, u_tme, u_tfx, u_tcc, u_fge, u_fba, u_fmt;\n"
    "uniform ivec4 u_fog;\n"
    "uniform int u_texmode;\n"     /* 0 decoded, 1 target CT32, 2 target CT24, 3 target CT16 */
    "uniform ivec2 u_tsize, u_toff, u_wrap;\n"
    "uniform ivec4 u_region, u_texa, u_atest, u_blend;\n"
    "uniform int u_bilinear, u_tscale, u_pre, u_exact, u_fix;\n"
    "uniform ivec4 u_flags;\n"     /* abe, pabe, colclamp, date | datm << 1 */
    "uniform uint u_fbmsk;\n"
    "uniform float u_zscale, u_zmax;\n"
    "\n"
    "int wrapc(int c, int size, int mode, int lo, int hi) {\n"
    "    if (mode == 0) return c & (size - 1);\n"
    "    if (mode == 1) return clamp(c, 0, size - 1);\n"
    "    if (mode == 2) return c < lo ? lo : (c > hi ? hi : c);\n"
    "    return (c & lo) | hi;\n"
    "}\n"
    "ivec4 texel(ivec2 i) {\n"     /* i: in texels of u_tex */
    "    ivec2 size = textureSize(u_tex, 0);\n"
    "    ivec4 c = ivec4(texelFetch(u_tex, clamp(i, ivec2(0), size - 1), 0) * 255.0 + 0.5);\n"
    "    bool black = c.r + c.g + c.b == 0;\n"
    "    if (u_texmode == 2) c.a = (u_texa.y != 0 && black) ? 0 : u_texa.x;\n"
    "    if (u_texmode == 3) c.a = c.a >= 128 ? u_texa.z : ((u_texa.y != 0 && black) ? 0 : u_texa.x);\n"
    "    return c;\n"
    "}\n"
    /* i in 1/u_tscale texel: wrapped in texels, then moved to the texture's
     * place in its target. */
    "ivec4 fetch(ivec2 i) {\n"
    "    ivec2 g = ivec2(floor(vec2(i) / float(u_tscale)));\n"
    "    ivec2 sub = i - g * u_tscale;\n"
    "    ivec2 w = ivec2(wrapc(g.x, u_tsize.x, u_wrap.x, u_region.x, u_region.y),\n"
    "                    wrapc(g.y, u_tsize.y, u_wrap.y, u_region.z, u_region.w)) & 2047;\n"
    "    return texel((w + u_toff) * u_tscale + sub);\n"
    "}\n"
    "ivec4 sample_tex() {\n"
    "    float q = v_uvq.z != 0.0 ? v_uvq.z : 1.0;\n"
    "    vec2 uv = vec2(ivec2(v_uvq.xy / q)) / 16.0;\n" /* 1/16 texel, truncated as on the GS */
    "    float s = float(u_tscale);\n"
    "    if (u_bilinear == 0)\n"
    "        return fetch(ivec2(floor(uv * s + (s - 1.0) * 0.5)));\n"
    "    vec2 us = uv * s + (s - 1.0) * 0.5 - 0.5;\n"
    "    ivec2 i = ivec2(floor(us));\n"
    "    ivec2 f = ivec2(floor((us - vec2(i)) * 16.0));\n"
    "    ivec4 c = fetch(i) * ((16 - f.x) * (16 - f.y)) + fetch(i + ivec2(1, 0)) * (f.x * (16 - f.y)) +\n"
    "              fetch(i + ivec2(0, 1)) * ((16 - f.x) * f.y) + fetch(i + ivec2(1, 1)) * (f.x * f.y);\n"
    "    return c >> 8;\n"
    "}\n"
    "bool compare(int method, int a, int ref) {\n"
    "    if (method == 0) return false;\n"
    "    if (method == 1) return true;\n"
    "    if (method == 2) return a < ref;\n"
    "    if (method == 3) return a <= ref;\n"
    "    if (method == 4) return a == ref;\n"
    "    if (method == 5) return a >= ref;\n"
    "    if (method == 6) return a > ref;\n"
    "    return a != ref;\n"
    "}\n"
    "vec4 to_target(ivec3 rgb, int a) {\n"
    "    if (u_fmt == 2) { rgb &= ~7; a &= 0x80; }\n" /* 16 bits: 5 bits a colour, 1 alpha bit */
    "    return vec4(vec3(rgb), float(a)) / 255.0;\n"
    "}\n"
    "void main() {\n"
    "    ivec4 c = u_iip != 0 ? ivec4(v_color + 0.01) : ivec4(v_flat);\n"
    "    if (u_tme != 0) {\n"
    "        ivec4 t = sample_tex();\n"
    "        if (u_tfx == 0) { c.rgb = (t.rgb * c.rgb) >> 7; if (u_tcc != 0) c.a = (t.a * c.a) >> 7; }\n"
    "        else if (u_tfx == 1) { c.rgb = t.rgb; if (u_tcc != 0) c.a = t.a; }\n"
    "        else if (u_tfx == 2) { c.rgb = ((t.rgb * c.rgb) >> 7) + c.a; if (u_tcc != 0) c.a = t.a + c.a; }\n"
    "        else { c.rgb = ((t.rgb * c.rgb) >> 7) + c.a; if (u_tcc != 0) c.a = t.a; }\n"
    "        c = min(c, 255);\n"
    "    }\n"
    "    if (u_fge != 0) {\n"
    "        int f = int(v_fog + 0.01);\n"
    "        c.rgb = (f * c.rgb + (255 - f) * u_fog.rgb) >> 8;\n"
    "    }\n"
    /* Alpha test; a second pass (u_atest.w) draws the pixels that fail. */
    "    if (u_atest.x != 0 && compare(u_atest.y, c.a, u_atest.z) == (u_atest.w != 0))\n"
    "        discard;\n"
    "    gl_FragDepth = floor(min(v_z, u_zmax) + 0.5) * u_zscale;\n"
    "    int as = c.a, a = (c.a | (u_fba != 0 ? 0x80 : 0)) & 255;\n"
    "    if (u_exact == 0) {\n"
    /* OpenGL blends: the shader only prepares Cs, and gives As / 128. */
    "        int k = u_blend.z == 2 ? u_fix : as;\n"
    "        ivec3 rgb = c.rgb;\n"
    "        if (u_pre == 1) rgb = (rgb * k) >> 7;\n"
    "        else if (u_pre == 2) rgb = rgb + ((rgb * k) >> 7);\n"
    "        else if (u_pre == 3) rgb = rgb - ((rgb * k) >> 7);\n"
    "        o_color = to_target(clamp(rgb, 0, 255), a);\n"
    "        o_factor = vec4(float(as) / 128.0);\n"
    "        return;\n"
    "    }\n"
    /* Everything here, from a copy of the target. */
    "    ivec4 d = ivec4(texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0) * 255.0 + 0.5);\n"
    "    if (u_fmt == 1) d.a |= 0x80;\n"
    "    if ((u_flags.w & 1) != 0 && ((d.a >> 7) & 1) != (u_flags.w >> 1))\n"
    "        discard;\n"
    "    ivec3 rgb = c.rgb;\n"
    "    if (u_flags.x != 0 && !(u_flags.y != 0 && as < 128)) {\n"
    "        int k = u_blend.z == 0 ? as : (u_blend.z == 1 ? d.a : u_fix);\n"
    "        ivec3 cols[3] = ivec3[3](c.rgb, d.rgb, ivec3(0));\n"
    "        rgb = (((cols[u_blend.x] - cols[u_blend.y]) * k) >> 7) + cols[u_blend.w];\n"
    "    }\n"
    "    rgb = u_flags.z != 0 ? clamp(rgb, 0, 255) : (rgb & 255);\n"
    "    uint src = uint(rgb.r) | (uint(rgb.g) << 8) | (uint(rgb.b) << 16) | (uint(a) << 24);\n"
    "    uint dst = uint(d.r) | (uint(d.g) << 8) | (uint(d.b) << 16) | (uint(d.a) << 24);\n"
    "    src = (src & ~u_fbmsk) | (dst & u_fbmsk);\n"
    "    o_color = to_target(ivec3(int(src & 255u), int((src >> 8) & 255u), int((src >> 16) & 255u)),\n"
    "                        int(src >> 24));\n"
    "    o_factor = vec4(1.0);\n"
    "}\n";

static const char blit_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";

static const char blit_fs[] =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_img;\n"
    "void main() { o_color = vec4(texture(u_img, v_uv).rgb, 1.0); }\n";

enum {
    U_SIZE, U_TEX, U_DST, U_IIP, U_TME, U_TFX, U_TCC, U_FGE, U_FBA, U_FMT, U_FOG, U_TEXMODE, U_TSIZE,
    U_TOFF, U_WRAP, U_REGION, U_TEXA, U_ATEST, U_BLEND, U_BILINEAR, U_TSCALE, U_PRE, U_EXACT, U_FIX,
    U_FLAGS, U_FBMSK, U_ZSCALE, U_ZMAX, U_COUNT
};
static const char *const uniform_names[U_COUNT] = {
    "u_size", "u_tex", "u_dst", "u_iip", "u_tme", "u_tfx", "u_tcc", "u_fge", "u_fba", "u_fmt", "u_fog",
    "u_texmode", "u_tsize", "u_toff", "u_wrap", "u_region", "u_texa", "u_atest", "u_blend", "u_bilinear",
    "u_tscale", "u_pre", "u_exact", "u_fix", "u_flags", "u_fbmsk", "u_zscale", "u_zmax",
};

static GLuint draw_prog, blit_prog, blit_img;
static GLint uni[U_COUNT];
static GLuint draw_vao, draw_vbo, blit_vao, blit_vbo;
static int gl_errors;

static void check_gl(const char *where)
{
    GLenum e = glGetError();

    if (e != GL_NO_ERROR && gl_errors < 10) {
        fprintf(stderr, "gpu: OpenGL error 0x%x in %s\n", e, where);
        gl_errors++;
    }
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "gpu: shader error:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(const char *vs, const char *fs, int dual)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs), p;
    GLint ok = 0;

    if (v == 0 || f == 0)
        return 0;
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    if (dual) {
        glBindFragDataLocationIndexed(p, 0, 0, "o_color");
        glBindFragDataLocationIndexed(p, 0, 1, "o_factor");
    }
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "gpu: shader link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ---- Targets ---------------------------------------------------------------
 *
 * A target is a frame buffer (RGBA8 texture) or depth buffer (32-bit float
 * depth texture) of GS memory, at bp (page aligned, as FRAME and ZBUF are)
 * with bw * 64 pixels a row. Colours are stored as their 8-bit values (the
 * 16-bit formats as 5 bits shifted up, alpha 0x80 or 0), depths as z / 2^n
 * for an n-bit Z format, so both copy back to memory exactly.
 */
enum { L32, L16, L16S, LZ32, LZ16, LZ16S };

typedef struct Target {
    int used, depth, layout, psm; /* psm: the depth format of a depth buffer */
    uint32_t bp, bw;
    int w, h;                     /* GS pixels */
    GLuint tex, fbo;
    struct Target *att;           /* depth buffer attached to fbo */
    GLuint att_tex;
    uint64_t last_use;
    uint64_t ver[GS_PAGES];       /* gs_page_gen of each page when it was copied in */
} Target;

#define MAX_TARGETS 24
static Target targets[MAX_TARGETS];
static Target *owner[GS_PAGES]; /* where the newest copy of each page is (NULL: memory) */
static int owned;               /* pages with an owner */
static uint64_t use_clock;

static int layout_of(int psm)
{
    switch (psm) {
    case GS_PSMCT16: return L16;
    case GS_PSMCT16S: return L16S;
    case GS_PSMZ32: case GS_PSMZ24: return LZ32;
    case GS_PSMZ16: return LZ16;
    case GS_PSMZ16S: return LZ16S;
    default: return L32;
    }
}

/* Page shape of a layout, in pixels. */
static int page_w(int layout)
{
    (void)layout;
    return 64;
}

static int page_h(int layout)
{
    return layout == L32 || layout == LZ32 ? 32 : 64;
}

static uint32_t row_pages(const Target *t)
{
    return t->bw ? t->bw : 1;
}

static uint32_t page_of(const Target *t, int cx, int cy)
{
    return (t->bp / 32 + (uint32_t)cy * row_pages(t) + (uint32_t)cx) % GS_PAGES;
}

static void set_owner(uint32_t p, Target *t)
{
    if (owner[p] == NULL && t != NULL)
        owned++;
    else if (owner[p] != NULL && t == NULL)
        owned--;
    owner[p] = t;
}

/* Temporary 1x textures for copies in and out of scaled targets. */
static struct {
    GLuint tex, fbo;
    int w, h, depth;
} tmp[2];

static void make_tex(GLuint tex, int depth, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (depth)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

/* Copies between frame buffers (the drawing's scissor must not clip them). */
static void blit(GLuint from, GLuint to, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1,
                 int depth)
{
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
}

static void attach(GLuint fbo, int depth, GLuint tex)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    if (depth) {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
}

/* A 1x texture of at least w x h, for depth or colour. */
static int tmp_get(int depth, int w, int h)
{
    int i = depth ? 1 : 0;

    if (tmp[i].tex == 0) {
        glGenTextures(1, &tmp[i].tex);
        glGenFramebuffers(1, &tmp[i].fbo);
    }
    if (tmp[i].w < w || tmp[i].h < h) {
        tmp[i].w = tmp[i].w > w ? tmp[i].w : w;
        tmp[i].h = tmp[i].h > h ? tmp[i].h : h;
        make_tex(tmp[i].tex, depth, tmp[i].w, tmp[i].h);
        attach(tmp[i].fbo, depth, tmp[i].tex);
    }
    return i;
}

/* Copies of targets read by the shader while the same target is drawn. */
static struct {
    GLuint tex, fbo;
    int w, h;
} copies[2];

static GLuint copy_of(int which, Target *t, int x0, int y0, int x1, int y1)
{
    int w = t->w * scale, h = t->h * scale;

    if (copies[which].tex == 0) {
        glGenTextures(1, &copies[which].tex);
        glGenFramebuffers(1, &copies[which].fbo);
    }
    if (copies[which].w < w || copies[which].h < h) {
        copies[which].w = copies[which].w > w ? copies[which].w : w;
        copies[which].h = copies[which].h > h ? copies[which].h : h;
        make_tex(copies[which].tex, 0, copies[which].w, copies[which].h);
        attach(copies[which].fbo, 0, copies[which].tex);
    }
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= t->w ? t->w - 1 : x1;
    y1 = y1 >= t->h ? t->h - 1 : y1;
    if (x1 >= x0 && y1 >= y0)
        blit(t->fbo, copies[which].fbo, x0 * scale, y0 * scale, (x1 + 1) * scale, (y1 + 1) * scale, x0 * scale,
             y0 * scale, (x1 + 1) * scale, (y1 + 1) * scale, 0);
    return copies[which].tex;
}

static void batch_flush(void);

static void target_free(Target *t)
{
    for (int p = 0; p < GS_PAGES; p++) {
        if (owner[p] == t)
            set_owner((uint32_t)p, NULL);
    }
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].att == t)
            targets[i].att = NULL;
    }
    glDeleteTextures(1, &t->tex);
    glDeleteFramebuffers(1, &t->fbo);
    memset(t, 0, sizeof(*t));
}

/* Makes the target at least h pixels high, keeping its contents. */
static void target_grow(Target *t, int h)
{
    GLuint tex, fbo;

    h = (h + 127) & ~127;
    if (h > 2048)
        h = 2048;
    if (h <= t->h)
        return;
    batch_flush();
    glGenTextures(1, &tex);
    glGenFramebuffers(1, &fbo);
    make_tex(tex, t->depth, t->w * scale, h * scale);
    attach(fbo, t->depth, tex);
    if (t->tex != 0) {
        blit(t->fbo, fbo, 0, 0, t->w * scale, t->h * scale, 0, 0, t->w * scale, t->h * scale, t->depth);
        glDeleteTextures(1, &t->tex);
        glDeleteFramebuffers(1, &t->fbo);
    }
    t->tex = tex;
    t->fbo = fbo;
    t->att = NULL;
    t->h = h;
    check_gl("target_grow");
}

static void download(Target *t, const uint32_t *pages, int n);

static Target *target_get(int depth, uint32_t bp, uint32_t bw, int psm, int rows)
{
    int layout = layout_of(psm);
    Target *t = NULL, *lru = NULL;

    for (int i = 0; i < MAX_TARGETS; i++) {
        Target *e = &targets[i];
        if (e->used && e->depth == depth && e->bp == bp && e->bw == bw && e->layout == layout &&
            (!depth || e->psm == psm)) {
            t = e;
            break;
        }
        if (lru == NULL || !e->used || (lru->used && e->last_use < lru->last_use))
            lru = e;
    }
    if (t == NULL) {
        t = lru;
        if (t->used) {
            /* Its pages go back to memory first. */
            uint32_t pages[GS_PAGES];
            int n = 0;
            for (int p = 0; p < GS_PAGES; p++) {
                if (owner[p] == t)
                    pages[n++] = (uint32_t)p;
            }
            if (n > 0)
                download(t, pages, n);
            batch_flush();
            target_free(t);
        }
        t->used = 1;
        t->depth = depth;
        t->layout = layout;
        t->psm = psm;
        t->bp = bp;
        t->bw = bw;
        t->w = (bw ? (int)bw : 1) * 64;
        if (t->w > 2048)
            t->w = 2048;
        t->h = 0;
        memset(t->ver, 0xff, sizeof(t->ver)); /* nothing copied in yet */
    }
    target_grow(t, rows < 256 ? 256 : rows);
    t->last_use = ++use_clock;
    return t;
}

/* ---- Copies between GS memory and targets ------------------------------- */

static uint32_t *staging;
static float *staging_z;
static size_t staging_cap;

static void staging_reserve(size_t pixels)
{
    if (pixels <= staging_cap)
        return;
    free(staging);
    free(staging_z);
    staging = malloc(pixels * 4);
    staging_z = malloc(pixels * 4);
    staging_cap = staging != NULL && staging_z != NULL ? pixels : 0;
}

static double z_scale_of(int psm)
{
    return psm == GS_PSMZ16 || psm == GS_PSMZ16S ? 1.0 / 65536.0
         : psm == GS_PSMZ24 ? 1.0 / 16777216.0 : 1.0 / 4294967296.0;
}

/* The pixel (x, y) of t's memory, as stored in the target. */
static uint32_t mem_color(const Target *t, int x, int y)
{
    uint32_t c;

    switch (t->layout) {
    case L16: case L16S: case LZ16: case LZ16S: {
        int k = t->layout == L16 ? 0 : t->layout == L16S ? 1 : t->layout == LZ16 ? 2 : 3;
        c = gs_rd16(gs_addr16(k, t->bp, t->bw, x, y));
        return ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19) |
               ((c & 0x8000) ? 0x80000000u : 0);
    }
    case LZ32:
        return gs_rd32(gs_addr32(1, t->bp, t->bw, x, y));
    default:
        return gs_rd32(gs_addr32(0, t->bp, t->bw, x, y));
    }
}

static void mem_store_color(const Target *t, int x, int y, uint32_t c)
{
    switch (t->layout) {
    case L16: case L16S: case LZ16: case LZ16S: {
        int k = t->layout == L16 ? 0 : t->layout == L16S ? 1 : t->layout == LZ16 ? 2 : 3;
        gs_wr16(gs_addr16(k, t->bp, t->bw, x, y),
                ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c >> 31) << 15));
        return;
    }
    case LZ32:
        gs_wr32(gs_addr32(1, t->bp, t->bw, x, y), c);
        return;
    default:
        gs_wr32(gs_addr32(0, t->bp, t->bw, x, y), c);
        return;
    }
}

static uint32_t mem_z(const Target *t, int x, int y)
{
    switch (t->psm) {
    case GS_PSMZ16: return gs_rd16(gs_addr16(2, t->bp, t->bw, x, y));
    case GS_PSMZ16S: return gs_rd16(gs_addr16(3, t->bp, t->bw, x, y));
    case GS_PSMZ24: return gs_rd32(gs_addr32(1, t->bp, t->bw, x, y)) & 0xffffff;
    default: return gs_rd32(gs_addr32(1, t->bp, t->bw, x, y));
    }
}

static void mem_store_z(const Target *t, int x, int y, uint32_t z)
{
    uint32_t a;

    switch (t->psm) {
    case GS_PSMZ16: gs_wr16(gs_addr16(2, t->bp, t->bw, x, y), z); return;
    case GS_PSMZ16S: gs_wr16(gs_addr16(3, t->bp, t->bw, x, y), z); return;
    case GS_PSMZ24:
        a = gs_addr32(1, t->bp, t->bw, x, y);
        gs_wr32(a, (gs_rd32(a) & 0xff000000u) | (z & 0xffffff));
        return;
    default: gs_wr32(gs_addr32(1, t->bp, t->bw, x, y), z); return;
    }
}

/* Copies the rectangle x, y, w, h of memory into the target. */
static void upload_rect(Target *t, int x, int y, int w, int h)
{
    double zs = z_scale_of(t->psm);

    staging_reserve((size_t)w * h);
    if (staging_cap == 0)
        return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (t->depth)
                staging_z[j * w + i] = (float)(mem_z(t, x + i, y + j) * zs);
            else
                staging[j * w + i] = mem_color(t, x + i, y + j);
        }
    }
    GLenum fmt = t->depth ? GL_DEPTH_COMPONENT : GL_RGBA, type = t->depth ? GL_FLOAT : GL_UNSIGNED_BYTE;
    const void *data = t->depth ? (const void *)staging_z : (const void *)staging;
    if (scale == 1) {
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, fmt, type, data);
    } else {
        int k = tmp_get(t->depth, w, h);
        glBindTexture(GL_TEXTURE_2D, tmp[k].tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, type, data);
        blit(tmp[k].fbo, t->fbo, 0, 0, w, h, x * scale, y * scale, (x + w) * scale, (y + h) * scale, t->depth);
    }
    check_gl("upload");
}

/* Copies the pages of t back to memory; they are then up to date in both. */
static void download(Target *t, const uint32_t *pages, int n)
{
    int pw = page_w(t->layout), ph = page_h(t->layout);
    uint32_t row = row_pages(t);
    int cx0 = 1 << 30, cy0 = 1 << 30, cx1 = -1, cy1 = -1;

    batch_flush();
    /* The rectangle of cells holding those pages. */
    for (int i = 0; i < n; i++) {
        uint32_t idx = (pages[i] + GS_PAGES - t->bp / 32 % GS_PAGES) % GS_PAGES;
        int cx = (int)(idx % row), cy = (int)(idx / row);
        if (cy * ph >= t->h || cx * pw >= t->w)
            continue;
        cx0 = cx < cx0 ? cx : cx0;
        cy0 = cy < cy0 ? cy : cy0;
        cx1 = cx > cx1 ? cx : cx1;
        cy1 = cy > cy1 ? cy : cy1;
    }
    if (cx1 >= 0) {
        int x = cx0 * pw, y = cy0 * ph, w = (cx1 + 1) * pw - x, h = (cy1 + 1) * ph - y;
        GLenum fmt = t->depth ? GL_DEPTH_COMPONENT : GL_RGBA, type = t->depth ? GL_FLOAT : GL_UNSIGNED_BYTE;
        double zs = 1.0 / z_scale_of(t->psm);

        if (x + w > t->w)
            w = t->w - x;
        if (y + h > t->h)
            h = t->h - y;
        staging_reserve((size_t)w * h);
        if (staging_cap != 0) {
            void *data = t->depth ? (void *)staging_z : (void *)staging;
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            if (scale == 1) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
                glReadPixels(x, y, w, h, fmt, type, data);
            } else {
                int k = tmp_get(t->depth, w, h);
                blit(t->fbo, tmp[k].fbo, x * scale, y * scale, (x + w) * scale, (y + h) * scale, 0, 0, w, h,
                     t->depth);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, tmp[k].fbo);
                glReadPixels(0, 0, w, h, fmt, type, data);
            }
            check_gl("download");
            for (int i = 0; i < n; i++) {
                uint32_t idx = (pages[i] + GS_PAGES - t->bp / 32 % GS_PAGES) % GS_PAGES;
                int px = (int)(idx % row) * pw, py = (int)(idx / row) * ph;
                if (owner[pages[i]] != t || py >= t->h || px >= t->w)
                    continue;
                for (int j = py; j < py + ph && j < y + h; j++) {
                    for (int k = px; k < px + pw && k < x + w; k++) {
                        if (t->depth) {
                            double z = floor(staging_z[(j - y) * w + (k - x)] * zs + 0.5);
                            mem_store_z(t, k, j, z >= 4294967295.0 ? 0xffffffffu : (uint32_t)z);
                        } else {
                            mem_store_color(t, k, j, staging[(j - y) * w + (k - x)]);
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (owner[pages[i]] == t) {
            set_owner(pages[i], NULL);
            t->ver[pages[i]] = gs_page_gen[pages[i]];
        }
    }
}

/* Makes GS memory up to date for the pages in mask. */
static void sync_mask(const uint64_t *mask)
{
    static uint32_t pages[GS_PAGES];

    for (int i = 0; i < MAX_TARGETS; i++) {
        Target *t = &targets[i];
        int n = 0;
        if (!t->used)
            continue;
        for (int p = 0; p < GS_PAGES; p++) {
            if (owner[p] == t && (mask[p / 64] >> (p % 64) & 1))
                pages[n++] = (uint32_t)p;
        }
        if (n > 0)
            download(t, pages, n);
    }
}

void gs_gpu_sync(int psm, uint32_t bp, uint32_t bw, int x0, int y0, int x1, int y1)
{
    uint64_t mask[GS_PAGES / 64] = { 0 };

    if (owned == 0)
        return;
    gs_page_mask(psm, bp, bw, x0, y0, x1, y1, mask);
    for (int i = 0; i < GS_PAGES / 64; i++) {
        if (mask[i] != 0) {
            sync_mask(mask);
            return;
        }
    }
}

void gs_gpu_sync_all(void)
{
    uint64_t mask[GS_PAGES / 64];

    if (owned == 0)
        return;
    memset(mask, 0xff, sizeof(mask));
    sync_mask(mask);
}

/* Makes the target up to date for the rectangle x0..x1, y0..y1 (in its
 * pixels): the pages that changed in memory since they were copied in, or
 * that another target drew, are copied in again (whole pages, so a page
 * the target owns is right everywhere). */
static void sync_mask(const uint64_t *mask);

static void sync_target(Target *t, int x0, int y0, int x1, int y1)
{
    int pw = page_w(t->layout), ph = page_h(t->layout), others = 0;
    uint64_t mask[GS_PAGES / 64] = { 0 };

    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= t->w ? t->w - 1 : x1;
    y1 = y1 >= t->h ? t->h - 1 : y1;
    /* Pages newest in other targets go back to memory first, together. */
    for (int cy = y0 / ph; cy <= y1 / ph; cy++) {
        for (int cx = x0 / pw; cx <= x1 / pw; cx++) {
            uint32_t p = page_of(t, cx, cy);
            if (owner[p] != NULL && owner[p] != t && t->ver[p] != gs_page_gen[p]) {
                mask[p / 64] |= 1ull << (p % 64);
                others = 1;
            }
        }
    }
    if (others)
        sync_mask(mask);
    for (int cy = y0 / ph; cy <= y1 / ph; cy++) {
        int run = -1; /* first cell of a run of cells to copy in */
        for (int cx = x0 / pw; cx <= x1 / pw + 1; cx++) {
            int stale = 0;
            if (cx <= x1 / pw) {
                uint32_t p = page_of(t, cx, cy);
                stale = owner[p] != t && t->ver[p] != gs_page_gen[p];
            }
            if (stale && run < 0)
                run = cx;
            if (!stale && run >= 0) {
                upload_rect(t, run * pw, cy * ph, (cx - run) * pw, ph);
                for (int c = run; c < cx; c++) {
                    uint32_t p = page_of(t, c, cy);
                    t->ver[p] = gs_page_gen[p];
                }
                run = -1;
            }
        }
    }
}

/* After drawing to x0..x1, y0..y1 of t: its pages are newest there. */
static void target_drawn(Target *t, int x0, int y0, int x1, int y1)
{
    int pw = page_w(t->layout), ph = page_h(t->layout);
    uint64_t gen = ++gs_gen;

    for (int cy = y0 / ph; cy <= y1 / ph; cy++) {
        for (int cx = x0 / pw; cx <= x1 / pw; cx++) {
            uint32_t p = page_of(t, cx, cy);
            gs_page_gen[p] = gen;
            t->ver[p] = gen;
            set_owner(p, t);
        }
    }
}

/* 1 if t owns a page of that rectangle. */
static int target_owns(const Target *t, int x0, int y0, int x1, int y1)
{
    int pw = page_w(t->layout), ph = page_h(t->layout);

    x1 = x1 >= t->w ? t->w - 1 : x1;
    y1 = y1 >= t->h ? t->h - 1 : y1;
    for (int cy = y0 / ph; cy <= y1 / ph; cy++) {
        for (int cx = x0 / pw; cx <= x1 / pw; cx++) {
            if (owner[page_of(t, cx, cy)] == t)
                return 1;
        }
    }
    return 0;
}

/* ---- Textures decoded from memory ------------------------------------------ */

#define TEX_ENTRIES 256

typedef struct {
    int used;
    uint64_t key, texa, clut;
    uint64_t stamp, last_use;
    int w, h;
    GLuint tex;
} GpuTex;

static GpuTex textures[TEX_ENTRIES];
static int last_tex = -1;
static uint32_t *decoded;
static size_t decoded_cap;

static GLuint batch_uses_tex(GLuint tex);

static GLuint texture_decoded(const DrawState *s)
{
    int indexed = s->tpsm == GS_PSMT8 || s->tpsm == GS_PSMT4 || s->tpsm == GS_PSMT8H ||
                  s->tpsm == GS_PSMT4HL || s->tpsm == GS_PSMT4HH;
    uint64_t key = (uint64_t)s->tbp | (uint64_t)s->tbw << 14 | (uint64_t)s->tpsm << 20 |
                   (uint64_t)s->tw << 26 | (uint64_t)s->th << 40;
    uint64_t texa = s->tpsm == GS_PSMCT32 ? 0 : (uint64_t)s->ta0 | (uint64_t)s->aem << 8 | (uint64_t)s->ta1 << 16;
    uint64_t clut = indexed ? s->clut_hash ^ ((uint64_t)s->cpsm << 56) ^ ((uint64_t)s->csa << 48) : 0;
    GpuTex *t = NULL, *lru = NULL;

    if (last_tex >= 0 && textures[last_tex].used && textures[last_tex].key == key &&
        textures[last_tex].texa == texa && textures[last_tex].clut == clut) {
        t = &textures[last_tex];
    } else {
        for (int i = 0; i < TEX_ENTRIES; i++) {
            GpuTex *e = &textures[i];
            if (e->used && e->key == key && e->texa == texa && e->clut == clut) {
                t = e;
                break;
            }
            if (lru == NULL || !e->used || (lru->used && e->last_use < lru->last_use))
                lru = e;
        }
    }
    if (t == NULL) {
        t = lru;
        if (t->tex == 0)
            glGenTextures(1, &t->tex);
        else if (batch_uses_tex(t->tex))
            batch_flush();
        t->used = 1;
        t->key = key;
        t->texa = texa;
        t->clut = clut;
        t->w = t->h = 0;
        t->stamp = 0;
    } else if (!gs_pages_newer(s->tpsm, s->tbp, s->tbw, 0, 0, s->tw - 1, s->th - 1, t->stamp)) {
        t->last_use = ++use_clock;
        last_tex = (int)(t - textures);
        return t->tex;
    }

    /* Decode it again: what the GPU drew there first goes back to memory. */
    gs_gpu_sync(s->tpsm, s->tbp, s->tbw, 0, 0, s->tw - 1, s->th - 1);
    if (batch_uses_tex(t->tex))
        batch_flush();
    if (decoded_cap < (size_t)s->tw * s->th) {
        free(decoded);
        decoded = malloc((size_t)s->tw * s->th * 4);
        decoded_cap = decoded != NULL ? (size_t)s->tw * s->th : 0;
        if (decoded == NULL)
            return 0;
    }
    gs_decode_texture(s, decoded, s->tw, s->th);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    if (t->w != s->tw || t->h != s->th) {
        make_tex(t->tex, 0, s->tw, s->th);
        t->w = s->tw;
        t->h = s->th;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->tw, s->th, GL_RGBA, GL_UNSIGNED_BYTE, decoded);
    check_gl("texture");
    t->stamp = gs_gen;
    t->last_use = ++use_clock;
    last_tex = (int)(t - textures);
    return t->tex;
}

/* ---- Drawing state ---------------------------------------------------------- */

typedef struct {
    Target *t, *zt;
    GLenum mode;            /* GL_TRIANGLES, GL_LINES, GL_POINTS */
    int scissor[4];         /* x0, y0, x1, y1 in GS pixels */
    /* uniforms */
    int iip, tme, tfx, tcc, fge, fba, fmt;
    int fog[3];
    int texmode, tsize[2], toff[2], wrap[2], region[4], texa[3], atest[3], blend[4];
    int bilinear, tscale, pre, exact, fix, flags[4];
    uint32_t fbmsk;
    float zscale, zmax;
    GLuint tex;
    int feedback;           /* tex is a copy of t, made at each primitive */
    int tex_rect[4];        /* the part of t it reads */
    /* OpenGL state */
    int blend_on;
    GLenum eq, sf, df;
    float fixf;
    int cmask[4];
    int depth_on, zwrite;
    GLenum zfunc;
    int afail;              /* 0: one pass; else what the pixels that fail write (AFAIL) */
} GpuState;

typedef struct {
    float x, y, z, r, g, b, a, u, v, q, f;
} GpuVertex;

static struct {
    GpuState st;
    GpuVertex *v;
    int n, cap;
    int x0, y0, x1, y1;     /* pixels drawn */
} batch;

static GLuint batch_uses_tex(GLuint tex)
{
    return batch.n > 0 && batch.st.tex == tex;
}

/* Fixed blending of OpenGL for (A - B) * C + D, A, B, D: 0 Cs, 1 Cd, 2 zero.
 * Returns 0 when the shader has to do it (u_exact). */
static int choose_blend(GpuState *st, int a, int b, int d)
{
    GLenum f = st->blend[2] == 2 ? GL_CONSTANT_ALPHA : GL_SRC1_ALPHA;
    GLenum nf = st->blend[2] == 2 ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_SRC1_ALPHA;
    static const struct {
        signed char a, b, d, eq, sf, df, pre;
    } table[] = {
        /* eq: 0 add, 1 subtract, 2 reverse subtract; factors: 0 zero,
         * 1 one, 2 f, 3 1 - f; pre: what the shader does to Cs */
        { 0, 1, 1, 0, 2, 3, 0 },  /* Cs f + Cd (1 - f) */
        { 0, 1, 2, 1, 2, 2, 0 },  /* Cs f - Cd f */
        { 0, 2, 0, 0, 1, 0, 2 },  /* Cs (1 + f) */
        { 0, 2, 1, 0, 2, 1, 0 },  /* Cs f + Cd */
        { 0, 2, 2, 0, 1, 0, 1 },  /* Cs f */
        { 1, 0, 0, 0, 3, 2, 0 },  /* Cs (1 - f) + Cd f */
        { 1, 0, 2, 2, 2, 2, 0 },  /* Cd f - Cs f */
        { 1, 2, 0, 0, 1, 2, 0 },  /* Cs + Cd f */
        { 1, 2, 2, 0, 0, 2, 0 },  /* Cd f */
        { 2, 0, 0, 0, 1, 0, 3 },  /* Cs (1 - f) */
        { 2, 0, 1, 2, 2, 1, 0 },  /* Cd - Cs f */
        { 2, 0, 2, 0, 0, 0, 0 },  /* - Cs f: 0 */
        { 2, 1, 0, 1, 1, 2, 0 },  /* Cs - Cd f */
        { 2, 1, 1, 0, 0, 3, 0 },  /* Cd (1 - f) */
        { 2, 1, 2, 0, 0, 0, 0 },  /* - Cd f: 0 */
    };
    const GLenum eqs[3] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT };

    if (a == b) { /* D alone */
        st->eq = GL_FUNC_ADD;
        st->sf = d == 0 ? GL_ONE : GL_ZERO;
        st->df = d == 1 ? GL_ONE : GL_ZERO;
        return 1;
    }
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].a == a && table[i].b == b && table[i].d == d) {
            GLenum fs[4] = { GL_ZERO, GL_ONE, f, nf };
            st->eq = eqs[(int)table[i].eq];
            st->sf = fs[(int)table[i].sf];
            st->df = fs[(int)table[i].df];
            st->pre = table[i].pre;
            return 1;
        }
    }
    return 0;
}

static uint32_t expand_mask16(uint32_t m)
{
    return ((m & 31) << 3) | (((m >> 5) & 31) << 11) | (((m >> 10) & 31) << 19) | ((m & 0x8000) ? 0xff000000u : 0);
}

/* The settings of a primitive. Returns 0 when it draws nothing. */
static int make_state(const DrawState *s, GLenum mode, int rows, GpuState *st)
{
    memset(st, 0, sizeof(*st));
    st->mode = mode;
    st->t = target_get(0, s->fbp, s->fbw, s->fpsm, rows);
    st->fmt = s->fpsm == GS_PSMCT24 || s->fpsm == GS_PSMZ24 ? 1 : (s->fpsm & 2) ? 2 : 0;
    st->scissor[0] = s->scax0;
    st->scissor[1] = s->scay0;
    st->scissor[2] = s->scax1 < st->t->w - 1 ? s->scax1 : st->t->w - 1;
    st->scissor[3] = s->scay1 < st->t->h - 1 ? s->scay1 : st->t->h - 1;
    st->iip = s->iip;
    st->fge = s->fge;
    st->fog[0] = s->fr;
    st->fog[1] = s->fg;
    st->fog[2] = s->fb_;
    st->fba = s->fba;
    st->tscale = 1;

    /* Depth */
    if (s->zte) {
        if (s->ztst == 0)
            return 0;
        st->zt = target_get(1, s->zbp, s->fbw, s->zpsm, st->t->h);
        if (st->zt->h < st->t->h)
            target_grow(st->zt, st->t->h);
        st->depth_on = 1;
        st->zwrite = !s->zmsk;
        st->zfunc = s->ztst == 1 ? GL_ALWAYS : s->ztst == 2 ? GL_GEQUAL : GL_GREATER;
        st->zscale = (float)z_scale_of(s->zpsm);
        st->zmax = (float)s->zmax;
    } else {
        st->zscale = 0.0f;
        st->zmax = 0.0f;
    }

    /* Which channels are written, and whether OpenGL can mask them. */
    uint32_t m = st->fmt == 2 ? expand_mask16(s->fbmsk16) : s->fbmsk;
    if (st->fmt == 1)
        m |= 0xff000000u;
    int partial = 0;
    for (int c = 0; c < 4; c++) {
        uint32_t byte = (m >> (c * 8)) & 255, full = st->fmt == 2 ? (c == 3 ? 0xff : 0xf8) : 0xff;
        st->cmask[c] = (byte & full) != full;
        if ((byte & full) != 0 && (byte & full) != full)
            partial = 1;
    }
    st->fbmsk = m;

    /* Alpha test */
    if (s->ate && s->atst != 1) {
        if (s->atst == 0 && s->afail == 0)
            return 0; /* every pixel fails and keeps nothing */
        st->atest[0] = 1;
        st->atest[1] = s->atst;
        st->atest[2] = s->aref;
        st->afail = s->afail == 0 ? 0 : s->afail;
    }

    /* Blending */
    st->blend[0] = s->sa;
    st->blend[1] = s->sb;
    st->blend[2] = s->sc;
    st->blend[3] = s->sd;
    st->fix = s->fix;
    st->flags[0] = s->abe;
    st->flags[1] = s->pabe;
    st->flags[2] = s->colclamp;
    st->flags[3] = s->date ? 1 | (s->datm << 1) : 0;
    if (s->abe) {
        if (s->sc == 1 || s->pabe || !s->colclamp || !choose_blend(st, s->sa, s->sb, s->sd)) {
            st->exact = 1;
        } else {
            st->blend_on = !(st->sf == GL_ONE && st->df == GL_ZERO && st->eq == GL_FUNC_ADD);
            st->fixf = s->fix > 128 ? 1.0f : s->fix / 128.0f;
        }
    }
    if (s->date || partial || (force_exact && s->abe))
        st->exact = 1;
    if (st->exact) {
        st->blend_on = 0;
        st->pre = 0;
        for (int c = 0; c < 4; c++)
            st->cmask[c] = 1; /* the shader keeps what is masked */
    }

    /* Texture */
    if (s->tme) {
        Target *src = NULL;
        int tl = layout_of(s->tpsm);
        int as_frame = s->tpsm == GS_PSMCT32 || s->tpsm == GS_PSMCT24 || s->tpsm == GS_PSMCT16 ||
                       s->tpsm == GS_PSMCT16S;

        st->tme = 1;
        st->tfx = s->tfx;
        st->tcc = s->tcc;
        st->tsize[0] = s->tw;
        st->tsize[1] = s->th;
        st->wrap[0] = s->wms;
        st->wrap[1] = s->wmt;
        st->region[0] = s->minu;
        st->region[1] = s->maxu;
        st->region[2] = s->minv;
        st->region[3] = s->maxv;
        st->texa[0] = s->ta0;
        st->texa[1] = s->aem;
        st->texa[2] = s->ta1;
        st->bilinear = s->bilinear;

        /* Read from a target drawn in that format? */
        if (as_frame) {
            for (int i = 0; i < MAX_TARGETS && src == NULL; i++) {
                Target *e = &targets[i];
                if (!e->used || e->depth || e->layout != tl || e->bw != s->tbw || s->tbp < e->bp ||
                    (s->tbp - e->bp) % 32 != 0)
                    continue;
                uint32_t k = (s->tbp - e->bp) / 32;
                int ox = (int)(k % row_pages(e)) * page_w(tl), oy = (int)(k / row_pages(e)) * page_h(tl);
                if (oy >= e->h || !target_owns(e, ox, oy, ox + s->tw - 1, oy + s->th - 1))
                    continue;
                src = e;
                st->toff[0] = ox;
                st->toff[1] = oy;
            }
        }
        if (src != NULL) {
            int r[4] = { st->toff[0], st->toff[1], st->toff[0] + s->tw - 1, st->toff[1] + s->th - 1 };
            sync_target(src, r[0], r[1], r[2], r[3]);
            st->texmode = s->tpsm == GS_PSMCT32 ? 1 : s->tpsm == GS_PSMCT24 ? 2 : 3;
            st->tscale = scale;
            st->tex = src->tex;
            if (src == st->t) {
                st->feedback = 1;
                memcpy(st->tex_rect, r, sizeof(r));
            }
        } else {
            st->texmode = 0;
            st->tex = texture_decoded(s);
            if (st->tex == 0)
                return 0;
        }
    }
    return 1;
}

/* ---- Batches ------------------------------------------------------------------ */

static void apply_uniforms(const GpuState *st, int pass)
{
    glUniform2f(uni[U_SIZE], (float)st->t->w, (float)st->t->h);
    glUniform1i(uni[U_TEX], 0);
    glUniform1i(uni[U_DST], 1);
    glUniform1i(uni[U_IIP], st->iip);
    glUniform1i(uni[U_TME], st->tme);
    glUniform1i(uni[U_TFX], st->tfx);
    glUniform1i(uni[U_TCC], st->tcc);
    glUniform1i(uni[U_FGE], st->fge);
    glUniform1i(uni[U_FBA], st->fba);
    glUniform1i(uni[U_FMT], st->fmt);
    glUniform4i(uni[U_FOG], st->fog[0], st->fog[1], st->fog[2], 0);
    glUniform1i(uni[U_TEXMODE], st->texmode);
    glUniform2i(uni[U_TSIZE], st->tsize[0], st->tsize[1]);
    glUniform2i(uni[U_TOFF], st->toff[0], st->toff[1]);
    glUniform2i(uni[U_WRAP], st->wrap[0], st->wrap[1]);
    glUniform4i(uni[U_REGION], st->region[0], st->region[1], st->region[2], st->region[3]);
    glUniform4i(uni[U_TEXA], st->texa[0], st->texa[1], st->texa[2], 0);
    glUniform4i(uni[U_ATEST], st->atest[0], st->atest[1], st->atest[2], pass);
    glUniform4i(uni[U_BLEND], st->blend[0], st->blend[1], st->blend[2], st->blend[3]);
    glUniform1i(uni[U_BILINEAR], st->bilinear);
    glUniform1i(uni[U_TSCALE], st->tscale);
    glUniform1i(uni[U_PRE], st->pre);
    glUniform1i(uni[U_EXACT], st->exact);
    glUniform1i(uni[U_FIX], st->fix);
    glUniform4i(uni[U_FLAGS], st->flags[0], st->flags[1], st->flags[2], st->flags[3]);
    glUniform1ui(uni[U_FBMSK], st->fbmsk);
    glUniform1f(uni[U_ZSCALE], st->zscale);
    glUniform1f(uni[U_ZMAX], st->zmax);
}

static void batch_flush(void)
{
    const GpuState *st = &batch.st;
    Target *t = st->t;

    if (batch.n == 0)
        return;

    /* Textures: a target read while it is drawn, and the target itself for
     * the shader's blending, are read from copies. */
    GLuint dst = st->exact ? copy_of(1, t, batch.x0, batch.y0, batch.x1, batch.y1) : 0;
    GLuint tex = st->feedback ? copy_of(0, t, st->tex_rect[0], st->tex_rect[1], st->tex_rect[2], st->tex_rect[3])
                              : st->tex;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dst);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    if (st->zt != NULL && (t->att != st->zt || t->att_tex != st->zt->tex)) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, st->zt->tex, 0);
        t->att = st->zt;
        t->att_tex = st->zt->tex;
    }
    glViewport(0, 0, t->w * scale, t->h * scale);
    glEnable(GL_SCISSOR_TEST);
    glScissor(st->scissor[0] * scale, st->scissor[1] * scale, (st->scissor[2] - st->scissor[0] + 1) * scale,
              (st->scissor[3] - st->scissor[1] + 1) * scale);

    if (st->depth_on) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(st->zfunc);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (st->blend_on) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(st->eq, GL_FUNC_ADD);
        glBlendFuncSeparate(st->sf, st->df, GL_ONE, GL_ZERO);
        glBlendColor(0.0f, 0.0f, 0.0f, st->fixf);
    } else {
        glDisable(GL_BLEND);
    }

    glUseProgram(draw_prog);
    glBindVertexArray(draw_vao);
    glBindBuffer(GL_ARRAY_BUFFER, draw_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)batch.n * sizeof(GpuVertex), batch.v, GL_STREAM_DRAW);
    if (st->mode == GL_POINTS)
        glPointSize((float)scale);

    /* The pixels that pass the alpha test; then, with an AFAIL other than
     * KEEP, those that fail, with only what AFAIL writes. */
    for (int pass = 0; pass < (st->afail ? 2 : 1); pass++) {
        int rgb = 1, a = 1, z = st->zwrite;
        if (pass == 1) {
            switch (st->afail) {
            case 1: z = 0; break;                 /* FB_ONLY */
            case 2: rgb = a = 0; break;           /* ZB_ONLY */
            default: a = 0; z = 0; break;         /* RGB_ONLY */
            }
        }
        if (st->atest[0] && st->atest[1] == 0 && pass == 0)
            continue; /* NEVER: only the failing pass */
        glColorMask(rgb && st->cmask[0], rgb && st->cmask[1], rgb && st->cmask[2], a && st->cmask[3]);
        glDepthMask((GLboolean)z);
        apply_uniforms(st, pass);
        glDrawArrays(st->mode, 0, batch.n);
    }
    check_gl("draw");
    batch.n = 0;
}

/* GS pixels are drawn when their sample point is on the top or left edge;
 * drawing everything up and left by less than the GS's 1/16 makes OpenGL do
 * the same whatever its own rule (exactly for the edges along rows and
 * columns, which is what matters: sprites, text, menus). The values given
 * with the vertices are those at the moved positions, so that they are
 * still those of the GS at each pixel. */
#define EPS (1.0f / 256.0f)

static void batch_vertex(float x, float y, const float *a)
{
    GpuVertex *g;

    if (batch.n == batch.cap) {
        int cap = batch.cap ? batch.cap * 2 : 4096;
        GpuVertex *nv = realloc(batch.v, (size_t)cap * sizeof(*nv));
        if (nv == NULL)
            return;
        batch.v = nv;
        batch.cap = cap;
    }
    g = &batch.v[batch.n++];
    g->x = x - EPS;
    g->y = y - EPS;
    g->z = a[0];
    g->r = a[1];
    g->g = a[2];
    g->b = a[3];
    g->a = a[4];
    g->u = a[5];
    g->v = a[6];
    g->q = a[7];
    g->f = a[8];
}

/* Texture coordinates of a vertex in 1/16 texel, and Q. */
static void tex_coords(const DrawState *s, const Vertex *p, int divide, float *u, float *v, float *q)
{
    if (s->fst) {
        *u = (float)p->u;
        *v = (float)p->v;
        *q = 1.0f;
    } else {
        float pq = p->q != 0.0f ? p->q : 1.0f;
        *u = p->s * s->tw * 16.0f;
        *v = p->t * s->th * 16.0f;
        *q = pq;
        if (divide) {
            *u /= pq;
            *v /= pq;
            *q = 1.0f;
        }
    }
}

void gs_gpu_prim(const DrawState *s, int type, const Vertex *v)
{
    GpuState st;
    GLenum mode = type == 0 ? GL_POINTS : type <= 2 ? GL_LINES : GL_TRIANGLES;
    int n = type == 0 ? 1 : type <= 2 || type == 6 ? 2 : 3;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);

    /* The pixels it may draw. */
    for (int i = 0; i < n; i++) {
        x0 = v[i].x < x0 ? v[i].x : x0;
        y0 = v[i].y < y0 ? v[i].y : y0;
        x1 = v[i].x > x1 ? v[i].x : x1;
        y1 = v[i].y > y1 ? v[i].y : y1;
    }
    if (type == 6) {
        x0 = (x0 + 15) >> 4; y0 = (y0 + 15) >> 4;
        x1 = ((x1 + 15) >> 4) - 1; y1 = ((y1 + 15) >> 4) - 1;
    } else if (type >= 3) {
        x0 = (x0 + 15) >> 4; y0 = (y0 + 15) >> 4;
        x1 >>= 4; y1 >>= 4;
    } else {
        x0 >>= 4; y0 >>= 4; x1 >>= 4; y1 >>= 4;
    }
    x0 = x0 > s->scax0 ? x0 : s->scax0;
    y0 = y0 > s->scay0 ? y0 : s->scay0;
    x1 = x1 < s->scax1 ? x1 : s->scax1;
    y1 = y1 < s->scay1 ? y1 : s->scay1;
    if (x1 < x0 || y1 < y0 || x0 > 2047 || y0 > 2047)
        return;
    if (type >= 3 && type <= 5 &&
        (int64_t)(v[1].x - v[0].x) * (v[2].y - v[0].y) == (int64_t)(v[2].x - v[0].x) * (v[1].y - v[0].y))
        return; /* no area */

    if (!make_state(s, mode, y1 + 1, &st))
        return;
    x1 = x1 < st.t->w - 1 ? x1 : st.t->w - 1;
    y1 = y1 < st.t->h - 1 ? y1 : st.t->h - 1;
    if (x1 < x0 || y1 < y0)
        return;

    /* Another batch, or a primitive that reads what the batch draws. */
    if (batch.n > 0 && (memcmp(&st, &batch.st, sizeof(st)) != 0 || st.feedback ||
                        (st.exact && x0 <= batch.x1 && batch.x0 <= x1 && y0 <= batch.y1 && batch.y0 <= y1)))
        batch_flush();

    /* What it draws on is up to date. */
    sync_target(st.t, x0, y0, x1, y1);
    if (st.zt != NULL)
        sync_target(st.zt, x0, y0, x1, y1);

    if (batch.n == 0) {
        batch.st = st;
        batch.x0 = x0; batch.y0 = y0; batch.x1 = x1; batch.y1 = y1;
    } else {
        batch.x0 = x0 < batch.x0 ? x0 : batch.x0;
        batch.y0 = y0 < batch.y0 ? y0 : batch.y0;
        batch.x1 = x1 > batch.x1 ? x1 : batch.x1;
        batch.y1 = y1 > batch.y1 ? y1 : batch.y1;
    }

    /* Values at each vertex: z, r, g, b, a, U, V, Q, fog. */
    float a[3][9];
    for (int i = 0; i < n; i++) {
        const Vertex *p = type == 6 ? &v[1] : &v[i]; /* sprites: all but U, V from the second */
        a[i][0] = (float)p->z;
        a[i][1] = (float)p->r;
        a[i][2] = (float)p->g;
        a[i][3] = (float)p->b;
        a[i][4] = (float)p->a;
        tex_coords(s, &v[i], type < 3 || type == 6, &a[i][5], &a[i][6], &a[i][7]);
        a[i][8] = (float)p->f;
    }
    if (type == 6) {
        float ax = v[0].x / 16.0f, ay = v[0].y / 16.0f, bx = v[1].x / 16.0f, by = v[1].y / 16.0f;
        float c[4][9];
        /* U along x, V along y, at the moved corners */
        float du = bx != ax ? (a[1][5] - a[0][5]) / (bx - ax) : 0.0f;
        float dv = by != ay ? (a[1][6] - a[0][6]) / (by - ay) : 0.0f;
        for (int k = 0; k < 4; k++) {
            memcpy(c[k], a[0], sizeof(c[k]));
            c[k][5] = (k & 1 ? a[1][5] : a[0][5]) - EPS * du;
            c[k][6] = (k & 2 ? a[1][6] : a[0][6]) - EPS * dv;
        }
        batch_vertex(ax, ay, c[0]);
        batch_vertex(bx, ay, c[1]);
        batch_vertex(ax, by, c[2]);
        batch_vertex(bx, ay, c[1]);
        batch_vertex(bx, by, c[3]);
        batch_vertex(ax, by, c[2]);
    } else if (type >= 3) {
        /* Gradients per pixel of each value, as the software GS takes them. */
        double dx1 = (v[1].x - v[0].x) / 16.0, dy1 = (v[1].y - v[0].y) / 16.0;
        double dx2 = (v[2].x - v[0].x) / 16.0, dy2 = (v[2].y - v[0].y) / 16.0;
        double det = dx1 * dy2 - dx2 * dy1; /* not 0: checked above */
        float shift[9];
        for (int k = 0; k < 9; k++) {
            double d1 = (double)a[1][k] - a[0][k], d2 = (double)a[2][k] - a[0][k];
            if (!s->iip && k >= 1 && k <= 4) {
                shift[k] = 0.0f; /* flat: the last vertex's colour as it is */
                continue;
            }
            double gx = (d1 * dy2 - d2 * dy1) / det, gy = (d2 * dx1 - d1 * dx2) / det;
            shift[k] = (float)(EPS * (gx + gy));
        }
        for (int i = 0; i < 3; i++) {
            for (int k = 0; k < 9; k++)
                a[i][k] -= shift[k];
            batch_vertex(v[i].x / 16.0f, v[i].y / 16.0f, a[i]);
        }
    } else {
        for (int i = 0; i < n; i++) /* points and lines: pixel centres */
            batch_vertex((float)(v[i].x >> 4) + 2 * EPS, (float)(v[i].y >> 4) + 2 * EPS, a[i]);
    }

    target_drawn(st.t, x0, y0, x1, y1);
    if (st.zt != NULL && st.zwrite)
        target_drawn(st.zt, x0, y0, x1, y1);
    if (st.feedback)
        batch_flush();
}

/* ---- Display -------------------------------------------------------------------- */

static void blit_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    float q[24] = {
        x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1,
        x1, y0, u1, v0, x1, y1, u1, v1, x0, y1, u0, v1,
    };

    glUseProgram(blit_prog);
    glUniform1i(blit_img, 0);
    glBindVertexArray(blit_vao);
    glBindBuffer(GL_ARRAY_BUFFER, blit_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void window_state(int fb_w, int fb_h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColorMask(1, 1, 1, 1);
    glActiveTexture(GL_TEXTURE0);
}

void gs_gpu_present(int fb_w, int fb_h, int x, int y, int w, int h, int smooth)
{
    uint32_t fbp, fbw;
    int psm, dw, dh;
    Target *t;

    batch_flush();
    gs_display_area(&fbp, &fbw, &psm, &dw, &dh);
    t = target_get(0, fbp, fbw, psm, dh);
    sync_target(t, 0, 0, dw - 1, dh - 1);

    window_state(fb_w, fb_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    /* Target rows go up from GS row 0; the window's from its bottom. */
    float x0 = (float)x / fb_w * 2.0f - 1.0f, x1 = (float)(x + w) / fb_w * 2.0f - 1.0f;
    float y0 = 1.0f - (float)y / fb_h * 2.0f, y1 = 1.0f - (float)(y + h) / fb_h * 2.0f;
    blit_quad(x0, y0, x1, y1, 0.0f, 0.0f, (float)dw / t->w, (float)dh / t->h);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check_gl("present");
}

void gs_gpu_draw_picture(const uint32_t *rgba, int pw, int ph, int changed, int fb_w, int fb_h, int x, int y,
                         int w, int h)
{
    static GLuint tex;
    static int tw, th;

    if (tex == 0)
        glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (pw != tw || ph != th) {
        make_tex(tex, 0, pw, ph);
        tw = pw;
        th = ph;
        changed = 1;
    }
    if (changed) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    window_state(fb_w, fb_h);
    float x0 = (float)x / fb_w * 2.0f - 1.0f, x1 = (float)(x + w) / fb_w * 2.0f - 1.0f;
    float y0 = 1.0f - (float)y / fb_h * 2.0f, y1 = 1.0f - (float)(y + h) / fb_h * 2.0f;
    blit_quad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f); /* the picture's rows go down */
    check_gl("picture");
}

/* ---- Start ---------------------------------------------------------------------- */

void gs_gpu_reset(void)
{
    if (!gs_gpu_on)
        return;
    batch.n = 0;
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].used)
            target_free(&targets[i]);
    }
    memset(owner, 0, sizeof(owner));
    owned = 0;
    for (int i = 0; i < TEX_ENTRIES; i++)
        textures[i].used = 0;
    last_tex = -1;
}

int gs_gpu_init(void *(*getproc)(const char *name), int scale_)
{
    const char *version;
    int major = 0, minor = 0;

#define GS_GL_LOAD(name, ret, args)                                    \
    p_##name = (PFN_##name)getproc(#name);                             \
    if (p_##name == NULL) {                                            \
        fprintf(stderr, "gpu: OpenGL function %s missing\n", #name);   \
        return 0;                                                      \
    }
    GS_GL_FUNCTIONS(GS_GL_LOAD)
#undef GS_GL_LOAD

    version = (const char *)glGetString(GL_VERSION);
    if (version == NULL || sscanf(version, "%d.%d", &major, &minor) != 2 || major * 10 + minor < 33) {
        fprintf(stderr, "gpu: OpenGL 3.3 needed, this graphics card has %s\n",
                version != NULL ? version : "none");
        return 0;
    }
    draw_prog = link(draw_vs, draw_fs, 1);
    blit_prog = link(blit_vs, blit_fs, 0);
    if (draw_prog == 0 || blit_prog == 0)
        return 0;
    for (int i = 0; i < U_COUNT; i++)
        uni[i] = glGetUniformLocation(draw_prog, uniform_names[i]);
    blit_img = glGetUniformLocation(blit_prog, "u_img");

    glGenVertexArrays(1, &draw_vao);
    glGenBuffers(1, &draw_vbo);
    glBindVertexArray(draw_vao);
    glBindBuffer(GL_ARRAY_BUFFER, draw_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), (void *)(7 * sizeof(float)));
    glGenVertexArrays(1, &blit_vao);
    glGenBuffers(1, &blit_vbo);
    glBindVertexArray(blit_vao);
    glBindBuffer(GL_ARRAY_BUFFER, blit_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    check_gl("init");

    scale = scale_ < 1 ? 1 : scale_ > 4 ? 4 : scale_;
    force_exact = getenv("CVX_GPU_EXACT") != NULL;
    printf("gpu: %s, OpenGL %s, %dx the PS2 resolution\n", (const char *)glGetString(GL_RENDERER), version,
           scale);
    gs_gpu_on = 1;
    gs_mem_sync = gs_gpu_sync_all;
    gs_mark_all(); /* what memory holds is newer than any target */
    return 1;
}
