#ifndef TORIRS_PLATFORM_CLIENT_SCALE_H
#define TORIRS_PLATFORM_CLIENT_SCALE_H

/*
 * Client scaling: the one place that decides three sizes, which nothing else
 * may work out for itself.
 *
 *   LAYOUT  what the interface lays itself out at (UITREE_LAYOUT_ROOT_W/H).
 *   RENDER  the pixels the world and the interface are rasterised into.
 *   OUTPUT  the rectangle of the window those pixels are shown in.
 *
 * Every lane's present, pointer mapping, touch mapping, keyboard inset and
 * readback takes its OUTPUT rectangle from ClientScale_Present. They used to
 * carry four copies of one letterbox, and a fifth in the web page's CSS; a
 * copy that drifted put clicks somewhere other than where the frame was drawn.
 *
 * How the settings combine, in the order they apply:
 *
 *   1. Interface scaling (device option 27) sets the LAYOUT. Resizable: the
 *      window divided by the percent. Fixed: the classic frame, unchanged --
 *      the window is sized to frame x percent instead.
 *   2. HighDPI (device option 34) says what that percent is OF. Device pixels:
 *      100% is one layout pixel per drawable pixel, so a 2x display shows the
 *      interface at half its authored size. Match display and window points:
 *      100% is one layout pixel per window POINT, so the percent is multiplied
 *      by the display density the platform detected. A fixed window is sized
 *      in points already, so on a fixed frame this step changes nothing.
 *   3. Stretch mode INTEGER rounds the chosen percent DOWN to a whole multiple
 *      of 100 before it is used, so every layout pixel lands on a whole number
 *      of window pixels (150% becomes 100%, with bars).
 *   4. The pixel limit (a WxH resolution), policy ENLARGE_INTERFACE: a
 *      resizable layout larger than the limit on either axis raises the
 *      percent until it fits -- the interface grows rather than the frame
 *      going over budget. Policy KEEP_INTERFACE leaves the layout alone.
 *   5. The frame's floor: a layout smaller than the frame can be laid out in
 *      LOWERS the percent until both axes clear it, never below 1:1 drawable
 *      pixels. Both axes move together, so the frame keeps the window's shape;
 *      clamping only the short axis is what used to show a 200% frame as a
 *      wide strip with bars above and below it.
 *   6. Stretch mode places the frame in the window: keep aspect (fractional,
 *      centred), integer (whole multiple, centred; falls back to keep aspect
 *      when the window is smaller than the layout), or stretch (fills it).
 *   7. The pixel limit caps the RENDER buffer of a renderer that draws at
 *      output resolution (GL3, GLES2, D3D9), never below the layout; HighDPI
 *      "window points" caps it at the window's size in points too. A CPU
 *      renderer always renders at the layout size.
 *   8. The output filter smooths the render buffer onto the output rectangle.
 *
 * Header-only on purpose: every lane includes it, and none of their source
 * lists should have to agree about one more translation unit.
 */

#include <assert.h>

enum ClientScaleFit
{
    CLIENT_SCALE_FIT_KEEP_ASPECT = 0,
    CLIENT_SCALE_FIT_INTEGER,
    CLIENT_SCALE_FIT_STRETCH,
    CLIENT_SCALE_FIT_COUNT
};

enum ClientScaleLimitPolicy
{
    CLIENT_SCALE_LIMIT_ENLARGE_INTERFACE = 0,
    CLIENT_SCALE_LIMIT_KEEP_INTERFACE,
    CLIENT_SCALE_LIMIT_POLICY_COUNT
};

/* Same values as RS_CS2_UI_SCALE_MODE_*, which is what the cache stores. */
enum ClientScaleFilter
{
    CLIENT_SCALE_FILTER_NEAREST = 0,
    CLIENT_SCALE_FILTER_LINEAR,
    CLIENT_SCALE_FILTER_BICUBIC,
    CLIENT_SCALE_FILTER_COUNT
};

/* Already resolved: the store's "automatic" is the caller's to decide. A
 * zeroed struct is DEVICE_PIXELS, which reads no density. */
