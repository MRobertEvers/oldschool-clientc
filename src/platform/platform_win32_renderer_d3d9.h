#ifndef SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_H
#define SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;
struct ToriRS_D3D9;

#define TORIRS_D3D9_BG 0xFF202428

#if defined(TORIRS_D3D9_RETAINED_TEST_API)
#define TORIRS_D3D9_RETAINED_GROUP_COUNT 2u

/**
 * Read-only snapshot of one CPU-side retained model group.  These values are
 * copied out of the renderer; callers never receive renderer-owned pointers.
 */
struct ToriRS_D3D9RetainedGroupStats
{
    uint32_t write_cursor;
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t alive_slot_count;
};

/** Read-only diagnostics for the fixed-function renderer's retained state. */
struct ToriRS_D3D9RetainedStats
{
    struct ToriRS_D3D9RetainedGroupStats groups[TORIRS_D3D9_RETAINED_GROUP_COUNT];
    uint32_t pose_element_count;
    uint32_t pose_element_capacity;
};
#endif

struct ToriRS_D3D9*
ToriRS_D3D9_New(int width, int height);

void
ToriRS_D3D9_Free(struct ToriRS_D3D9* d3d9);

#if defined(TORIRS_D3D9_RETAINED_TEST_API)
/**
 * Attach a scene without creating a window or D3D device.  This is only for
 * CPU-only renderer tests that execute retained-resource commands; normal
 * clients must attach their scene through ToriRS_D3D9_Init.
 */
bool
ToriRS_D3D9_AttachSceneHeadlessForTest(
    struct ToriRS_D3D9* d3d9,
    struct ToriDraw_Scene* scene);

/** Copy the renderer's current CPU-retained allocation/high-water state. */
bool
ToriRS_D3D9_GetRetainedStats(
    struct ToriRS_D3D9 const* d3d9,
    struct ToriRS_D3D9RetainedStats* out_stats);

/** Query a retained pose's base in its owning VBO (page-local for Batch16). */
bool
ToriRS_D3D9_GetPoseBase(
    struct ToriRS_D3D9 const* d3d9,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base);
#endif

bool
ToriRS_D3D9_Init(
    struct ToriRS_D3D9* d3d9,
    void* native_window,
    struct ToriDraw_Scene* scene,
    bool z_buffer_enabled);

void
ToriRS_D3D9_SetViewport(
    struct ToriRS_D3D9* d3d9,
    int width,
    int height);

void
ToriRS_D3D9_SetInterfaceScaleMode(
    struct ToriRS_D3D9* d3d9,
    int mode);

void
ToriRS_D3D9_SetPick(struct ToriRS_D3D9* d3d9, int mouse_x, int mouse_y);

struct ToriRS_PickHits const*
ToriRS_D3D9_PickHits(struct ToriRS_D3D9 const* d3d9);

void
ToriRS_D3D9_Execute(
    struct ToriRS_D3D9* d3d9,
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
ToriRS_D3D9_DrawBootBar(
    struct ToriRS_D3D9* d3d9,
    int progress,
    int caption_font_id,
    char const* caption);

void
ToriRS_D3D9_RenderFrame(struct ToriRS_D3D9* d3d9, struct ToriRS_Frame* frame);

void
ToriRS_D3D9_Present(struct ToriRS_D3D9* d3d9);

/**
 * Read the finished frame off the device into `pixels`, top-down ARGB.
 *
 * `width`/`height` are the CANVAS size, not the window's -- the back buffer is
 * the client rect and the canvas is letterboxed inside it, so the readback is
 * sampled back down onto the canvas grid, exactly as the GL lanes do
 * (ToriRS_GL3_ReadPixels).
 *
 * MUST be called BEFORE ToriRS_D3D9_Present, the same as the GL twin: the swap
 * chain is D3DSWAPEFFECT_DISCARD, so after Present the back buffer's contents
 * are explicitly undefined.
 *
 * Returns false when there is no usable device. A pipeline stall by nature,
 * so App_DrawComplete only asks for it when a capture is actually pending.
 */
bool
ToriRS_D3D9_ReadPixels(
    struct ToriRS_D3D9* d3d9,
    int* pixels,
    int width,
    int height);

#endif
