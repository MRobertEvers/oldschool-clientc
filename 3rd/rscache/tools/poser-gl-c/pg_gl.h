#ifndef POSER_GL_C_PG_GL_H
#define POSER_GL_C_PG_GL_H

/*
 * A hand-rolled OpenGL 3.3 core loader.
 *
 * Every entry point is fetched through SDL_GL_GetProcAddress rather than linked,
 * and no system GL header is included at all. That is deliberate: the platforms
 * this repo targets disagree about which functions the GL library exports
 * directly (macOS exports the lot, Linux exports GL 1.1 and leaves the rest to
 * the extension mechanism), and a header that guesses wrong fails at link time
 * on the other machine. Declaring the handful we use costs one table.
 */

#include <stddef.h>
#include <stdint.h>

typedef unsigned int PG_GLenum;
typedef unsigned char PG_GLboolean;
typedef unsigned int PG_GLbitfield;
typedef signed char PG_GLbyte;
typedef short PG_GLshort;
typedef int PG_GLint;
typedef int PG_GLsizei;
typedef unsigned char PG_GLubyte;
typedef unsigned short PG_GLushort;
typedef unsigned int PG_GLuint;
typedef float PG_GLfloat;
typedef double PG_GLdouble;
typedef char PG_GLchar;
typedef ptrdiff_t PG_GLintptr;
typedef ptrdiff_t PG_GLsizeiptr;

/* ---- constants (only what the editor uses) ------------------------------ */

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
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
#define GL_LINE_SMOOTH 0x0B20
#define GL_LINE_SMOOTH_HINT 0x0C52
#define GL_NICEST 0x1102
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_MULTISAMPLE 0x809D
#define GL_R8 0x8229

/* ---- entry points -------------------------------------------------------- */

/*
 * X-macro over every GL function the editor calls. Adding a call means adding a
 * line here and nowhere else; forgetting to leaves a null pointer that crashes on
 * first use rather than failing to build, which is why pg_gl_load reports the
 * name of anything it could not resolve.
 */
