#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES2_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES2_H

/*
 * The GLES2 GPU renderer: OpenGL ES 2.0, core profile, NO extensions.
 *
 * One renderer for two lanes. Android links it against the NDK's GLES2 over an
 * EGL context (--gles2 / --gles2-zbuffer); the browser links the same four
 * translation units against WebGL1, which is the same API to the letter, over
 * an SDL-made context (--webgl1 / --webgl1-zbuffer). Nothing in these files
 * knows which: every GL call comes from <GLES2/gl2.h>, and the context comes
 * from the neutral seam in platform_gl_context.h.
 *
 * Its own renderer rather than a build of the desktop GL one, and shaped after
 * the Windows D3D9 renderer rather than after it -- because D3D9's retained
 * model is the one that already answers the questions a 2013 phone (or a
 * browser paying a JavaScript call per GL entry point) asks:
 *
 *   - geometry is BAKED ONCE into 16-bit-indexable pages and never rebuilt per
 *     frame (Batch16 for the scene, a paged arena for everything else);
 *   - the per-frame cost is an index stream, and on the depth path most of the
 *     static world does not even pay that: a pose whose faces are all opaque is
 *     a contiguous run of triangles and is drawn straight from the vertex
 *     buffer as an array range, no indices at all;
 *   - textures are resolved at bake into a single atlas, so the fragment shader
 *     is one texture fetch and the draw loop switches texture only for the
 *     handful of scrolling ones;
 *   - the UI is a retained sprite atlas, retained font atlases and one
 *     streamed vertex ring.
 *
 * The public surface is the same shape as the D3D9 renderer's
 * (platform_win32_renderer_d3d9.h), so main.c drives every GPU renderer the
 * same way. The seam's two implementations are platform_android_gl.c (EGL)
 * and platform_gl_context_sdl.c (SDL, which in the browser is emscripten's EGL
 * emulation over a WebGL1 canvas context).
 */

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include "platform/platform_gl_context.h"

#include <stdbool.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;
struct ToriRS_GLES2;

#define TORIRS_GLES2_BG 0xFF202428

struct ToriRS_GLES2*
ToriRS_GLES2_New(int width, int height);

void
ToriRS_GLES2_Free(struct ToriRS_GLES2* renderer);

/**
 * Bring up the GL context and every GPU resource.
 *
 * `z_buffer` selects the depth-buffered world pass over the painter one -- the
 * same opt-in D3D9 has (`--d3d9-zbuffer`). It is decided here because the
 * depth buffer is part of the EGL config the context is created with, and the
 * caller must also put the app into TORIRS_WORLD_DEPTH so the visible set is
 * collected without the tile wavefront and the face-distance sort.
 */
bool
ToriRS_GLES2_Init(
    struct ToriRS_GLES2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

/** Point the renderer at a new canvas size. Only the letterbox and the 2D
 *  projection depend on it; nothing is reallocated. */
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
ToriRS_GLES2_SetPick(struct ToriRS_GLES2* renderer, int mouse_x, int mouse_y);

struct ToriRS_PickHits const*
ToriRS_GLES2_PickHits(struct ToriRS_GLES2 const* renderer);

void
ToriRS_GLES2_Execute(
    struct ToriRS_GLES2* renderer,
    struct ToriRS_RenderCommand const* command);

/**
 * The startup progress bar, before there is a frame to build.
 *
 * `progress` < 0 clears without a bar (the post-login loading screen). The
 * caption is drawn either way; pass caption NULL / caption_font_id < 0 for
 * none. The font id is a SCENE font id, resolved out of the scene this
 * renderer was initialised with.
 */
void
ToriRS_GLES2_DrawBootBar(
    struct ToriRS_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_GLES2_RenderFrame(struct ToriRS_GLES2* renderer, struct ToriRS_Frame* frame);

/**
 * The pixels behind a rotated-masked sprite (the minimap bake) were rewritten
 * IN PLACE. The renderer keeps a GPU copy of that sprite and, since an
 * in-place rewrite raises no scene event, it cannot tell a changed frame
 * from an untouched one -- it used to hash the whole bake every eighth
 * frame to find out. The producer knows; it calls this. Process-wide (the
 * caller holds no renderer) and safe to call with no renderer alive.
 */
void
ToriRS_GLES2_RotmaskSourceChanged(void);

/**
 * Read the frame back off the device into `pixels`, top-down ARGB, sampled
 * onto the CANVAS grid (width/height are the canvas size, not the surface's).
 *
 * Call it before the swap: GLES2 has no glReadBuffer, so this reads whatever
 * the default framebuffer holds, which is the finished frame right up to
 * eglSwapBuffers and undefined after it. A pipeline stall by nature; the app
 * asks only when a capture is actually pending.
 */
bool
ToriRS_GLES2_ReadPixels(
    struct ToriRS_GLES2* renderer,
    int* pixels,
    int width,
    int height);

#endif
