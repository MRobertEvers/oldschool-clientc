#include "opengl3_internal.h"

#include <stdio.h>
#include <stdlib.h>

/* Explicit casts — SDL_GL_GetProcAddress returns void*. */
#define O3L(name, Type, field)                                                         \
    do                                                                                 \
    {                                                                                  \
        void* p##field = SDL_GL_GetProcAddress(#name);                                 \
        if( !p##field )                                                               \
        {                                                                              \
            fprintf(stderr, "OpenGL3: missing proc %s\n", #name);                      \
            return false;                                                              \
        }                                                                              \
        field = (Type)p##field;                                                        \
    } while( 0 )

PFNGLCREATESHADERPROC trspk_glCreateShader;
PFNGLSHADERSOURCEPROC trspk_glShaderSource;
PFNGLCOMPILESHADERPROC trspk_glCompileShader;
PFNGLGETSHADERIVPROC trspk_glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC trspk_glGetShaderInfoLog;
PFNGLDELETESHADERPROC trspk_glDeleteShader;
PFNGLCREATEPROGRAMPROC trspk_glCreateProgram;
PFNGLATTACHSHADERPROC trspk_glAttachShader;
PFNGLLINKPROGRAMPROC trspk_glLinkProgram;
PFNGLGETPROGRAMIVPROC trspk_glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC trspk_glGetProgramInfoLog;
PFNGLDELETEPROGRAMPROC trspk_glDeleteProgram;
PFNGLUSEPROGRAMPROC trspk_glUseProgram;
PFNGLGETATTRIBLOCATIONPROC trspk_glGetAttribLocation;
PFNGLGETUNIFORMLOCATIONPROC trspk_glGetUniformLocation;
PFNGLBINDATTRIBLOCATIONPROC trspk_glBindAttribLocation;
PFNGLGENVERTEXARRAYSPROC trspk_glGenVertexArrays;
PFNGLDELETEVERTEXARRAYSPROC trspk_glDeleteVertexArrays;
PFNGLBINDVERTEXARRAYPROC trspk_glBindVertexArray;
PFNGLGENBUFFERSPROC trspk_glGenBuffers;
PFNGLDELETEBUFFERSPROC trspk_glDeleteBuffers;
PFNGLBINDBUFFERPROC trspk_glBindBuffer;
PFNGLBUFFERDATAPROC trspk_glBufferData;
PFNGLBUFFERSUBDATAPROC trspk_glBufferSubData;
PFNGLGETBUFFERPARAMETERIVPROC trspk_glGetBufferParameteriv;
PFNGLENABLEVERTEXATTRIBARRAYPROC trspk_glEnableVertexAttribArray;
PFNGLDISABLEVERTEXATTRIBARRAYPROC trspk_glDisableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC trspk_glVertexAttribPointer;
PFNGLDRAWELEMENTSPROC trspk_glDrawElements;
PFNGLVIEWPORTPROC trspk_glViewport;
PFNGLSCISSORPROC trspk_glScissor;
PFNGLENABLEPROC trspk_glEnable;
PFNGLDISABLEPROC trspk_glDisable;
PFNGLDETACHSHADERPROC trspk_glDetachShader;
PFNGLMAPBUFFERPROC trspk_glMapBuffer;
PFNGLUNMAPBUFFERPROC trspk_glUnmapBuffer;
PFNGLBLENDEQUATIONPROC trspk_glBlendEquation;
PFNGLCLEARPROC trspk_glClear;
PFNGLCLEARCOLORPROC trspk_glClearColor;
PFNGLCLEARDEPTHFPROC trspk_glClearDepthf;
PFNGLBLENDFUNCPROC trspk_glBlendFunc;
PFNGLDEPTHFUNCPROC trspk_glDepthFunc;
PFNGLDEPTHMASKPROC trspk_glDepthMask;
PFNGLCULLFACEPROC trspk_glCullFace;
PFNGLFRONTFACEPROC trspk_glFrontFace;
PFNGLACTIVETEXTUREPROC trspk_glActiveTexture;
PFNGLBINDTEXTUREPROC trspk_glBindTexture;
PFNGLGENTEXTURESPROC trspk_glGenTextures;
PFNGLDELETETEXTURESPROC trspk_glDeleteTextures;
PFNGLTEXIMAGE2DPROC trspk_glTexImage2D;
PFNGLTEXPARAMETERIPROC trspk_glTexParameteri;
PFNGLUNIFORM1IPROC trspk_glUniform1i;
PFNGLUNIFORM1FPROC trspk_glUniform1f;
PFNGLUNIFORMMATRIX4FVPROC trspk_glUniformMatrix4fv;

bool
trspk_opengl3_load_gl_procs(void)
{
    O3L(glCreateShader, PFNGLCREATESHADERPROC, trspk_glCreateShader);
    O3L(glShaderSource, PFNGLSHADERSOURCEPROC, trspk_glShaderSource);
    O3L(glCompileShader, PFNGLCOMPILESHADERPROC, trspk_glCompileShader);
    O3L(glGetShaderiv, PFNGLGETSHADERIVPROC, trspk_glGetShaderiv);
    O3L(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, trspk_glGetShaderInfoLog);
    O3L(glDeleteShader, PFNGLDELETESHADERPROC, trspk_glDeleteShader);
    O3L(glCreateProgram, PFNGLCREATEPROGRAMPROC, trspk_glCreateProgram);
    O3L(glAttachShader, PFNGLATTACHSHADERPROC, trspk_glAttachShader);
    O3L(glLinkProgram, PFNGLLINKPROGRAMPROC, trspk_glLinkProgram);
    O3L(glGetProgramiv, PFNGLGETPROGRAMIVPROC, trspk_glGetProgramiv);
    O3L(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC, trspk_glGetProgramInfoLog);
    O3L(glDeleteProgram, PFNGLDELETEPROGRAMPROC, trspk_glDeleteProgram);
    O3L(glUseProgram, PFNGLUSEPROGRAMPROC, trspk_glUseProgram);
    O3L(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC, trspk_glGetAttribLocation);
    O3L(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC, trspk_glGetUniformLocation);
    O3L(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC, trspk_glBindAttribLocation);
    O3L(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC, trspk_glGenVertexArrays);
    O3L(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC, trspk_glDeleteVertexArrays);
    O3L(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC, trspk_glBindVertexArray);
    O3L(glGenBuffers, PFNGLGENBUFFERSPROC, trspk_glGenBuffers);
    O3L(glDeleteBuffers, PFNGLDELETEBUFFERSPROC, trspk_glDeleteBuffers);
    O3L(glBindBuffer, PFNGLBINDBUFFERPROC, trspk_glBindBuffer);
    O3L(glBufferData, PFNGLBUFFERDATAPROC, trspk_glBufferData);
    O3L(glBufferSubData, PFNGLBUFFERSUBDATAPROC, trspk_glBufferSubData);
    O3L(glGetBufferParameteriv, PFNGLGETBUFFERPARAMETERIVPROC, trspk_glGetBufferParameteriv);
    O3L(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC, trspk_glEnableVertexAttribArray);
    O3L(
        glDisableVertexAttribArray,
        PFNGLDISABLEVERTEXATTRIBARRAYPROC,
        trspk_glDisableVertexAttribArray);
    O3L(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC, trspk_glVertexAttribPointer);
    O3L(glDrawElements, PFNGLDRAWELEMENTSPROC, trspk_glDrawElements);
    O3L(glViewport, PFNGLVIEWPORTPROC, trspk_glViewport);
    O3L(glScissor, PFNGLSCISSORPROC, trspk_glScissor);
    O3L(glEnable, PFNGLENABLEPROC, trspk_glEnable);
    O3L(glDisable, PFNGLDISABLEPROC, trspk_glDisable);
    O3L(glDetachShader, PFNGLDETACHSHADERPROC, trspk_glDetachShader);
    O3L(glMapBuffer, PFNGLMAPBUFFERPROC, trspk_glMapBuffer);
    O3L(glUnmapBuffer, PFNGLUNMAPBUFFERPROC, trspk_glUnmapBuffer);
    O3L(glBlendEquation, PFNGLBLENDEQUATIONPROC, trspk_glBlendEquation);
    O3L(glClear, PFNGLCLEARPROC, trspk_glClear);
    O3L(glClearColor, PFNGLCLEARCOLORPROC, trspk_glClearColor);
    O3L(glClearDepthf, PFNGLCLEARDEPTHFPROC, trspk_glClearDepthf);
    O3L(glBlendFunc, PFNGLBLENDFUNCPROC, trspk_glBlendFunc);
    O3L(glDepthFunc, PFNGLDEPTHFUNCPROC, trspk_glDepthFunc);
    O3L(glDepthMask, PFNGLDEPTHMASKPROC, trspk_glDepthMask);
    O3L(glCullFace, PFNGLCULLFACEPROC, trspk_glCullFace);
    O3L(glFrontFace, PFNGLFRONTFACEPROC, trspk_glFrontFace);
    O3L(glActiveTexture, PFNGLACTIVETEXTUREPROC, trspk_glActiveTexture);
    O3L(glBindTexture, PFNGLBINDTEXTUREPROC, trspk_glBindTexture);
    O3L(glGenTextures, PFNGLGENTEXTURESPROC, trspk_glGenTextures);
    O3L(glDeleteTextures, PFNGLDELETETEXTURESPROC, trspk_glDeleteTextures);
    O3L(glTexImage2D, PFNGLTEXIMAGE2DPROC, trspk_glTexImage2D);
    O3L(glTexParameteri, PFNGLTEXPARAMETERIPROC, trspk_glTexParameteri);
    O3L(glUniform1i, PFNGLUNIFORM1IPROC, trspk_glUniform1i);
    O3L(glUniform1f, PFNGLUNIFORM1FPROC, trspk_glUniform1f);
    O3L(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC, trspk_glUniformMatrix4fv);
    return true;
}