#define PG_GL_FUNCTIONS(X)                                                                        \
    X(void, Clear, (PG_GLbitfield mask))                                                          \
    X(void, ClearColor, (PG_GLfloat r, PG_GLfloat g, PG_GLfloat b, PG_GLfloat a))                 \
    X(void, Viewport, (PG_GLint x, PG_GLint y, PG_GLsizei w, PG_GLsizei h))                       \
    X(void, Scissor, (PG_GLint x, PG_GLint y, PG_GLsizei w, PG_GLsizei h))                        \
    X(void, Enable, (PG_GLenum cap))                                                              \
    X(void, Disable, (PG_GLenum cap))                                                             \
    X(void, DrawArrays, (PG_GLenum mode, PG_GLint first, PG_GLsizei count))                       \
    X(void, PolygonMode, (PG_GLenum face, PG_GLenum mode))                                        \
    X(void, CullFace, (PG_GLenum mode))                                                           \
    X(void, BlendFunc, (PG_GLenum sfactor, PG_GLenum dfactor))                                    \
    X(void, DepthFunc, (PG_GLenum func))                                                          \
    X(void, Hint, (PG_GLenum target, PG_GLenum mode))                                             \
    X(void, LineWidth, (PG_GLfloat width))                                                        \
    X(void, PixelStorei, (PG_GLenum pname, PG_GLint param))                                       \
    X(PG_GLenum, GetError, (void))                                                                \
    X(const PG_GLubyte*, GetString, (PG_GLenum name))                                             \
    X(void, ReadPixels,                                                                           \
      (PG_GLint x,                                                                                \
       PG_GLint y,                                                                                \
       PG_GLsizei w,                                                                              \
       PG_GLsizei h,                                                                              \
       PG_GLenum fmt,                                                                             \
       PG_GLenum type,                                                                            \
       void* pixels))                                                                             \
    X(PG_GLuint, CreateShader, (PG_GLenum type))                                                  \
    X(void, ShaderSource,                                                                         \
      (PG_GLuint shader, PG_GLsizei count, const PG_GLchar* const* str, const PG_GLint* len))     \
    X(void, CompileShader, (PG_GLuint shader))                                                    \
    X(void, GetShaderiv, (PG_GLuint shader, PG_GLenum pname, PG_GLint* params))                   \
    X(void, GetShaderInfoLog,                                                                     \
      (PG_GLuint shader, PG_GLsizei bufsize, PG_GLsizei* length, PG_GLchar* log))                 \
    X(void, DeleteShader, (PG_GLuint shader))                                                     \
    X(PG_GLuint, CreateProgram, (void))                                                           \
    X(void, AttachShader, (PG_GLuint program, PG_GLuint shader))                                  \
    X(void, DetachShader, (PG_GLuint program, PG_GLuint shader))                                  \
    X(void, LinkProgram, (PG_GLuint program))                                                     \
    X(void, ValidateProgram, (PG_GLuint program))                                                 \
    X(void, GetProgramiv, (PG_GLuint program, PG_GLenum pname, PG_GLint* params))                 \
    X(void, GetProgramInfoLog,                                                                    \
      (PG_GLuint program, PG_GLsizei bufsize, PG_GLsizei* length, PG_GLchar* log))                \
    X(void, UseProgram, (PG_GLuint program))                                                      \
    X(void, DeleteProgram, (PG_GLuint program))                                                   \
    X(void, BindAttribLocation, (PG_GLuint program, PG_GLuint index, const PG_GLchar* name))      \
    X(PG_GLint, GetUniformLocation, (PG_GLuint program, const PG_GLchar* name))                   \
    X(void, Uniform1f, (PG_GLint location, PG_GLfloat v0))                                        \
    X(void, Uniform1i, (PG_GLint location, PG_GLint v0))                                          \
    X(void, Uniform2f, (PG_GLint location, PG_GLfloat v0, PG_GLfloat v1))                         \
    X(void, Uniform3f, (PG_GLint location, PG_GLfloat v0, PG_GLfloat v1, PG_GLfloat v2))          \
    X(void, Uniform4f,                                                                            \
      (PG_GLint location, PG_GLfloat v0, PG_GLfloat v1, PG_GLfloat v2, PG_GLfloat v3))            \
    X(void, UniformMatrix4fv,                                                                     \
      (PG_GLint location, PG_GLsizei count, PG_GLboolean transpose, const PG_GLfloat* value))     \
    X(void, GenVertexArrays, (PG_GLsizei n, PG_GLuint* arrays))                                   \
    X(void, BindVertexArray, (PG_GLuint array))                                                   \
    X(void, DeleteVertexArrays, (PG_GLsizei n, const PG_GLuint* arrays))                          \
    X(void, GenBuffers, (PG_GLsizei n, PG_GLuint* buffers))                                       \
    X(void, BindBuffer, (PG_GLenum target, PG_GLuint buffer))                                     \
    X(void, BufferData,                                                                           \
      (PG_GLenum target, PG_GLsizeiptr size, const void* data, PG_GLenum usage))                  \
    X(void, DeleteBuffers, (PG_GLsizei n, const PG_GLuint* buffers))                              \
    X(void, VertexAttribPointer,                                                                  \
      (PG_GLuint index,                                                                           \
       PG_GLint size,                                                                             \
       PG_GLenum type,                                                                            \
       PG_GLboolean normalized,                                                                   \
       PG_GLsizei stride,                                                                         \
       const void* pointer))                                                                      \
    X(void, VertexAttribIPointer,                                                                 \
      (PG_GLuint index,                                                                           \
       PG_GLint size,                                                                             \
       PG_GLenum type,                                                                            \
       PG_GLsizei stride,                                                                         \
       const void* pointer))                                                                      \
    X(void, EnableVertexAttribArray, (PG_GLuint index))                                           \
    X(void, DisableVertexAttribArray, (PG_GLuint index))                                          \
    X(void, GenTextures, (PG_GLsizei n, PG_GLuint* textures))                                     \
    X(void, BindTexture, (PG_GLenum target, PG_GLuint texture))                                   \
    X(void, DeleteTextures, (PG_GLsizei n, const PG_GLuint* textures))                            \
    X(void, ActiveTexture, (PG_GLenum texture))                                                   \
    X(void, TexParameteri, (PG_GLenum target, PG_GLenum pname, PG_GLint param))                   \
    X(void, TexImage2D,                                                                           \
      (PG_GLenum target,                                                                          \
       PG_GLint level,                                                                            \
       PG_GLint internalformat,                                                                   \
       PG_GLsizei width,                                                                          \
       PG_GLsizei height,                                                                          \
       PG_GLint border,                                                                           \
       PG_GLenum format,                                                                          \
       PG_GLenum type,                                                                            \
       const void* pixels))                                                                       \
    X(void, GenFramebuffers, (PG_GLsizei n, PG_GLuint* framebuffers))                             \
    X(void, BindFramebuffer, (PG_GLenum target, PG_GLuint framebuffer))                           \
    X(void, DeleteFramebuffers, (PG_GLsizei n, const PG_GLuint* framebuffers))                    \
    X(void, FramebufferTexture2D,                                                                 \
      (PG_GLenum target,                                                                          \
       PG_GLenum attachment,                                                                      \
       PG_GLenum textarget,                                                                       \
       PG_GLuint texture,                                                                         \
       PG_GLint level))                                                                           \
    X(void, GenRenderbuffers, (PG_GLsizei n, PG_GLuint* renderbuffers))                           \
    X(void, BindRenderbuffer, (PG_GLenum target, PG_GLuint renderbuffer))                         \
    X(void, DeleteRenderbuffers, (PG_GLsizei n, const PG_GLuint* renderbuffers))                  \
    X(void, RenderbufferStorage,                                                                  \
      (PG_GLenum target, PG_GLenum internalformat, PG_GLsizei width, PG_GLsizei height))          \
    X(void, FramebufferRenderbuffer,                                                              \
      (PG_GLenum target,                                                                          \
       PG_GLenum attachment,                                                                      \
       PG_GLenum renderbuffertarget,                                                              \
       PG_GLuint renderbuffer))                                                                   \
    X(PG_GLenum, CheckFramebufferStatus, (PG_GLenum target))                                      \
    X(void, DrawBuffer, (PG_GLenum buf))

#define PG_GL_DECLARE(ret, name, args) extern ret(*pg_gl##name) args;
PG_GL_FUNCTIONS(PG_GL_DECLARE)
#undef PG_GL_DECLARE

/**
 * Resolve every entry point above. Returns 0 on success, otherwise the count of
 * functions that could not be resolved (each named on stderr).
 */
int
pg_gl_load(void);

#endif
