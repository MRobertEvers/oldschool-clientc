#ifndef TORIRS_PLATFORM_KIT_OPENGL3_GL_H
#define TORIRS_PLATFORM_KIT_OPENGL3_GL_H

#include <SDL_video.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int TRSPK_GLuint;
typedef int TRSPK_GLint;
typedef unsigned int TRSPK_GLenum;
typedef unsigned char TRSPK_GLboolean;
typedef int TRSPK_GLsizei;
typedef char TRSPK_GLchar;
typedef ptrdiff_t TRSPK_GLsizeiptr;
typedef ptrdiff_t TRSPK_GLintptr;
typedef float TRSPK_GLfloat;
typedef void TRSPK_GLvoid;
typedef uint64_t TRSPK_GLuint64;

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT
#define GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT 0x4000
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif
#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED ((TRSPK_GLuint64)0xFFFFFFFFFFFFFFFFull)
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif

#define TRSPK_GLAPIENTRY

typedef TRSPK_GLuint(TRSPK_GLAPIENTRY* PFNGLCREATESHADERPROC)(TRSPK_GLenum type);
typedef void(TRSPK_GLAPIENTRY* PFNGLSHADERSOURCEPROC)(
    TRSPK_GLuint shader,
    TRSPK_GLsizei count,
    const TRSPK_GLchar* const* string,
    const TRSPK_GLint* length);
typedef void(TRSPK_GLAPIENTRY* PFNGLCOMPILESHADERPROC)(TRSPK_GLuint shader);
typedef void(TRSPK_GLAPIENTRY* PFNGLGETSHADERIVPROC)(
    TRSPK_GLuint shader,
    TRSPK_GLenum pname,
    TRSPK_GLint* params);
typedef void(TRSPK_GLAPIENTRY* PFNGLGETSHADERINFOLOGPROC)(
    TRSPK_GLuint shader,
    TRSPK_GLsizei bufSize,
    TRSPK_GLsizei* length,
    TRSPK_GLchar* infoLog);
typedef void(TRSPK_GLAPIENTRY* PFNGLDELETESHADERPROC)(TRSPK_GLuint shader);

