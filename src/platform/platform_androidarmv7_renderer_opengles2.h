#ifndef SRC_PLATFORM_PLATFORM_ANDROIDARMV7_RENDERER_OPENGLES2_H
#define SRC_PLATFORM_PLATFORM_ANDROIDARMV7_RENDERER_OPENGLES2_H

/*
 * Android's OpenGL ES 2.0 renderer, on an EGL context.
 *
 * The drawing is the shared core's (platform_renderer_es2_*.c), which the
 * browser binds the same way through platform_web_renderer_webgl1.c. This file
 * is what makes it ANDROID's renderer rather than an anonymous one:
 *
 *   - it names it, so a logcat line says "GLES2" and not "WebGL1";
 *   - it asks the context seam for TORIRS_GL_CLIENT_ES2, which on this lane
 *     is an EGL context at client version 2 from an EGL_OPENGL_ES2_BIT
 *     config (platform_android_gl.c);
 *   - it is what --gles2 / --gles2-zbuffer select, and what Client Settings
 *     offers as "OpenGL ES 2". The dual-core lane
 *     (platform_androidarmv7_renderer_opengles2_dualcore.c, --gles2-dualcore) wraps this one
 *     and is Android-only for the same reason this file is.
 *
 * The ceiling the core is written to is ANDROID-GLES2-001 in
 * docs/platform_quirks.md. Nothing lane-specific belongs in the core; when
 * something turns out to be, it belongs here.
 */

#include "platform/platform_renderer_es2.h"

struct ClientScaleSettings;
struct ToriDraw_Scene;
struct ToriRS_Frame;

/*
 * The handle.
 *
 * Deliberately never defined: it IS a struct ToriRS_ES2, and only this lane's
 * .c file knows that. An incomplete type means a caller cannot reach past the
 * lane into the core by accident, and costs nothing -- there is no wrapper
 * object to allocate and no indirection on the way through.
 */
struct ToriRS_GLES2;

/** The core, named for this lane. */
struct ToriRS_GLES2*
ToriRS_GLES2_New(int width, int height);

void
ToriRS_GLES2_Free(
    struct ToriRS_GLES2* renderer);

bool
ToriRS_GLES2_Init(
    struct ToriRS_GLES2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

void
ToriRS_GLES2_SetViewport(
    struct ToriRS_GLES2* renderer,
    int width,
    int height);

void
ToriRS_GLES2_SetInterfaceScaleMode(
    struct ToriRS_GLES2* renderer,
    int mode);

void
ToriRS_GLES2_SetClientScaling(
    struct ToriRS_GLES2* renderer,
    struct ClientScaleSettings const* settings);

void
ToriRS_GLES2_SetPick(
    struct ToriRS_GLES2* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriRS_GLES2_PickHits(
    struct ToriRS_GLES2 const* renderer);

void
ToriRS_GLES2_Execute(
    struct ToriRS_GLES2* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriRS_GLES2_DrawBootBar(
    struct ToriRS_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_GLES2_RenderFrame(
    struct ToriRS_GLES2* renderer,
    struct ToriRS_Frame* frame);

bool
ToriRS_GLES2_ReadPixels(
    struct ToriRS_GLES2* renderer,
    int* pixels,
    int width,
    int height);

/**
 * The shared core behind this lane's handle.
 *
 * For the dual-core lane (platform_androidarmv7_renderer_opengles2_dualcore.c) and nothing
 * else. That lane is Android-only -- it is the second Krait core, which no
 * browser has -- and it works by installing a model-stage source ON THE CORE,
 * so it needs the core and not the lane's name for it. Anything else reaching
 * for this is reaching past the lane, which is what the opaque handle is
 * meant to prevent.
 */
struct ToriRS_ES2*
ToriRS_GLES2_Core(struct ToriRS_GLES2* renderer);

/** @see ToriRS_ES2_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriRS_GLES2_RotmaskSourceChanged(void);

#endif
