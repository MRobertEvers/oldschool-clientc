#ifndef SRC_PLATFORM_PLATFORM_RENDERER_ES3_H
#define SRC_PLATFORM_PLATFORM_RENDERER_ES3_H

/*
 * The WebGL2 GPU renderer: OpenGL ES 3.0 core, no extensions.
 *
 * The browser lane's second GPU renderer, beside the WebGL1 one, and a
 * SEPARATE renderer from it -- its own translation units, its own type, its
 * own flags (--webgl2 / --webgl2-zbuffer), its own entry in
 * ToriRS_RendererKind. Nothing is switched by preprocessor between them and
 * neither calls the other; a browser that cannot give a WebGL2 context runs
 * --webgl1 exactly as before.
 *
 * ## Why a second renderer rather than a mode of the first
 *
 * The WebGL1 renderer (platform_androidarmv7_renderer_opengles2_*.c) is shared with Android,
 * where the target is a 2013 phone, and it is written to OpenGL ES 2.0 core
 * with no extension at all. Most of what that costs is not a missing feature
 * but a missing INDEX: a 16-bit element cannot reach past 65,536 vertices
 * from wherever the attributes are bound, so the retained world has to be
 * addressed in pages, a draw ends every time one is crossed, and the painter
 * path cannot index the retained world at all -- it copies every drawn model
 * into a GPU ring first, with placement serials, an overwrite guard and a
 * fragmentation compaction to make that ring work.
 *
 * Making that conditional inside one renderer would mean two shapes of the
 * scene bake, the draw sequence, the painter and the depth path living in
 * one set of files under runtime tests. The parts that are genuinely shared
 * -- the bake pipeline, the vertex layout, the atlas, the pose tables, every
 * TRSPK helper -- already are shared, in 3rd/trspk and src/render. What
 * differs is the GL layer, and that is what this renderer is.
 *
 * ## What it is
 *
 * The retained model is still the D3D9 renderer's, because that is the model
 * that answers the question a browser asks -- it pays a JavaScript call per
 * GL entry point, so the frame's cost is the number of calls:
 *
 *   - geometry is BAKED ONCE and never rebuilt per frame (Batch16 for the
 *     scene, an arena for everything else), and drawn WHERE IT WAS BAKED;
 *   - the per-frame cost of the static world is an index stream and nothing
 *     else -- six bytes a face, no vertex traffic -- and on the depth path
 *     most of it does not even pay that: a pose whose faces are all opaque is
 *     a contiguous run of triangles drawn as an array range;
 *   - textures are resolved at bake into a single atlas, so the world pass
 *     binds one texture and the index stream is never split by texture;
 *   - the UI is a retained sprite atlas, single-channel font atlases and one
 *     streamed vertex ring;
 *   - each vertex layout owns a vertex array object, so changing stream is
 *     one call instead of four.
 *
 * platform_renderer_es3_core.h lists what ES 3.0 is used for, feature by
 * feature, against what the ES2 renderer has to do instead.
 *
 * The public surface is the same shape as the D3D9 and GLES2 renderers'
 * (platform_win32_renderer_d3d9.h, platform_androidarmv7_renderer_opengles2.h), so main.c
 * drives every GPU renderer the same way. The context comes from the neutral
 * seam in platform_gl_context.h, which on this lane is
 * platform_gl_context_sdl.c over emscripten's EGL emulation; the WebGL
 * version SDL asks the browser for is decided in platform_sdl2.c.
 */

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include "platform/platform_gl_context.h"

#include <stdbool.h>

struct ClientScaleSettings;
struct ToriDraw_Scene;
struct ToriRS_Frame;
struct ToriRS_ES3;

#define TORIRS_ES3_BG 0xFF202428

/**
 * Make the core.
 *
 * `name` is what this renderer calls itself in a log line, and it belongs to
 * the LANE: the same code is "WebGL2" in a browser and "GLES3" on a phone.
 * Stored, not copied, so every caller passes a literal.
 */
struct ToriRS_ES3*
ToriRS_ES3_New(int width, int height, char const* name);

/** What the lane named this renderer. Never NULL. */
char const*
ToriRS_ES3_Name(struct ToriRS_ES3 const* renderer);

void
ToriRS_ES3_Free(struct ToriRS_ES3* renderer);

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
ToriRS_ES3_Init(
    struct ToriRS_ES3* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

/** Point the renderer at a new canvas size. Only the letterbox and the 2D
 *  projection depend on it; nothing is reallocated. */
void
ToriRS_ES3_SetViewport(
    struct ToriRS_ES3* renderer,
    int width,
    int height);

void
ToriRS_ES3_SetInterfaceScaleMode(
    struct ToriRS_ES3* renderer,
    int mode);

/**
 * The client scaling settings (platform/client_scale.h), copied. When they
 * make the render size differ from the output size, frames are drawn into an
 * offscreen buffer and sampled onto the output rect with the output filter.
 */
void
ToriRS_ES3_SetClientScaling(
    struct ToriRS_ES3* renderer,
    struct ClientScaleSettings const* settings);

void
ToriRS_ES3_SetPick(struct ToriRS_ES3* renderer, int mouse_x, int mouse_y);

struct ToriRS_PickHits const*
ToriRS_ES3_PickHits(struct ToriRS_ES3 const* renderer);

void
ToriRS_ES3_Execute(
    struct ToriRS_ES3* renderer,
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
ToriRS_ES3_DrawBootBar(
    struct ToriRS_ES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_ES3_RenderFrame(struct ToriRS_ES3* renderer, struct ToriRS_Frame* frame);

/**
 * The pixels behind a rotated-masked sprite (the minimap bake) were rewritten
 * IN PLACE. The renderer keeps a GPU copy of that sprite and, since an
 * in-place rewrite raises no scene event, it cannot tell a changed frame
 * from an untouched one -- it used to hash the whole bake every eighth
 * frame to find out. The producer knows; it calls this. Process-wide (the
 * caller holds no renderer) and safe to call with no renderer alive.
 */
void
ToriRS_ES3_RotmaskSourceChanged(void);

/**
 * Read the frame back off the device into `pixels`, top-down ARGB, sampled
 * onto the CANVAS grid (width/height are the canvas size, not the surface's).
 *
 * Call it before the swap: the default framebuffer holds the finished frame
 * right up to the swap and is undefined after it. A frame drawn offscreen
 * (client scaling's render size differs from the output) is read from that
 * buffer instead. Only the letterbox rectangle is read, not the bars around
 * it. A pipeline stall by nature; the app asks only when a capture is
 * actually pending.
 */
bool
ToriRS_ES3_ReadPixels(
    struct ToriRS_ES3* renderer,
    int* pixels,
    int width,
    int height);

#endif