enum ClientScaleHighDpi
{
    /** 100% is one layout pixel per drawable pixel. */
    CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS = 0,
    /** 100% is one layout pixel per window point; rendered at drawable pixels. */
    CLIENT_SCALE_HIGH_DPI_MATCH_DISPLAY,
    /** 100% is one layout pixel per window point, and rendered at points. */
    CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS,
    CLIENT_SCALE_HIGH_DPI_COUNT
};

struct ClientScaleSettings
{
    enum ClientScaleFit fit;
    /** Largest render buffer, in pixels, per axis. 0: no limit on that axis. */
    int max_pixel_width;
    int max_pixel_height;
    enum ClientScaleLimitPolicy limit_policy;
    /** Already resolved: "same as the interface filter" is the caller's. */
    enum ClientScaleFilter output_filter;
    enum ClientScaleHighDpi high_dpi;
    /** Drawable pixels per window point, as a percent: 100 on an ordinary
     *  display, 200 on a Retina one. Read only when `high_dpi` is not
     *  DEVICE_PIXELS, and must be positive then. */
    int density_percent;
};

struct ClientScaleRect
{
    int x;
    int y;
    int w;
    int h;
};

struct ClientScaleLayout
{
    int w;
    int h;
    /** The percent the window was actually divided by, in drawable pixels. */
    int percent;
    /** The same scale in the units the player picked it in: `percent` with
     *  the HighDPI density taken back out. What a readout should show. */
    int shown_percent;
    /** Stretch mode INTEGER rounded the chosen percent down. */
    int rounded_by_integer;
    /** The pixel limit raised the percent. */
    int raised_by_limit;
    /** The frame's floor lowered the percent. */
    int lowered_by_floor;
};

struct ClientScalePresent
{
    /** Where the frame lands, in the output area's pixels, top-left origin. */
    struct ClientScaleRect output;
    int render_w;
    int render_h;
    /** Stretch mode INTEGER could not fit one multiple and kept aspect. */
    int integer_fell_back;
};

/** What 100% is worth in drawable pixels under the HighDPI mode, as a
 *  percent: the density, or 100 for device pixels. */
static inline int
ClientScale_LayoutDensityPercent(struct ClientScaleSettings const* settings)
{
    assert(settings);
    if( settings->high_dpi == CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS )
        return 100;
    assert(settings->density_percent > 0);
    return settings->density_percent;
}

/** The percent stretch mode INTEGER will actually use. */
static inline int
ClientScale_RoundPercent(
    struct ClientScaleSettings const* settings,
    int percent)
{
    assert(settings);
    assert(percent > 0);
    if( settings->fit != CLIENT_SCALE_FIT_INTEGER )
        return percent;
    percent = percent / 100 * 100;
    return percent < 100 ? 100 : percent;
}

/* The smallest percent that fits `window_px` into `limit_px`, rounded up. */
static inline int
client_scale_percent_to_fit(
    int window_px,
    int limit_px)
{
    assert(limit_px > 0);
    return (int)(((long long)window_px * 100 + limit_px - 1) / limit_px);
}

/**
 * The layout a resizable window lays out at.
 *
 * `window_w`/`window_h` is the game area in drawable pixels. `floor_w` and
 * `floor_h` are the smallest layout the frame on screen can be laid out in
 * (0: no floor on that axis); a floor is a fact about the frame, which this
 * module does not know, so the caller states it. A window smaller than the
 * floor even at 1:1 still gets a layout below it, and the caller's own clamp
 * (App_SetCanvasSize) letterboxes that case as it always has.
 */
static inline void
ClientScale_WindowLayout(
    struct ClientScaleSettings const* settings,
    int percent,
    int window_w,
    int window_h,
    int floor_w,
    int floor_h,
    struct ClientScaleLayout* out)
{
    int const density = ClientScale_LayoutDensityPercent(settings);
    int rounded;
    int effective;

    assert(settings);
    assert(out);
    assert(percent > 0);
    assert(window_w > 0);
    assert(window_h > 0);
    assert(floor_w >= 0);
    assert(floor_h >= 0);

    rounded = ClientScale_RoundPercent(settings, percent);
    out->rounded_by_integer = rounded != percent;
    out->raised_by_limit = 0;
    out->lowered_by_floor = 0;
    effective = (int)((long long)rounded * density / 100);
    if( effective < 1 )
        effective = 1;

