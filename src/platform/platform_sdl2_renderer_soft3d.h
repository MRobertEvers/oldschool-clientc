#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

struct ToriDraw_Scene;
struct ToriRS_Frame;

/*
 * The working buffers that have to survive a frame boundary: the blit scratch
 * and the outline/shadow LRU (what stops SpriteNewGraphicOutline recomputing
 * the same chrome icons every frame -- idle flamegraphs put it at ~2.5% of
 * samples). Opaque here because nothing outside the renderer reads it; it is
 * allocated by ToriRS_Soft3D_New and released by ToriRS_Soft3D_Free, and
 * ToriRS_Soft3D_Init carries it across the per-frame reset.
 */
struct ToriRS_Soft3DScratch;

#include "render/torirs_polygon.h"

#define TORIRS_SOFT3D_BG 0xFF202428

#include "toridraw_raster_kernel.h"

struct ToriRS_Soft3D
{
    struct ToriDraw_Scene* scene;
    /* Projection + face sort + raster, chosen once at init and passed to
     * every stage (toridraw.h, the *WithKernel entries). */
    const struct ToriDraw_RasterKernelSD* kernel;
    /*
     * The in-frame A/B (toridraw_frame_ab.h) with a kernel per arm. Under
     * TORIDRAW_FRAME_AB=1, TORIDRAW_FRAME_AB_KERNELS=<A>,<B> names the face
     * sort each arm runs (`bucket` | `flat`) and TORIDRAW_FRAME_AB_BATCH=<A>,<B>
     * whether the batched presorted-run walk is armed (0 | 1); an unset
     * knob leaves that stage the same in both arms. Every model of a frame
     * draws through the frame's arm, so the two arms alternate ABBA inside
     * one process and the run-to-run mode of the box subtracts out.
     */
    struct ToriDraw_RasterKernelSD kernel_ab[2];
    int batch_ab[2];
    int* pixels;
    int width;
    int height;
    int stride;

    /* Polygon run state: points accumulate between POLYGON_BEGIN and
     * POLYGON_END, and the fill happens on END. Held here rather than passed
     * through because a run spans several commands by design -- see the
     * TORIRSRC_POLYGON_* note in torirs_render.h. */
    struct ToriRS_RenderCommand_PolygonBegin polygon;
    int polygon_open;
    int polygon_x[TORIRS_POLYGON_MAX_POINTS];
    int polygon_y[TORIRS_POLYGON_MAX_POINTS];
    int polygon_count;

    bool has_3d;
    struct ToriDraw_ViewPort view_port_3d;
    struct ToriDraw_Camera camera_3d;

    /* Render-time world hittest (see torirs_pick.h): pickable DRAW_MODELs
     * that project VISIBLE and contain the mouse point land in pick_hits. */
    bool pick_enabled;
    int pick_mouse_x; /* canvas coords */
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;

    /* Owned by New/Free, and the one field Init does not reset. */
    struct ToriRS_Soft3DScratch* scratch;
};

/** Allocate a renderer and its frame-crossing scratch. The renderer is meant
 * to be made once and re-pointed at each frame's buffer with Init; making one
 * per frame throws the outline cache away with it. */
struct ToriRS_Soft3D*
ToriRS_Soft3D_New(void);

/** Release a renderer and everything its scratch holds. Accepts NULL. */
void
ToriRS_Soft3D_Free(struct ToriRS_Soft3D* soft);

/** Point an already-New'd renderer at this frame's scene and pixel buffer.
 * Resets all frame state; the scratch and its caches carry over. */
void
ToriRS_Soft3D_Init(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int width,
    int height);

/** Arm the world hittest for the next RenderFrame: resets pick_hits and
 * records the mouse point to test pickable models against. */
void
ToriRS_Soft3D_SetPick(
    struct ToriRS_Soft3D* soft,
    int mouse_x,
    int mouse_y);

/** Clear framebuffer to Soft3D background, then drain frame commands. */
void
ToriRS_Soft3D_RenderFrame(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame);

/** Execute a single GFX command into soft->pixels. */
void
ToriRS_Soft3D_Execute(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd);

#endif
