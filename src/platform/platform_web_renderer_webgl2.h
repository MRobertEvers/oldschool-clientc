#ifndef SRC_PLATFORM_PLATFORM_WEB_RENDERER_WEBGL2_H
#define SRC_PLATFORM_PLATFORM_WEB_RENDERER_WEBGL2_H

/*
 * The browser's OpenGL ES 3.0 renderer, on a WebGL2 context.
 *
 * WebGL2 is OpenGL ES 3.0 -- not all of it, but a well-defined subset -- and
 * the shared core (platform_renderer_es3_*.c) is written to that subset
 * precisely so the Android lane can bind the same code through
 * platform_androidarmv7_renderer_opengles3.c without a single #if. The one place the two
 * genuinely differ is GL_TEXTURE_SWIZZLE_*, which ES 3.0 has and WebGL2 does
 * not; the core does without it, which costs Android nothing.
 *
 * This file is what makes it a WEBGL2 renderer rather than an anonymous one:
 *
 *   - it names it, so a browser console says "WebGL2";
 *   - it asks the context seam for TORIPLATFORM_GL_CLIENT_ES3, which in a browser
 *     is a WebGL2 canvas context by name. The lane can also make a WebGL1
 *     one, so which of the two this gets is a decision, not a default;
 *   - it is what --webgl2 / --webgl2-zbuffer select, and what Client
 *     Settings offers as "WebGL 2".
 *
 * WEB-GL2-000 in docs/platform_quirks.md is the contract.
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
struct ToriPlatformWeb_Renderer_WebGL2;

/** The core, named for this lane. */
struct ToriPlatformWeb_Renderer_WebGL2*
ToriPlatformWeb_Renderer_WebGL2_New(int width, int height);

void
ToriPlatformWeb_Renderer_WebGL2_Free(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer);

/* Two renderers, two entry points: the painter flag brings up the painter
 * one and the -zbuffer flag the depth one, and whichever was chosen owns the
 * handle's Execute, DrawBootBar and RenderFrame from then on. There is no
 * flag and no test below this line. @see 3rd/trspk/es3/trspk_es3.h. */
bool
ToriPlatformWeb_Renderer_WebGL2_PainterInit(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);
bool
ToriPlatformWeb_Renderer_WebGL2_ZBufferInit(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);

void
ToriPlatformWeb_Renderer_WebGL2_SetViewport(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int width,
    int height);

void
ToriPlatformWeb_Renderer_WebGL2_SetInterfaceScaleMode(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int mode);

void
ToriPlatformWeb_Renderer_WebGL2_SetClientScaling(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ClientScaleSettings const* settings);

void
ToriPlatformWeb_Renderer_WebGL2_SetPick(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriPlatformWeb_Renderer_WebGL2_PickHits(
    struct ToriPlatformWeb_Renderer_WebGL2 const* renderer);

void
ToriPlatformWeb_Renderer_WebGL2_PainterExecute(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command);
void
ToriPlatformWeb_Renderer_WebGL2_ZBufferExecute(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriPlatformWeb_Renderer_WebGL2_PainterDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);
void
ToriPlatformWeb_Renderer_WebGL2_ZBufferDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriPlatformWeb_Renderer_WebGL2_PainterRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_Frame* frame);
void
ToriPlatformWeb_Renderer_WebGL2_ZBufferRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_Frame* frame);

bool
ToriPlatformWeb_Renderer_WebGL2_ReadPixels(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int* pixels,
    int width,
    int height);

/** @see TRSPK_Renderer_ES3_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriPlatformWeb_Renderer_WebGL2_RotmaskSourceChanged(void);

#endif
