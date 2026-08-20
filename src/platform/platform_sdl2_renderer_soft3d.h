#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_SOFT3D_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

struct ToriDraw_Scene;
struct ToriRS_Frame;

#include "render/torirs_polygon.h"

#define TORIRS_SOFT3D_BG 0xFF202428

struct ToriRS_Soft3D
{
    struct ToriDraw_Scene* scene;
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
