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
 *   - it asks the context seam for TORIPLATFORM_GL_CLIENT_ES2, which on this lane
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

#include "es2/trspk_es2.h"

struct ClientScaleSettings;
struct ToriDraw_Scene;
struct ToriRS_Frame;

/*
 * The handle.
 *
 * Deliberately never defined: it IS a struct TRSPK_Renderer_ES2, and only this lane's
 * .c file knows that. An incomplete type means a caller cannot reach past the
 * lane into the core by accident, and costs nothing -- there is no wrapper
 * object to allocate and no indirection on the way through.
 */
struct ToriPlatformAndroid_Renderer_GLES2;

/** The core, named for this lane. */
struct ToriPlatformAndroid_Renderer_GLES2*
ToriPlatformAndroid_Renderer_GLES2_New(int width, int height);

void
ToriPlatformAndroid_Renderer_GLES2_Free(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer);

/* Two renderers, two entry points: the painter flag brings up the painter
 * one and the -zbuffer flag the depth one, and whichever was chosen owns the
 * handle's Execute, DrawBootBar and RenderFrame from then on. There is no
 * flag and no test below this line. @see 3rd/trspk/es2/trspk_es2.h. */
bool
ToriPlatformAndroid_Renderer_GLES2_PainterInit(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);
bool
ToriPlatformAndroid_Renderer_GLES2_ZBufferInit(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);

void
ToriPlatformAndroid_Renderer_GLES2_SetViewport(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int width,
    int height);

void
ToriPlatformAndroid_Renderer_GLES2_SetInterfaceScaleMode(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int mode);

void
ToriPlatformAndroid_Renderer_GLES2_SetClientScaling(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ClientScaleSettings const* settings);

void
ToriPlatformAndroid_Renderer_GLES2_SetPick(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriPlatformAndroid_Renderer_GLES2_PickHits(
    struct ToriPlatformAndroid_Renderer_GLES2 const* renderer);

void
ToriPlatformAndroid_Renderer_GLES2_PainterExecute(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_RenderCommand const* command);
void
ToriPlatformAndroid_Renderer_GLES2_ZBufferExecute(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriPlatformAndroid_Renderer_GLES2_PainterDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);
void
ToriPlatformAndroid_Renderer_GLES2_ZBufferDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriPlatformAndroid_Renderer_GLES2_PainterRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_Frame* frame);
void
ToriPlatformAndroid_Renderer_GLES2_ZBufferRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_Frame* frame);

bool
ToriPlatformAndroid_Renderer_GLES2_ReadPixels(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
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
struct TRSPK_Renderer_ES2*
ToriPlatformAndroid_Renderer_GLES2_Core(struct ToriPlatformAndroid_Renderer_GLES2* renderer);

/** @see TRSPK_Renderer_ES2_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriPlatformAndroid_Renderer_GLES2_RotmaskSourceChanged(void);

#endif
