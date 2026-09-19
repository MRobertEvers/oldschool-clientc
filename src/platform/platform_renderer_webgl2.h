#ifndef SRC_PLATFORM_PLATFORM_RENDERER_WEBGL2_H
#define SRC_PLATFORM_PLATFORM_RENDERER_WEBGL2_H

/*
 * The browser's OpenGL ES 3.0 renderer, on a WebGL2 context.
 *
 * WebGL2 is OpenGL ES 3.0 -- not all of it, but a well-defined subset -- and
 * the shared core (platform_renderer_es3_*.c) is written to that subset
 * precisely so the Android lane can bind the same code through
 * platform_renderer_gles3.c without a single #if. The one place the two
 * genuinely differ is GL_TEXTURE_SWIZZLE_*, which ES 3.0 has and WebGL2 does
 * not; the core does without it, which costs Android nothing.
 *
 * This file is what makes it a WEBGL2 renderer rather than an anonymous one:
 *
 *   - it names it, so a browser console says "WebGL2";
 *   - it asks the context seam for TORIRS_GL_CLIENT_ES3, which in a browser
 *     is a WebGL2 canvas context by name. The lane can also make a WebGL1
 *     one, so which of the two this gets is a decision, not a default;
 *   - it is what --webgl2 / --webgl2-zbuffer select, and what Client
 *     Settings offers as "WebGL 2".
 *
 * WEB-GL2-000 in docs/platform_quirks.md is the contract.
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
struct ToriRS_WebGL2;

/** The core, named for this lane. */
struct ToriRS_WebGL2*
ToriRS_WebGL2_New(int width, int height);

void
ToriRS_WebGL2_Free(
    struct ToriRS_WebGL2* renderer);

bool
ToriRS_WebGL2_Init(
    struct ToriRS_WebGL2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

void
ToriRS_WebGL2_SetViewport(
    struct ToriRS_WebGL2* renderer,
    int width,
    int height);

void
ToriRS_WebGL2_SetInterfaceScaleMode(
    struct ToriRS_WebGL2* renderer,
    int mode);

void
ToriRS_WebGL2_SetClientScaling(
    struct ToriRS_WebGL2* renderer,
    struct ClientScaleSettings const* settings);

void
ToriRS_WebGL2_SetPick(
    struct ToriRS_WebGL2* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriRS_WebGL2_PickHits(
    struct ToriRS_WebGL2 const* renderer);

void
ToriRS_WebGL2_Execute(
    struct ToriRS_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriRS_WebGL2_DrawBootBar(
    struct ToriRS_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_WebGL2_RenderFrame(
    struct ToriRS_WebGL2* renderer,
    struct ToriRS_Frame* frame);

bool
ToriRS_WebGL2_ReadPixels(
    struct ToriRS_WebGL2* renderer,
    int* pixels,
    int width,
    int height);

/** @see ToriRS_ES3_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriRS_WebGL2_RotmaskSourceChanged(void);

#endif
