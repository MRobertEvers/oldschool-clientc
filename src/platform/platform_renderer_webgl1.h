#ifndef SRC_PLATFORM_PLATFORM_RENDERER_WEBGL1_H
#define SRC_PLATFORM_PLATFORM_RENDERER_WEBGL1_H

/*
 * The browser's OpenGL ES 2.0 renderer, on a WebGL1 context.
 *
 * WebGL1 IS OpenGL ES 2.0 -- the same API to the letter, with no extension
 * beyond it -- so the drawing is entirely the shared core's
 * (platform_renderer_es2_*.c), which the Android lane binds the same way
 * through platform_renderer_gles2.c. This file is what makes it a WEBGL1
 * renderer rather than an anonymous one:
 *
 *   - it names it. The core logs through the name it is given, so a browser
 *     console says "WebGL1" and not "GLES2", which is a renderer that is not
 *     running here;
 *   - it asks the context seam for TORIRS_GL_CLIENT_ES2, which in a browser
 *     is a WebGL1 canvas context by name. The lane can also make a WebGL2
 *     one (platform_renderer_webgl2_*.c is a separate renderer), so which of
 *     the two this gets is a decision, not a default;
 *   - it is what --webgl1 / --webgl1-zbuffer select, and what Client
 *     Settings offers as "WebGL 1".
 *
 * The ceiling the core is written to is WEB-GL1-001 in
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
struct ToriRS_WebGL1;

/** The core, named for this lane. */
struct ToriRS_WebGL1*
ToriRS_WebGL1_New(int width, int height);

void
ToriRS_WebGL1_Free(
    struct ToriRS_WebGL1* renderer);

bool
ToriRS_WebGL1_Init(
    struct ToriRS_WebGL1* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

void
ToriRS_WebGL1_SetViewport(
    struct ToriRS_WebGL1* renderer,
    int width,
    int height);

void
ToriRS_WebGL1_SetInterfaceScaleMode(
    struct ToriRS_WebGL1* renderer,
    int mode);

void
ToriRS_WebGL1_SetClientScaling(
    struct ToriRS_WebGL1* renderer,
    struct ClientScaleSettings const* settings);

void
ToriRS_WebGL1_SetPick(
    struct ToriRS_WebGL1* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriRS_WebGL1_PickHits(
    struct ToriRS_WebGL1 const* renderer);

void
ToriRS_WebGL1_Execute(
    struct ToriRS_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriRS_WebGL1_DrawBootBar(
    struct ToriRS_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_WebGL1_RenderFrame(
    struct ToriRS_WebGL1* renderer,
    struct ToriRS_Frame* frame);

bool
ToriRS_WebGL1_ReadPixels(
    struct ToriRS_WebGL1* renderer,
    int* pixels,
    int width,
    int height);

/** @see ToriRS_ES2_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriRS_WebGL1_RotmaskSourceChanged(void);

#endif
