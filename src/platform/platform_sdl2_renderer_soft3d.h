#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

struct ToriDraw_Scene;
struct ToriRS_Frame;

#include "render/torirs_polygon.h"

#define TORIRS_SOFT3D_BG 0xFF202428

/** Matches APP_DAMAGE_RECT_MAX; the renderer does not include app.h. */
#define TORIRS_SOFT3D_DAMAGE_RECT_MAX 4

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

    /* Damage rectangle for this frame, half-open [x0,x1) x [y0,y1), or
     * damage_valid == 0 for "the whole canvas". When set, the frame clears
     * only this box and every draw is clipped to it, leaving the rest of the
     * buffer holding the pixels the previous frame left there.
     *
     * The renderer does not decide this and cannot check it: whether last
     * frame's pixels are still correct is a fact about the UI tree, which
     * lives in App. @see App::damage_valid. */
    int damage_valid;
    int damage_x0;
    int damage_y0;
    int damage_x1;
    int damage_y1;
    /* The same damage as separate rectangles, used by the CLEAR only. The clip
     * above stays the bounding box because a ViewPort holds one rectangle;
     * the clear has no such constraint and skipping the strip between the
     * world viewport and the minimap is most of what damage buys here.
     *
     * Safe because of what a retained frame means: outside the live rects
     * nothing changed, so the pixels already there are the pixels the frame
     * wants. Chrome redrawn over them lands on its own previous output. */
    int damage_rect_count;
    int damage_rects[TORIRS_SOFT3D_DAMAGE_RECT_MAX][4];
};

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

/**
 * Restrict the next RenderFrame to a damage box: only this rectangle is
 * cleared, and every draw is clipped to it. Pixels outside are left as the
 * previous frame wrote them, so the caller must be able to prove they are
 * still correct -- see App::damage_valid for what that proof consists of.
 *
 * Not calling this leaves the frame full-canvas, which is always correct.
 */
void
ToriRS_Soft3D_SetDamage(
    struct ToriRS_Soft3D* soft,
    int x,
    int y,
    int w,
    int h);

/**
 * Narrow the CLEAR to these rectangles instead of the damage box. Must be
 * called after ToriRS_Soft3D_SetDamage, whose box must still cover them --
 * the draw clip keeps using the box. Passing 0 rects leaves the box clear.
 */
void
ToriRS_Soft3D_SetDamageClearRects(
    struct ToriRS_Soft3D* soft,
    int const (*rects)[4],
    int count);

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