typedef TRSPK_GLuint(TRSPK_GLAPIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void(TRSPK_GLAPIENTRY* PFNGLATTACHSHADERPROC)(TRSPK_GLuint program, TRSPK_GLuint shader);
typedef void(TRSPK_GLAPIENTRY* PFNGLLINKPROGRAMPROC)(TRSPK_GLuint program);
typedef void(TRSPK_GLAPIENTRY* PFNGLGETPROGRAMIVPROC)(
    TRSPK_GLuint program,
    TRSPK_GLenum pname,
    TRSPK_GLint* params);
typedef void(TRSPK_GLAPIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(
    TRSPK_GLuint program,
    TRSPK_GLsizei bufSize,
    TRSPK_GLsizei* length,
    TRSPK_GLchar* infoLog);
typedef void(TRSPK_GLAPIENTRY* PFNGLDELETEPROGRAMPROC)(TRSPK_GLuint program);
typedef void(TRSPK_GLAPIENTRY* PFNGLUSEPROGRAMPROC)(TRSPK_GLuint program);
typedef TRSPK_GLint(TRSPK_GLAPIENTRY* PFNGLGETATTRIBLOCATIONPROC)(
    TRSPK_GLuint program,
    const TRSPK_GLchar* name);
typedef TRSPK_GLint(TRSPK_GLAPIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(
    TRSPK_GLuint program,
    const TRSPK_GLchar* name);
typedef void(TRSPK_GLAPIENTRY* PFNGLBINDATTRIBLOCATIONPROC)(
    TRSPK_GLuint program,
    TRSPK_GLuint index,
    const TRSPK_GLchar* name);

typedef void(TRSPK_GLAPIENTRY* PFNGLGENVERTEXARRAYSPROC)(TRSPK_GLsizei n, TRSPK_GLuint* arrays);
typedef void(TRSPK_GLAPIENTRY* PFNGLDELETEVERTEXARRAYSPROC)(TRSPK_GLsizei n, const TRSPK_GLuint* arrays);
typedef void(TRSPK_GLAPIENTRY* PFNGLBINDVERTEXARRAYPROC)(TRSPK_GLuint array);

typedef void(TRSPK_GLAPIENTRY* PFNGLGENBUFFERSPROC)(TRSPK_GLsizei n, TRSPK_GLuint* buffers);
typedef void(TRSPK_GLAPIENTRY* PFNGLDELETEBUFFERSPROC)(TRSPK_GLsizei n, const TRSPK_GLuint* buffers);
typedef void(TRSPK_GLAPIENTRY* PFNGLBINDBUFFERPROC)(TRSPK_GLenum target, TRSPK_GLuint buffer);
typedef void(TRSPK_GLAPIENTRY* PFNGLBUFFERDATAPROC)(
    TRSPK_GLenum target,
    TRSPK_GLsizeiptr size,
    const TRSPK_GLvoid* data,
    TRSPK_GLenum usage);
typedef void(TRSPK_GLAPIENTRY* PFNGLBUFFERSUBDATAPROC)(
    TRSPK_GLenum target,
    TRSPK_GLintptr offset,
    TRSPK_GLsizeiptr size,
    const TRSPK_GLvoid* data);
typedef void(TRSPK_GLAPIENTRY* PFNGLGETBUFFERPARAMETERIVPROC)(
    TRSPK_GLenum target,
    TRSPK_GLenum pname,
    TRSPK_GLint* params);

typedef void(TRSPK_GLAPIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(TRSPK_GLuint index);
typedef void(TRSPK_GLAPIENTRY* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(TRSPK_GLuint index);
typedef void(TRSPK_GLAPIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(
    TRSPK_GLuint index,
    TRSPK_GLint size,
    TRSPK_GLenum type,
    TRSPK_GLboolean normalized,
    TRSPK_GLsizei stride,
    const TRSPK_GLvoid* pointer);

typedef void(TRSPK_GLAPIENTRY* PFNGLDRAWELEMENTSPROC)(
    TRSPK_GLenum mode,
    TRSPK_GLsizei count,
    TRSPK_GLenum type,
    const TRSPK_GLvoid* indices);

typedef void(TRSPK_GLAPIENTRY* PFNGLVIEWPORTPROC)(
    TRSPK_GLint x,
    TRSPK_GLint y,
    TRSPK_GLsizei width,
    TRSPK_GLsizei height);
typedef void(TRSPK_GLAPIENTRY* PFNGLSCISSORPROC)(
    TRSPK_GLint x,
    TRSPK_GLint y,
    TRSPK_GLsizei width,
    TRSPK_GLsizei height);
typedef void(TRSPK_GLAPIENTRY* PFNGLENABLEPROC)(TRSPK_GLenum cap);
typedef void(TRSPK_GLAPIENTRY* PFNGLDISABLEPROC)(TRSPK_GLenum cap);
typedef unsigned int TRSPK_GLbitfield;
typedef void(TRSPK_GLAPIENTRY* PFNGLDETACHSHADERPROC)(TRSPK_GLuint program, TRSPK_GLuint shader);
typedef TRSPK_GLvoid*(TRSPK_GLAPIENTRY* PFNGLMAPBUFFERPROC)(TRSPK_GLenum target, TRSPK_GLenum access);
typedef TRSPK_GLboolean(TRSPK_GLAPIENTRY* PFNGLUNMAPBUFFERPROC)(TRSPK_GLenum target);
typedef void(TRSPK_GLAPIENTRY* PFNGLBLENDEQUATIONPROC)(TRSPK_GLenum mode);

typedef void(TRSPK_GLAPIENTRY* PFNGLCLEARPROC)(TRSPK_GLbitfield mask);
typedef void(TRSPK_GLAPIENTRY* PFNGLCLEARCOLORPROC)(
    TRSPK_GLfloat red,
    TRSPK_GLfloat green,
    TRSPK_GLfloat blue,
    TRSPK_GLfloat alpha);
typedef void(TRSPK_GLAPIENTRY* PFNGLCLEARDEPTHFPROC)(TRSPK_GLfloat d);

typedef void(TRSPK_GLAPIENTRY* PFNGLBLENDFUNCPROC)(TRSPK_GLenum sfactor, TRSPK_GLenum dfactor);
typedef void(TRSPK_GLAPIENTRY* PFNGLDEPTHFUNCPROC)(TRSPK_GLenum func);
typedef void(TRSPK_GLAPIENTRY* PFNGLDEPTHMASKPROC)(TRSPK_GLboolean flag);
typedef void(TRSPK_GLAPIENTRY* PFNGLCULLFACEPROC)(TRSPK_GLenum mode);
typedef void(TRSPK_GLAPIENTRY* PFNGLFRONTFACEPROC)(TRSPK_GLenum mode);

typedef void(TRSPK_GLAPIENTRY* PFNGLACTIVETEXTUREPROC)(TRSPK_GLenum texture);
typedef void(TRSPK_GLAPIENTRY* PFNGLBINDTEXTUREPROC)(TRSPK_GLenum target, TRSPK_GLuint texture);
typedef void(TRSPK_GLAPIENTRY* PFNGLGENTEXTURESPROC)(TRSPK_GLsizei n, TRSPK_GLuint* textures);
typedef void(TRSPK_GLAPIENTRY* PFNGLDELETETEXTURESPROC)(TRSPK_GLsizei n, const TRSPK_GLuint* textures);
typedef void(TRSPK_GLAPIENTRY* PFNGLTEXIMAGE2DPROC)(
    TRSPK_GLenum target,
    TRSPK_GLint level,
    TRSPK_GLint internalformat,
    TRSPK_GLsizei width,
    TRSPK_GLsizei height,
    TRSPK_GLint border,
    TRSPK_GLenum format,
    TRSPK_GLenum type,
    const TRSPK_GLvoid* pixels);
typedef void(TRSPK_GLAPIENTRY* PFNGLTEXPARAMETERIPROC)(
    TRSPK_GLenum target,
    TRSPK_GLenum pname,
    TRSPK_GLint param);

typedef void(TRSPK_GLAPIENTRY* PFNGLUNIFORM1IPROC)(TRSPK_GLint location, TRSPK_GLint v0);
typedef void(TRSPK_GLAPIENTRY* PFNGLUNIFORM1FPROC)(TRSPK_GLint location, TRSPK_GLfloat v0);
typedef void(TRSPK_GLAPIENTRY* PFNGLUNIFORMMATRIX4FVPROC)(
    TRSPK_GLint location,
    TRSPK_GLsizei count,
    TRSPK_GLboolean transpose,
    const TRSPK_GLfloat* value);

typedef void(TRSPK_GLAPIENTRY* PFNGLBUFFERSTORAGEPROC)(
    TRSPK_GLenum target,
    TRSPK_GLsizeiptr size,
    const TRSPK_GLvoid* data,
    TRSPK_GLbitfield flags);
typedef TRSPK_GLvoid*(TRSPK_GLAPIENTRY* PFNGLMAPBUFFERRANGEPROC)(
    TRSPK_GLenum target,
    TRSPK_GLintptr offset,
    TRSPK_GLsizeiptr length,
    TRSPK_GLbitfield access);
typedef void(TRSPK_GLAPIENTRY* PFNGLBINDBUFFERRANGEPROC)(
    TRSPK_GLenum target,
    TRSPK_GLuint index,
    TRSPK_GLuint buffer,
    TRSPK_GLintptr offset,
    TRSPK_GLsizeiptr size);
typedef TRSPK_GLuint(TRSPK_GLAPIENTRY* PFNGLGETUNIFORMBLOCKINDEXPROC)(
    TRSPK_GLuint program,
    const TRSPK_GLchar* uniformBlockName);
typedef void(TRSPK_GLAPIENTRY* PFNGLUNIFORMBLOCKBINDINGPROC)(
    TRSPK_GLuint program,
    TRSPK_GLuint uniformBlockIndex,
    TRSPK_GLuint uniformBlockBinding);
typedef TRSPK_GLvoid*(TRSPK_GLAPIENTRY* TRSPKPFNGLFENCESYNCPROC)(
    TRSPK_GLenum condition,
    TRSPK_GLbitfield flags);
typedef void(TRSPK_GLAPIENTRY* TRSPKPFNGLDELETESYNCPROC)(TRSPK_GLvoid* sync);
typedef TRSPK_GLenum(TRSPK_GLAPIENTRY* TRSPKPFNGLCLIENTWAITSYNCPROC)(
    TRSPK_GLvoid* sync,
    TRSPK_GLbitfield flags,
    TRSPK_GLuint64 timeout);
typedef void(TRSPK_GLAPIENTRY* PFNGLFLUSHPROC)(void);

extern PFNGLCREATESHADERPROC trspk_glCreateShader;
extern PFNGLSHADERSOURCEPROC trspk_glShaderSource;
extern PFNGLCOMPILESHADERPROC trspk_glCompileShader;
extern PFNGLGETSHADERIVPROC trspk_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC trspk_glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC trspk_glDeleteShader;
extern PFNGLCREATEPROGRAMPROC trspk_glCreateProgram;
extern PFNGLATTACHSHADERPROC trspk_glAttachShader;
extern PFNGLLINKPROGRAMPROC trspk_glLinkProgram;
extern PFNGLGETPROGRAMIVPROC trspk_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC trspk_glGetProgramInfoLog;
extern PFNGLDELETEPROGRAMPROC trspk_glDeleteProgram;
extern PFNGLUSEPROGRAMPROC trspk_glUseProgram;
extern PFNGLGETATTRIBLOCATIONPROC trspk_glGetAttribLocation;
extern PFNGLGETUNIFORMLOCATIONPROC trspk_glGetUniformLocation;
extern PFNGLBINDATTRIBLOCATIONPROC trspk_glBindAttribLocation;
extern PFNGLGENVERTEXARRAYSPROC trspk_glGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC trspk_glDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC trspk_glBindVertexArray;
extern PFNGLGENBUFFERSPROC trspk_glGenBuffers;
extern PFNGLDELETEBUFFERSPROC trspk_glDeleteBuffers;
extern PFNGLBINDBUFFERPROC trspk_glBindBuffer;
extern PFNGLBUFFERDATAPROC trspk_glBufferData;
extern PFNGLBUFFERSUBDATAPROC trspk_glBufferSubData;
extern PFNGLGETBUFFERPARAMETERIVPROC trspk_glGetBufferParameteriv;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC trspk_glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC trspk_glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC trspk_glVertexAttribPointer;
extern PFNGLDRAWELEMENTSPROC trspk_glDrawElements;
extern PFNGLVIEWPORTPROC trspk_glViewport;
extern PFNGLSCISSORPROC trspk_glScissor;
extern PFNGLENABLEPROC trspk_glEnable;
extern PFNGLDISABLEPROC trspk_glDisable;
extern PFNGLDETACHSHADERPROC trspk_glDetachShader;
extern PFNGLMAPBUFFERPROC trspk_glMapBuffer;
extern PFNGLUNMAPBUFFERPROC trspk_glUnmapBuffer;
extern PFNGLBLENDEQUATIONPROC trspk_glBlendEquation;
extern PFNGLCLEARPROC trspk_glClear;
extern PFNGLCLEARCOLORPROC trspk_glClearColor;
extern PFNGLCLEARDEPTHFPROC trspk_glClearDepthf;
extern PFNGLBLENDFUNCPROC trspk_glBlendFunc;
extern PFNGLDEPTHFUNCPROC trspk_glDepthFunc;
extern PFNGLDEPTHMASKPROC trspk_glDepthMask;
extern PFNGLCULLFACEPROC trspk_glCullFace;
extern PFNGLFRONTFACEPROC trspk_glFrontFace;
extern PFNGLACTIVETEXTUREPROC trspk_glActiveTexture;
extern PFNGLBINDTEXTUREPROC trspk_glBindTexture;
extern PFNGLGENTEXTURESPROC trspk_glGenTextures;
extern PFNGLDELETETEXTURESPROC trspk_glDeleteTextures;
extern PFNGLTEXIMAGE2DPROC trspk_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC trspk_glTexParameteri;
extern PFNGLUNIFORM1IPROC trspk_glUniform1i;
extern PFNGLUNIFORM1FPROC trspk_glUniform1f;
extern PFNGLUNIFORMMATRIX4FVPROC trspk_glUniformMatrix4fv;
extern PFNGLBUFFERSTORAGEPROC trspk_glBufferStorage;
extern PFNGLMAPBUFFERRANGEPROC trspk_glMapBufferRange;
extern PFNGLBINDBUFFERRANGEPROC trspk_glBindBufferRange;
extern PFNGLGETUNIFORMBLOCKINDEXPROC trspk_glGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC trspk_glUniformBlockBinding;
extern TRSPKPFNGLFENCESYNCPROC trspk_glFenceSync;
extern TRSPKPFNGLDELETESYNCPROC trspk_glDeleteSync;
extern TRSPKPFNGLCLIENTWAITSYNCPROC trspk_glClientWaitSync;
extern PFNGLFLUSHPROC trspk_glFlush;

bool
trspk_opengl3_load_gl_procs(void);

#endif
