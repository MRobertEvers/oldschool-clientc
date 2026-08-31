#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include <SDL.h>
#include <stdbool.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;

#define TORIRS_GL3_BG 0xFF202428

struct ToriRS_GL3;

struct ToriRS_GL3*
ToriRS_GL3_New(int width, int height);

void
ToriRS_GL3_Free(struct ToriRS_GL3* gl3);

/** Point the renderer at a new canvas size (client resize). Only the GL
 *  viewport and the 2D ortho projection depend on it — the atlases, VBO pool
 *  and pose table are size-independent, so nothing is reallocated. No-op when
 *  the size is unchanged. */
void
ToriRS_GL3_SetViewport(
    struct ToriRS_GL3* gl3,
    int width,
    int height);

void
ToriRS_GL3_SetInterfaceScaleMode(
    struct ToriRS_GL3* gl3,
    int mode);

/**
 * Bring up the GL context and the renderer's resources.
 *
 * `z_buffer` selects the depth-buffered world pass instead of the painter one,
 * the same opt-in D3D9 has (`--d3d9-zbuffer`; see WINDOWS-D3D9-ZBUFFER-001).
 * It must be decided here rather than per frame: it changes the pixel format
 * the context is created with, and the caller must also put the app into
 * TORIRS_WORLD_DEPTH so the visible set is collected without the tile wavefront
 * and the opaque face-distance sort.
 */
bool
ToriRS_GL3_Init(
    struct ToriRS_GL3* gl3,
    SDL_Window* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer);

void
ToriRS_GL3_SetPick(struct ToriRS_GL3* gl3, int mouse_x, int mouse_y);

void
ToriRS_GL3_RenderFrame(struct ToriRS_GL3* gl3, struct ToriRS_Frame* frame);

void
ToriRS_GL3_Execute(
    struct ToriRS_GL3* gl3,
    struct ToriRS_RenderCommand const* cmd);

/**
 * The startup progress bar, before there is a frame to build.
 *
 * `progress` < 0 clears without a bar (the post-login loading screen). The
 * caption is drawn either way, so the text-only screen still carries its
 * sentence; pass caption NULL / caption_font_id < 0 for none. Both come from
 * App_BootBarCaption -- the font id is a SCENE font id, resolved out of the
 * scene this renderer was initialised with, so the app must have registered
 * the face before the id means anything here.
 */
void
ToriRS_GL3_DrawBootBar(
    struct ToriRS_GL3* gl3,
    int progress,
    int caption_font_id,
    char const* caption);

struct ToriRS_PickHits const*
ToriRS_GL3_PickHits(struct ToriRS_GL3 const* gl3);

/**
 * Read the presented frame back off the device into `pixels`, top-down ARGB.
 *
 * `width`/`height` are the CANVAS size, not the window's: the drawable is
 * letterboxed and possibly scaled by the display's DPI, so the readback is
 * sampled back down onto the canvas grid the rest of the client thinks in.
 * That is what makes a GPU capture the same size as a software one.
 *
 * Call it BEFORE the buffer swap: it reads GL_BACK, which holds the finished
 * frame right up to the swap and is undefined immediately after one. Returns
 * false when there is no context to read.
 *
 * (RuneLite reads after its swapBuffers, which is the other valid pairing
 * rather than a disagreement -- rlawt's getBufferMode hands it GL_FRONT, the
 * buffer the swap moved the frame into. Mixing the two, back-buffer-after,
 * reads whatever the driver happened to leave behind.)
 *
 * This is a pipeline stall and is meant to be called rarely: the app asks for
 * it only when a capture is actually pending (App_DrawComplete), the same way
 * RuneLite's DrawManager only invokes its supplier when a listener is queued.
 */
bool
ToriRS_GL3_ReadPixels(
    struct ToriRS_GL3* gl3,
    int* pixels,
    int width,
    int height);

#endif
