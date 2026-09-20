#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES3_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES3_H

/*
 * Android's OpenGL ES 3.0 renderer, on an EGL context.
 *
 * The same core the browser runs as WebGL2 (platform_renderer_es3_*.c),
 * bound to EGL instead of a canvas. It is worth having on this lane because
 * the phone this client targets can run it: a Moto X gen 1 is an Adreno 320,
 * which is an OpenGL ES 3.0 part -- ES 2.0 was forced on the BROWSER by
 * Chrome blacklisting the Krait drivers for WebGL2, not by the hardware.
 *
 * What that buys is what ES 3.0 buys anywhere: 32-bit indices, so the
 * retained world is indexed where it was baked instead of copied into a ring
 * every frame; vertex array objects, so a stream change is one call; a
 * uniform block for the world matrix. See 3rd/trspk/es3/es3_core.h.
 *
 * It is NOT the default here and should not become one on measurement
 * nobody has taken: --gles2 remains what this lane starts with, and whether
 * ES 3.0 is faster on a 2013 Adreno is a question for the device, not for
 * this comment.
 *
 *   - it names it, so a logcat line says "GLES3";
 *   - it asks the context seam for TORIPLATFORM_GL_CLIENT_ES3, which here is an
 *     EGL context at client version 3 from an EGL_OPENGL_ES3_BIT config
 *     (platform_android_gl.c);
 *   - it is what --gles3 / --gles3-zbuffer select, and what Client Settings
 *     offers as "OpenGL ES 3". Those two flags choose between two whole
 *     renderers, not between two modes of one.
 */

#include "es3/trspk_es3.h"

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
struct ToriPlatformAndroid_Renderer_GLES3;

/** The core, named for this lane. */
struct ToriPlatformAndroid_Renderer_GLES3*
ToriPlatformAndroid_Renderer_GLES3_New(int width, int height);

void
ToriPlatformAndroid_Renderer_GLES3_Free(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer);

/* Two renderers, two entry points: --gles3 brings up the painter one and
 * --gles3-zbuffer the depth one, and whichever was chosen owns the handle's
 * Execute, DrawBootBar and RenderFrame from then on. There is no flag and
 * no test below this line. @see 3rd/trspk/es3/trspk_es3.h. */
bool
ToriPlatformAndroid_Renderer_GLES3_PainterInit(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);
bool
ToriPlatformAndroid_Renderer_GLES3_ZBufferInit(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);

void
ToriPlatformAndroid_Renderer_GLES3_SetViewport(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int width,
    int height);

void
ToriPlatformAndroid_Renderer_GLES3_SetInterfaceScaleMode(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int mode);

void
ToriPlatformAndroid_Renderer_GLES3_SetClientScaling(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ClientScaleSettings const* settings);

void
ToriPlatformAndroid_Renderer_GLES3_SetPick(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriPlatformAndroid_Renderer_GLES3_PickHits(
    struct ToriPlatformAndroid_Renderer_GLES3 const* renderer);

void
ToriPlatformAndroid_Renderer_GLES3_PainterExecute(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_RenderCommand const* command);
void
ToriPlatformAndroid_Renderer_GLES3_ZBufferExecute(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriPlatformAndroid_Renderer_GLES3_PainterDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption);
void
ToriPlatformAndroid_Renderer_GLES3_ZBufferDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriPlatformAndroid_Renderer_GLES3_PainterRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_Frame* frame);
void
ToriPlatformAndroid_Renderer_GLES3_ZBufferRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_Frame* frame);

bool
ToriPlatformAndroid_Renderer_GLES3_ReadPixels(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int* pixels,
    int width,
    int height);

/** @see TRSPK_Renderer_ES3_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriPlatformAndroid_Renderer_GLES3_RotmaskSourceChanged(void);

#endif