    if( settings->limit_policy == CLIENT_SCALE_LIMIT_ENLARGE_INTERFACE &&
        (settings->max_pixel_width > 0 || settings->max_pixel_height > 0) )
    {
        int needed = 0;
        if( settings->max_pixel_width > 0 )
            needed = client_scale_percent_to_fit(window_w, settings->max_pixel_width);
        if( settings->max_pixel_height > 0 )
        {
            int const needed_h = client_scale_percent_to_fit(window_h, settings->max_pixel_height);
            if( needed_h > needed )
                needed = needed_h;
        }
        if( settings->fit == CLIENT_SCALE_FIT_INTEGER )
            needed = (needed + 99) / 100 * 100;
        if( needed > effective )
        {
            effective = needed;
            out->raised_by_limit = 1;
        }
    }

    if( (floor_w > 0 && window_w * 100 / effective < floor_w) ||
        (floor_h > 0 && window_h * 100 / effective < floor_h) )
    {
        /* The largest percent whose layout clears the floor on BOTH axes. */
        int lowered = effective;
        if( floor_w > 0 && window_w * 100 / floor_w < lowered )
            lowered = window_w * 100 / floor_w;
        if( floor_h > 0 && window_h * 100 / floor_h < lowered )
            lowered = window_h * 100 / floor_h;
        if( settings->fit == CLIENT_SCALE_FIT_INTEGER )
            lowered = lowered / 100 * 100;
        /* Never below 1:1: a frame drawn at fewer drawable pixels than it has
         * throws interface pixels away. The window is what is short then. */
        if( lowered < 100 )
            lowered = 100;
        if( lowered < effective )
        {
            effective = lowered;
            out->lowered_by_floor = 1;
        }
    }

    out->percent = effective;
    out->shown_percent = (int)((long long)effective * 100 / density);
    out->w = (int)((long long)window_w * 100 / effective);
    out->h = (int)((long long)window_h * 100 / effective);
    if( out->w < 1 )
        out->w = 1;
    if( out->h < 1 )
        out->h = 1;
}

/* Byte-for-byte the letterbox every lane carried before this module, so the
 * default mode moves no pixel. */
static inline void
client_scale_keep_aspect(
    int layout_w,
    int layout_h,
    int area_w,
    int area_h,
    struct ClientScaleRect* out)
{
    float const src_aspect = (float)layout_w / (float)layout_h;
    float const area_aspect = (float)area_w / (float)area_h;

    if( src_aspect > area_aspect )
    {
        out->w = area_w;
        out->h = (int)((float)area_w / src_aspect);
        out->x = 0;
        out->y = (area_h - out->h) / 2;
    }
    else
    {
        out->h = area_h;
        out->w = (int)((float)area_h * src_aspect);
        out->y = 0;
        out->x = (area_w - out->w) / 2;
    }
}

/**
 * Where a layout-sized frame lands in an output area, and how big a buffer
 * a renderer should draw it into.
 *
 * `renders_at_output`: 1 for a renderer that rasterises at output resolution
 * (the GPU lanes), 0 for one that fills a layout-sized CPU buffer.
 */
static inline void
ClientScale_Present(
    struct ClientScaleSettings const* settings,
    int layout_w,
    int layout_h,
    int area_w,
    int area_h,
    int renders_at_output,
    struct ClientScalePresent* out)
{
    int integer_k = 0;

    assert(settings);
    assert(out);
    assert(layout_w > 0);
    assert(layout_h > 0);

    out->integer_fell_back = 0;
    if( area_w <= 0 || area_h <= 0 )
    {
        out->output.x = 0;
        out->output.y = 0;
        out->output.w = layout_w;
        out->output.h = layout_h;
        out->render_w = layout_w;
        out->render_h = layout_h;
        return;
    }

    switch( settings->fit )
    {
    case CLIENT_SCALE_FIT_STRETCH:
        out->output.x = 0;
        out->output.y = 0;
        out->output.w = area_w;
        out->output.h = area_h;
        break;
    case CLIENT_SCALE_FIT_INTEGER:
    {
        int const kx = area_w / layout_w;
        int const ky = area_h / layout_h;
        integer_k = kx < ky ? kx : ky;
        if( integer_k >= 1 )
        {
            out->output.w = layout_w * integer_k;
            out->output.h = layout_h * integer_k;
            out->output.x = (area_w - out->output.w) / 2;
            out->output.y = (area_h - out->output.h) / 2;
            break;
        }
        out->integer_fell_back = 1;
        client_scale_keep_aspect(layout_w, layout_h, area_w, area_h, &out->output);
        break;
    }
    case CLIENT_SCALE_FIT_KEEP_ASPECT:
    default:
        client_scale_keep_aspect(layout_w, layout_h, area_w, area_h, &out->output);
        break;
    }
    if( out->output.w < 1 )
        out->output.w = 1;
    if( out->output.h < 1 )
        out->output.h = 1;

