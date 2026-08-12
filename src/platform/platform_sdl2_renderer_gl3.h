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

void
ToriRS_GL3_DrawBootBar(struct ToriRS_GL3* gl3, int progress);

struct ToriRS_PickHits const*
ToriRS_GL3_PickHits(struct ToriRS_GL3 const* gl3);

#endif
