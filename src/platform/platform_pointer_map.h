#ifndef PLATFORM_POINTER_MAP_H
#define PLATFORM_POINTER_MAP_H

/*
 * Where a pointer in WINDOW POINTS lands in the client's LAYOUT, as one pure
 * function of the window's geometry -- the whole of the translation a mouse
 * press, a motion or a finger takes on the SDL lane (platform_sdl2.c,
 * PlatformWindow_MapMouse) and the same game area the present places the
 * frame in (sdl_game_area). Pure so it can be tested across every
 * combination that decides it, without a window: 1x and 2x density, the
 * plugin pane closed or open (rail, page, both), the title screen's fixed
 * 765x503 layout and the in-game resizable one, a fixed and a grown window.
 * @see src/platform/test/sdl_pointer_map_test.c (make test-sdl-pointer-map).
 *
 * The three units:
 *   - window POINTS: what SDL reports the pointer and SDL_GetWindowSize in;
 *   - drawable PIXELS: the framebuffer; points x density on a HighDPI window;
 *   - LAYOUT pixels: the client's canvas, which the present places into the
 *     game area (the drawable less the pane) by ClientScale_Present.
 * The pane is the trailing `pane_point_w` window points, in pixels at the
 * same ratio as the whole window (chrome_drawable_size).
 */

#include "platform/client_scale.h"

#include <assert.h>
#include <stdbool.h>

struct PlatformPointerGeometry
{
    /** SDL_GetWindowSize: the whole window, pane included. */
    int point_w;
    int point_h;
    /** The drawable, pane included, in pixels. */
    int drawable_w;
    int drawable_h;
    /** Window points the plugin pane takes at the trailing edge; 0 with none. */
    int pane_point_w;
    /** The client's canvas: what the frame is built at and the pointer is
     *  reported in. */
    int layout_w;
    int layout_h;
};

/** The pane's width in drawable pixels, clamped inside the drawable. */
static inline int
PlatformPointer_PanePixels(struct PlatformPointerGeometry const* geometry)
{
    long long pixels = 0;

    assert(geometry);
    if( geometry->pane_point_w > 0 && geometry->point_w > 0 && geometry->drawable_w > 0 )
        pixels = (long long)geometry->pane_point_w * geometry->drawable_w / geometry->point_w;
    if( pixels < 0 )
        pixels = 0;
    if( pixels > geometry->drawable_w )
        pixels = geometry->drawable_w;
    return (int)pixels;
}

/** The game area in drawable pixels: the drawable less the pane. */
static inline void
PlatformPointer_GameArea(
    struct PlatformPointerGeometry const* geometry,
    int* out_area_w,
    int* out_area_h)
{
    assert(geometry);
    assert(out_area_w);
    assert(out_area_h);
    *out_area_w = geometry->drawable_w - PlatformPointer_PanePixels(geometry);
    *out_area_h = geometry->drawable_h;
}

/**
 * A window point mapped into the layout, clamped inside it. False (and 0,0)
 * when the window has no area to map into -- a minimised window, or a pane
 * that took all of it -- which is a legitimate state, not a caller's error.
 */
static inline bool
PlatformPointer_WindowToLayout(
    struct ClientScaleSettings const* settings,
    struct PlatformPointerGeometry const* geometry,
    int win_x,
    int win_y,
    int* out_x,
    int* out_y)
{
    struct ClientScalePresent present;
    int area_w = 0;
    int area_h = 0;

    assert(settings);
    assert(geometry);
    assert(out_x);
    assert(out_y);
    assert(geometry->layout_w > 0);
    assert(geometry->layout_h > 0);
    *out_x = 0;
    *out_y = 0;
    PlatformPointer_GameArea(geometry, &area_w, &area_h);
    if( geometry->point_w <= 0 || geometry->point_h <= 0 || area_w <= 0 || area_h <= 0 )
        return false;
    ClientScale_Present(
        settings, geometry->layout_w, geometry->layout_h, area_w, area_h, &present);
    /* Points to pixels once, at the whole window's ratio; everything after
     * is the present's own arithmetic, inverted. */
    ClientScale_OutputToLayout(
        &present.output,
        geometry->layout_w,
        geometry->layout_h,
        (int)((long long)win_x * geometry->drawable_w / geometry->point_w),
        (int)((long long)win_y * geometry->drawable_h / geometry->point_h),
        out_x,
        out_y);
    return true;
}

/**
 * The inverse, for tests and probes: the window point at the centre of where
 * the present DRAWS layout pixel (layout_x, layout_y). A pointer there must
 * map back to that layout pixel; that is the whole contract.
 */
static inline bool
PlatformPointer_LayoutToWindow(
    struct ClientScaleSettings const* settings,
    struct PlatformPointerGeometry const* geometry,
    int layout_x,
    int layout_y,
    int* out_win_x,
    int* out_win_y)
{
    struct ClientScalePresent present;
    int area_w = 0;
    int area_h = 0;
    long long pixel_x;
    long long pixel_y;

    assert(settings);
    assert(geometry);
    assert(out_win_x);
    assert(out_win_y);
    assert(geometry->layout_w > 0);
    assert(geometry->layout_h > 0);
    PlatformPointer_GameArea(geometry, &area_w, &area_h);
    if( geometry->drawable_w <= 0 || geometry->drawable_h <= 0 || area_w <= 0 || area_h <= 0 )
        return false;
    ClientScale_Present(
        settings, geometry->layout_w, geometry->layout_h, area_w, area_h, &present);
    pixel_x = present.output.x +
              ((long long)layout_x * 2 + 1) * present.output.w / (2LL * geometry->layout_w);
    pixel_y = present.output.y +
              ((long long)layout_y * 2 + 1) * present.output.h / (2LL * geometry->layout_h);
    /* The window point whose pixel range holds that pixel. */
    *out_win_x = (int)(pixel_x * geometry->point_w / geometry->drawable_w);
    *out_win_y = (int)(pixel_y * geometry->point_h / geometry->drawable_h);
    return true;
}

#endif
