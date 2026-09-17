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
    /* Projection + face sort + raster, named by ONE object, taken once at
     * init through ToriDraw_KernelTake -- which is also where it is validated
     * against this scene -- and passed to every stage (the *WithTable
     * entries). */
    const struct ToriDraw_Kernel* kernel;
    /*
     * The in-frame A/B (toridraw_frame_ab.h) with a TABLE per arm. Under
     * TORIDRAW_FRAME_AB=1, TORIDRAW_FRAME_AB_KERNELS=<A>,<B> names the face
     * sort each arm runs (`bucket` | `flat`) and TORIDRAW_FRAME_AB_BATCH=<A>,<B>
     * whether the batched presorted-run walk is armed (0 | 1); an unset
     * knob leaves that stage the same in both arms. Every model of a frame
     * draws through the frame's arm, so the two arms alternate ABBA inside
     * one process and the run-to-run mode of the box subtracts out.
     *
     * A table by value, so an arm can name a different sort without touching
     * the process-wide object the getter handed out. This used to be a copy of
     * the RASTER kernel with its deprecated stage-2 slot overwritten, which
     * meant the harness was the last thing keeping that slot alive.
     */
    struct ToriDraw_Kernel kernel_ab[2];
    int batch_ab[2];
    int* pixels;
    /* The buffer: what `pixels` holds and the world is drawn at. */
    int width;
    int height;
    int stride;
    /* The layout every command's 2D coordinates are in. Equal to the buffer
     * unless interface scaling is above 100%, and then every 2D command is
     * written scaled by width/layout_w, height/layout_h. @see
     * ToriRS_Soft3D_SetLayout. */
    int layout_w;
    int layout_h;
    bool scaled;
    /* All Settings' interface scaling mode: 0 nearest, 1 linear, 2 bicubic.
     * @see ToriRS_Soft3D_SetInterfaceScaleMode. */
    int interface_scale_mode;

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

/**
 * The layout the next frame's commands are in, when it is not the buffer:
 * the world renders at the buffer's pixels and every 2D command is scaled
 * into it. Call after Init and before SetPick (the pick point is a layout
 * point, and is scaled here to the world it tests).
 */
void
ToriRS_Soft3D_SetLayout(
    struct ToriRS_Soft3D* soft,
    int layout_w,
    int layout_h);

/**
 * The interface filter for the next frame (device option 15: 0 nearest,
 * 1 linear, 2 bicubic). Call after Init, which resets it to nearest.
 *
 * Nearest writes every 2D command scaled straight into the buffer. Linear and
 * bicubic draw each 2D segment 1:1 into a layout-sized layer and filter that
 * picture into the buffer once, the way the GPU lanes do.
 */
void
ToriRS_Soft3D_SetInterfaceScaleMode(
    struct ToriRS_Soft3D* soft,
    int mode);

/**
 * Draw layout-space pixels into a scaled buffer from outside the command
 * stream (a boot bar, a viewport notice): LayerBegin hands back a
 * layout-sized canvas whose `x0,y0 - x1,y1` region is transparent; LayerEnd
 * scales every pixel written there into the buffer. Identity when the frame
 * is not scaled: LayerBegin returns the buffer itself.
 */
int*
ToriRS_Soft3D_LayerBegin(
    struct ToriRS_Soft3D* soft,
    int x0,
    int y0,
    int x1,
    int y1);

void
ToriRS_Soft3D_LayerEnd(struct ToriRS_Soft3D* soft);

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
