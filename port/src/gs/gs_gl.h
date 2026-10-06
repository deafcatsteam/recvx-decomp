/*
 * The part of OpenGL 3.3 (core profile) the GPU renderer uses, loaded at run
 * time through the function given to gs_gpu_init: the port links no
 * OpenGL library, so it starts the same on a PC without OpenGL.
 */
#ifndef GS_GL_H
#define GS_GL_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_NONE 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_NEVER 0x0200
#define GL_GREATER 0x0204
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_SRC1_ALPHA 0x8589
#define GL_ONE_MINUS_SRC1_ALPHA 0x88FB
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_DEPTH_COMPONENT 0x1902
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_REPEAT 0x2901
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF

/* name, return type, arguments */
#define GS_GL_FUNCTIONS(F) \
    F(glGetString, const GLubyte *, (GLenum name)) \
    F(glGetError, GLenum, (void)) \
    F(glGetIntegerv, void, (GLenum pname, GLint *data)) \
    F(glEnable, void, (GLenum cap)) \
    F(glDisable, void, (GLenum cap)) \
    F(glViewport, void, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    F(glScissor, void, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    F(glClear, void, (GLbitfield mask)) \
    F(glClearColor, void, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    F(glColorMask, void, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    F(glDepthMask, void, (GLboolean flag)) \
    F(glDepthFunc, void, (GLenum func)) \
    F(glBlendFuncSeparate, void, (GLenum sfrgb, GLenum dfrgb, GLenum sfa, GLenum dfa)) \
    F(glBlendEquationSeparate, void, (GLenum rgb, GLenum alpha)) \
    F(glBlendColor, void, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    F(glPixelStorei, void, (GLenum pname, GLint param)) \
    F(glReadPixels, void, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)) \
    F(glFinish, void, (void)) \
    F(glPointSize, void, (GLfloat size)) \
    F(glDrawBuffer, void, (GLenum buf)) \
    F(glReadBuffer, void, (GLenum buf)) \
    F(glGenTextures, void, (GLsizei n, GLuint *textures)) \
    F(glDeleteTextures, void, (GLsizei n, const GLuint *textures)) \
    F(glBindTexture, void, (GLenum target, GLuint texture)) \
    F(glActiveTexture, void, (GLenum texture)) \
    F(glTexImage2D, void, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, \
                           GLint border, GLenum format, GLenum type, const void *pixels)) \
    F(glTexSubImage2D, void, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, \
                              GLenum format, GLenum type, const void *pixels)) \
    F(glTexParameteri, void, (GLenum target, GLenum pname, GLint param)) \
    F(glGenerateMipmap, void, (GLenum target)) \
    F(glGetFloatv, void, (GLenum pname, GLfloat *data)) \
    F(glGenSamplers, void, (GLsizei n, GLuint *samplers)) \
    F(glBindSampler, void, (GLuint unit, GLuint sampler)) \
    F(glSamplerParameteri, void, (GLuint sampler, GLenum pname, GLint param)) \
    F(glSamplerParameterf, void, (GLuint sampler, GLenum pname, GLfloat param)) \
    F(glGenFramebuffers, void, (GLsizei n, GLuint *fbs)) \
    F(glDeleteFramebuffers, void, (GLsizei n, const GLuint *fbs)) \
    F(glBindFramebuffer, void, (GLenum target, GLuint fb)) \
    F(glFramebufferTexture2D, void, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, \
                                     GLint level)) \
    F(glCheckFramebufferStatus, GLenum, (GLenum target)) \
    F(glBlitFramebuffer, void, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, \
                                GLint dy1, GLbitfield mask, GLenum filter)) \
    F(glGenBuffers, void, (GLsizei n, GLuint *buffers)) \
    F(glDeleteBuffers, void, (GLsizei n, const GLuint *buffers)) \
    F(glBindBuffer, void, (GLenum target, GLuint buffer)) \
    F(glBufferData, void, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    F(glGenVertexArrays, void, (GLsizei n, GLuint *arrays)) \
    F(glDeleteVertexArrays, void, (GLsizei n, const GLuint *arrays)) \
    F(glBindVertexArray, void, (GLuint array)) \
    F(glEnableVertexAttribArray, void, (GLuint index)) \
    F(glVertexAttribPointer, void, (GLuint index, GLint size, GLenum type, GLboolean normalized, \
                                    GLsizei stride, const void *pointer)) \
    F(glCreateShader, GLuint, (GLenum type)) \
    F(glDeleteShader, void, (GLuint shader)) \
    F(glShaderSource, void, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
    F(glCompileShader, void, (GLuint shader)) \
    F(glGetShaderiv, void, (GLuint shader, GLenum pname, GLint *params)) \
    F(glGetShaderInfoLog, void, (GLuint shader, GLsizei size, GLsizei *length, GLchar *log)) \
    F(glCreateProgram, GLuint, (void)) \
    F(glDeleteProgram, void, (GLuint program)) \
    F(glAttachShader, void, (GLuint program, GLuint shader)) \
    F(glBindAttribLocation, void, (GLuint program, GLuint index, const GLchar *name)) \
    F(glBindFragDataLocationIndexed, void, (GLuint program, GLuint color, GLuint index, const GLchar *name)) \
    F(glLinkProgram, void, (GLuint program)) \
    F(glGetProgramiv, void, (GLuint program, GLenum pname, GLint *params)) \
    F(glGetProgramInfoLog, void, (GLuint program, GLsizei size, GLsizei *length, GLchar *log)) \
    F(glUseProgram, void, (GLuint program)) \
    F(glGetUniformLocation, GLint, (GLuint program, const GLchar *name)) \
    F(glUniform1i, void, (GLint location, GLint v0)) \
    F(glUniform2i, void, (GLint location, GLint v0, GLint v1)) \
    F(glUniform4i, void, (GLint location, GLint v0, GLint v1, GLint v2, GLint v3)) \
    F(glUniform1ui, void, (GLint location, GLuint v0)) \
    F(glUniform1f, void, (GLint location, GLfloat v0)) \
    F(glUniform2f, void, (GLint location, GLfloat v0, GLfloat v1)) \
    F(glUniform4f, void, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    F(glUniform1iv, void, (GLint location, GLsizei count, const GLint *value)) \
    F(glDrawArrays, void, (GLenum mode, GLint first, GLsizei count))

#define GS_GL_DECLARE(name, ret, args) typedef ret(GLAPIENTRY *PFN_##name) args; extern PFN_##name p_##name;
GS_GL_FUNCTIONS(GS_GL_DECLARE)
#undef GS_GL_DECLARE

/* The pointers are named p_gl...: an executable exporting its own glEnable
 * could be taken for OpenGL's by the driver. */
#define glGetString p_glGetString
#define glGetError p_glGetError
#define glGetIntegerv p_glGetIntegerv
#define glEnable p_glEnable
#define glDisable p_glDisable
#define glViewport p_glViewport
#define glScissor p_glScissor
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glColorMask p_glColorMask
#define glDepthMask p_glDepthMask
#define glDepthFunc p_glDepthFunc
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glBlendEquationSeparate p_glBlendEquationSeparate
#define glBlendColor p_glBlendColor
#define glPixelStorei p_glPixelStorei
#define glReadPixels p_glReadPixels
#define glFinish p_glFinish
#define glPointSize p_glPointSize
#define glDrawBuffer p_glDrawBuffer
#define glReadBuffer p_glReadBuffer
#define glGenTextures p_glGenTextures
#define glDeleteTextures p_glDeleteTextures
#define glBindTexture p_glBindTexture
#define glActiveTexture p_glActiveTexture
#define glTexImage2D p_glTexImage2D
#define glTexSubImage2D p_glTexSubImage2D
#define glTexParameteri p_glTexParameteri
#define glGenerateMipmap p_glGenerateMipmap
#define glGetFloatv p_glGetFloatv
#define glGenSamplers p_glGenSamplers
#define glBindSampler p_glBindSampler
#define glSamplerParameteri p_glSamplerParameteri
#define glSamplerParameterf p_glSamplerParameterf
#define glGenFramebuffers p_glGenFramebuffers
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glBlitFramebuffer p_glBlitFramebuffer
#define glGenBuffers p_glGenBuffers
#define glDeleteBuffers p_glDeleteBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glGenVertexArrays p_glGenVertexArrays
#define glDeleteVertexArrays p_glDeleteVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glCreateShader p_glCreateShader
#define glDeleteShader p_glDeleteShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glDeleteProgram p_glDeleteProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glBindFragDataLocationIndexed p_glBindFragDataLocationIndexed
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform2i p_glUniform2i
#define glUniform4i p_glUniform4i
#define glUniform1ui p_glUniform1ui
#define glUniform1f p_glUniform1f
#define glUniform2f p_glUniform2f
#define glUniform4f p_glUniform4f
#define glUniform1iv p_glUniform1iv
#define glDrawArrays p_glDrawArrays

#endif
