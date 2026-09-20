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
 * uniform block for the world matrix. See platform_renderer_es3_core.h.
 *
 * It is NOT the default here and should not become one on measurement
 * nobody has taken: --gles2 remains what this lane starts with, and whether
 * ES 3.0 is faster on a 2013 Adreno is a question for the device, not for
 * this comment.
 *
 *   - it names it, so a logcat line says "GLES3";
 *   - it asks the context seam for TORIRS_GL_CLIENT_ES3, which here is an
 *     EGL context at client version 3 from an EGL_OPENGL_ES3_BIT config
 *     (platform_android_gl.c);
 *   - it is what --gles3 / --gles3-zbuffer select, and what Client Settings
 *     offers as "OpenGL ES 3".
 */

#include "platform/platform_renderer_es3.h"

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
struct ToriRS_GLES3;

/** The core, named for this lane. */
struct ToriRS_GLES3*
ToriRS_GLES3_New(int width, int height);

void
ToriRS_GLES3_Free(
    struct ToriRS_GLES3* renderer);

bool
ToriRS_GLES3_Init(
    struct ToriRS_GLES3* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

void
ToriRS_GLES3_SetViewport(
    struct ToriRS_GLES3* renderer,
    int width,
    int height);

void
ToriRS_GLES3_SetInterfaceScaleMode(
    struct ToriRS_GLES3* renderer,
    int mode);

void
ToriRS_GLES3_SetClientScaling(
    struct ToriRS_GLES3* renderer,
    struct ClientScaleSettings const* settings);

void
ToriRS_GLES3_SetPick(
    struct ToriRS_GLES3* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriRS_GLES3_PickHits(
    struct ToriRS_GLES3 const* renderer);

void
ToriRS_GLES3_Execute(
    struct ToriRS_GLES3* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriRS_GLES3_DrawBootBar(
    struct ToriRS_GLES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_GLES3_RenderFrame(
    struct ToriRS_GLES3* renderer,
    struct ToriRS_Frame* frame);

bool
ToriRS_GLES3_ReadPixels(
    struct ToriRS_GLES3* renderer,
    int* pixels,
    int width,
    int height);

/** @see ToriRS_ES3_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriRS_GLES3_RotmaskSourceChanged(void);

#endif