    if( !renders_at_output )
    {
        out->render_w = layout_w;
        out->render_h = layout_h;
        return;
    }
    out->render_w = out->output.w;
    out->render_h = out->output.h;
    {
        /* The render box: the pixel limit, and under HighDPI "window points"
         * the game area in points. 0 is no cap on that axis. */
        int cap_w = settings->max_pixel_width;
        int cap_h = settings->max_pixel_height;

        if( settings->high_dpi == CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS )
        {
            int points_w;
            int points_h;
            assert(settings->density_percent > 0);
            points_w = (int)((long long)area_w * 100 / settings->density_percent);
            points_h = (int)((long long)area_h * 100 / settings->density_percent);
            if( cap_w <= 0 || points_w < cap_w )
                cap_w = points_w;
            if( cap_h <= 0 || points_h < cap_h )
                cap_h = points_h;
        }
        /* Never below the layout: under the limit's KEEP_INTERFACE policy a
         * layout may be larger than the limit, and drawing it into fewer
         * pixels than it has would throw interface pixels away. */
        if( cap_w > 0 && cap_w < layout_w )
            cap_w = layout_w;
        if( cap_h > 0 && cap_h < layout_h )
            cap_h = layout_h;
        if( (cap_w > 0 && out->render_w > cap_w) || (cap_h > 0 && out->render_h > cap_h) )
        {
            if( integer_k >= 1 )
            {
                /* Stay on the layout's grid, so a capped integer frame is
                 * still whole pixels all the way down. */
                int k = integer_k;
                if( cap_w > 0 && cap_w / layout_w < k )
                    k = cap_w / layout_w;
                if( cap_h > 0 && cap_h / layout_h < k )
                    k = cap_h / layout_h;
                out->render_w = layout_w * k;
                out->render_h = layout_h * k;
            }
            else
            {
                /* One factor for both axes, so the buffer keeps the output's
                 * shape; the tighter axis sets it. */
                long long num = 1;
                long long den = 1;
                if( cap_w > 0 && out->render_w > cap_w )
                {
                    num = cap_w;
                    den = out->render_w;
                }
                if( cap_h > 0 && out->render_h > cap_h &&
                    (long long)cap_h * den < num * out->render_h )
                {
                    num = cap_h;
                    den = out->render_h;
                }
                out->render_w = (int)((long long)out->output.w * num / den);
                out->render_h = (int)((long long)out->output.h * num / den);
                if( out->render_w < layout_w && out->output.w >= layout_w )
                    out->render_w = layout_w;
                if( out->render_h < layout_h && out->output.h >= layout_h )
                    out->render_h = layout_h;
                if( out->render_w < 1 )
                    out->render_w = 1;
                if( out->render_h < 1 )
                    out->render_h = 1;
            }
        }
    }
}

/**
 * An output-area point, in the same pixels ClientScale_Present was given,
 * mapped into layout coordinates and clamped inside the layout.
 */
static inline void
ClientScale_OutputToLayout(
    struct ClientScaleRect const* output,
    int layout_w,
    int layout_h,
    int area_x,
    int area_y,
    int* out_x,
    int* out_y)
{
    long long x;
    long long y;

    assert(output);
    assert(output->w > 0);
    assert(output->h > 0);
    assert(layout_w > 0);
    assert(layout_h > 0);
    assert(out_x);
    assert(out_y);

    x = (long long)(area_x - output->x) * layout_w / output->w;
    y = (long long)(area_y - output->y) * layout_h / output->h;
    if( x < 0 )
        x = 0;
    else if( x >= layout_w )
        x = layout_w - 1;
    if( y < 0 )
        y = 0;
    else if( y >= layout_h )
        y = layout_h - 1;
    *out_x = (int)x;
    *out_y = (int)y;
}

#endif
