#ifndef SRC_PLATFORM_PLATFORM_RENDERER_SOFT3D_H
#define SRC_PLATFORM_PLATFORM_RENDERER_SOFT3D_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

struct ToriDraw_Scene;
struct ToriRS_Frame;

/*
 * The working buffers that have to survive a frame boundary: the blit scratch
 * and the outline/shadow LRU (what stops SpriteNewGraphicOutline recomputing
 * the same chrome icons every frame -- idle flamegraphs put it at ~2.5% of
 * samples). Opaque here because nothing outside the renderer reads it; it is
 * allocated by ToriPlatform_Renderer_Soft3D_New and released by ToriPlatform_Renderer_Soft3D_Free, and
 * ToriPlatform_Renderer_Soft3D_Init carries it across the per-frame reset.
 */
struct ToriPlatform_Renderer_Soft3D_Scratch;

#include "render/torirs_polygon.h"

#define TORIRS_SOFT3D_BG 0xFF202428

#include "toridraw_raster_kernel.h"

struct ToriPlatform_Renderer_Soft3D
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
    /* The depth-buffered world (ToriPlatform_Renderer_Soft3D_SetDepth): every
     * 3D pass is one frame-wide z-buffer and models draw unsorted through the
     * software-zbuffered table. The frame A/B does not apply to it. */
    bool depth;
    /* The rev-241 client's own depth buffer (ToriPlatform_Renderer_Soft3D_
     * SetRsZBuffer): one frame-wide z-buffer per 3D pass, every model drawn
     * through the opaque table as it arrives and again, in the same order,
     * through the translucent one at END_3D. The frame A/B does not apply. */
    bool rszbuffer;
    const struct ToriDraw_Kernel* kernel_translucent;
    /* Set while END_3D replays the pass's models through kernel_translucent:
     * they draw again, but do not pick again. */
    bool translucent_pass;
    int* pixels;
    /* The buffer: what `pixels` holds and the world is drawn at. */
    int width;
    int height;
    int stride;
    /* The layout every command's 2D coordinates are in. Equal to the buffer
     * unless interface scaling is above 100%, and then every 2D command is
     * written scaled by width/layout_w, height/layout_h. @see
     * ToriPlatform_Renderer_Soft3D_SetLayout. */
    int layout_w;
    int layout_h;
    bool scaled;
    /* All Settings' interface scaling mode: 0 nearest, 1 linear, 2 bicubic.
     * @see ToriPlatform_Renderer_Soft3D_SetInterfaceScaleMode. */
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
    struct ToriPlatform_Renderer_Soft3D_Scratch* scratch;
};

/** Allocate a renderer and its frame-crossing scratch. The renderer is meant
 * to be made once and re-pointed at each frame's buffer with Init; making one
 * per frame throws the outline cache away with it. */
struct ToriPlatform_Renderer_Soft3D*
ToriPlatform_Renderer_Soft3D_New(void);

/** Release a renderer and everything its scratch holds. Accepts NULL. */
void
ToriPlatform_Renderer_Soft3D_Free(struct ToriPlatform_Renderer_Soft3D* soft);

/** Point an already-New'd renderer at this frame's scene and pixel buffer.
 * Resets all frame state; the scratch and its caches carry over. */
void
ToriPlatform_Renderer_Soft3D_Init(
    struct ToriPlatform_Renderer_Soft3D* soft,
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
ToriPlatform_Renderer_Soft3D_SetLayout(
    struct ToriPlatform_Renderer_Soft3D* soft,
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
ToriPlatform_Renderer_Soft3D_SetInterfaceScaleMode(
    struct ToriPlatform_Renderer_Soft3D* soft,
    int mode);

/**
 * Draw the world through a depth buffer (the "Software (depth buffer)"
 * renderer) instead of the painter's order. Call after Init, which resets it
 * to the painter.
 *
 * Each BEGIN_3D .. END_3D pass then opens one frame-wide z-buffer
 * (ToriDraw_SceneZBufferBeginPass) and every model of the pass draws through
 * ToriDraw_KernelGetSoftwareZBuffered with no face sort, so models occlude
 * each other per pixel. That is only correct when the app collects the world
 * as TORIRS_WORLD_DEPTH -- the same contract as the GPU depth renderers.
 */
void
ToriPlatform_Renderer_Soft3D_SetDepth(
    struct ToriPlatform_Renderer_Soft3D* soft,
    bool depth);

/**
 * Draw the world the way the rev-241 client draws it under `::zbuf 1` (the
 * "Software (OSRS z-buffer)" renderer). Call after Init, which resets it.
 *
 * Each BEGIN_3D .. END_3D pass opens one frame-wide z-buffer. Every model of
 * the pass draws its opaque faces through ToriDraw_KernelGetSoftwareRsZBuffered
 * Opaque as it arrives, and is recorded; END_3D draws the recorded models
 * again, in the same order, through the translucent table, which draws only
 * their faces with alpha. The world is collected in the painter's order
 * (TORIRS_WORLD_PAINTER): the client's translucent pass follows it, and its
 * opaque pass does not care.
 */
void
ToriPlatform_Renderer_Soft3D_SetRsZBuffer(
    struct ToriPlatform_Renderer_Soft3D* soft,
    bool rszbuffer);

/**
 * Draw layout-space pixels into a scaled buffer from outside the command
 * stream (a boot bar, a viewport notice): LayerBegin hands back a
 * layout-sized canvas whose `x0,y0 - x1,y1` region is transparent; LayerEnd
 * scales every pixel written there into the buffer. Identity when the frame
 * is not scaled: LayerBegin returns the buffer itself.
 */
int*
ToriPlatform_Renderer_Soft3D_LayerBegin(
    struct ToriPlatform_Renderer_Soft3D* soft,
    int x0,
    int y0,
    int x1,
    int y1);

void
ToriPlatform_Renderer_Soft3D_LayerEnd(struct ToriPlatform_Renderer_Soft3D* soft);

/** Arm the world hittest for the next RenderFrame: resets pick_hits and
 * records the mouse point to test pickable models against. */
void
ToriPlatform_Renderer_Soft3D_SetPick(
    struct ToriPlatform_Renderer_Soft3D* soft,
    int mouse_x,
    int mouse_y);

/** Clear framebuffer to Soft3D background, then drain frame commands. */
void
ToriPlatform_Renderer_Soft3D_RenderFrame(
    struct ToriPlatform_Renderer_Soft3D* soft,
    struct ToriRS_Frame* frame);

/** Execute a single GFX command into soft->pixels. */
void
ToriPlatform_Renderer_Soft3D_Execute(
    struct ToriPlatform_Renderer_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd);

#endif
