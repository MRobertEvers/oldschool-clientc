#ifndef SRC_PLATFORM_PLATFORM_WEB_RENDERER_WEBGL1_H
#define SRC_PLATFORM_PLATFORM_WEB_RENDERER_WEBGL1_H

/*
 * The browser's OpenGL ES 2.0 renderer, on a WebGL1 context.
 *
 * WebGL1 IS OpenGL ES 2.0 -- the same API to the letter, with no extension
 * beyond it -- so the drawing is entirely the shared core's
 * (platform_renderer_es2_*.c), which the Android lane binds the same way
 * through platform_androidarmv7_renderer_opengles2.c. This file is what makes it a WEBGL1
 * renderer rather than an anonymous one:
 *
 *   - it names it. The core logs through the name it is given, so a browser
 *     console says "WebGL1" and not "GLES2", which is a renderer that is not
 *     running here;
 *   - it asks the context seam for TORIPLATFORM_GL_CLIENT_ES2, which in a browser
 *     is a WebGL1 canvas context by name. The lane can also make a WebGL2
 *     one (platform_web_renderer_webgl2_*.c is a separate renderer), so which of
 *     the two this gets is a decision, not a default;
 *   - it is what --webgl1 / --webgl1-zbuffer select, and what Client
 *     Settings offers as "WebGL 1".
 *
 * The ceiling the core is written to is WEB-GL1-001 in
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
struct ToriPlatformWeb_Renderer_WebGL1;

/** The core, named for this lane. */
struct ToriPlatformWeb_Renderer_WebGL1*
ToriPlatformWeb_Renderer_WebGL1_New(int width, int height);

void
ToriPlatformWeb_Renderer_WebGL1_Free(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer);

/* Two renderers, two entry points: the painter flag brings up the painter
 * one and the -zbuffer flag the depth one, and whichever was chosen owns the
 * handle's Execute, DrawBootBar and RenderFrame from then on. There is no
 * flag and no test below this line. @see 3rd/trspk/es2/trspk_es2.h. */
bool
ToriPlatformWeb_Renderer_WebGL1_PainterInit(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);
bool
ToriPlatformWeb_Renderer_WebGL1_ZBufferInit(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene);

void
ToriPlatformWeb_Renderer_WebGL1_SetViewport(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int width,
    int height);

void
ToriPlatformWeb_Renderer_WebGL1_SetInterfaceScaleMode(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int mode);

void
ToriPlatformWeb_Renderer_WebGL1_SetClientScaling(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ClientScaleSettings const* settings);

void
ToriPlatformWeb_Renderer_WebGL1_SetPick(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int mouse_x,
    int mouse_y);

struct ToriRS_PickHits const*
ToriPlatformWeb_Renderer_WebGL1_PickHits(
    struct ToriPlatformWeb_Renderer_WebGL1 const* renderer);

void
ToriPlatformWeb_Renderer_WebGL1_PainterExecute(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command);
void
ToriPlatformWeb_Renderer_WebGL1_ZBufferExecute(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command);

void
ToriPlatformWeb_Renderer_WebGL1_PainterDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption);
void
ToriPlatformWeb_Renderer_WebGL1_ZBufferDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriPlatformWeb_Renderer_WebGL1_PainterRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_Frame* frame);
void
ToriPlatformWeb_Renderer_WebGL1_ZBufferRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_Frame* frame);

bool
ToriPlatformWeb_Renderer_WebGL1_ReadPixels(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int* pixels,
    int width,
    int height);

/** @see TRSPK_Renderer_ES2_RotmaskSourceChanged: process-wide, and safe with no
 *  renderer alive. */
void
ToriPlatformWeb_Renderer_WebGL1_RotmaskSourceChanged(void);

#endif
