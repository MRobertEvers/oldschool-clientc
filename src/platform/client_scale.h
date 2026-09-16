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
 *   2. Stretch mode INTEGER rounds that percent DOWN to a whole multiple of
 *      100 before it is used, so every layout pixel lands on a whole number
 *      of window pixels (150% becomes 100%, with bars).
 *   3. The pixel limit (max_pixel_height), policy ENLARGE_INTERFACE: a
 *      resizable layout taller than the limit raises the percent until it
 *      fits -- the interface grows rather than the frame going over budget.
 *      Policy KEEP_INTERFACE leaves the layout alone.
 *   4. Stretch mode places the frame in the window: keep aspect (fractional,
 *      centred), integer (whole multiple, centred; falls back to keep aspect
 *      when the window is smaller than the layout), or stretch (fills it).
 *   5. The pixel limit caps the RENDER buffer of a renderer that draws at
 *      output resolution (GL3, GLES2, D3D9), never below the layout. A CPU
 *      renderer always renders at the layout size.
 *   6. The output filter smooths the render buffer onto the output rectangle.
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

struct ClientScaleSettings
{
    enum ClientScaleFit fit;
    /** Tallest render buffer, in pixels. 0: no limit. */
    int max_pixel_height;
    enum ClientScaleLimitPolicy limit_policy;
    /** Already resolved: "same as the interface filter" is the caller's. */
    enum ClientScaleFilter output_filter;
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
    /** The percent the layout was actually divided by. */
    int percent;
    /** Stretch mode INTEGER rounded the chosen percent down. */
    int rounded_by_integer;
    /** The pixel limit raised the percent. */
    int raised_by_limit;
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

/**
 * The layout a resizable window lays out at.
 *
 * `window_w`/`window_h` is the game area in output pixels. The caller still
 * applies its own floor (App_SetCanvasSize): a floor is a fact about the frame
 * on screen, which this module does not know.
 */
static inline void
ClientScale_WindowLayout(
    struct ClientScaleSettings const* settings,
    int percent,
    int window_w,
    int window_h,
    struct ClientScaleLayout* out)
{
    int effective;

    assert(settings);
    assert(out);
    assert(percent > 0);
    assert(window_w > 0);
    assert(window_h > 0);

    effective = ClientScale_RoundPercent(settings, percent);
    out->rounded_by_integer = effective != percent;
    out->raised_by_limit = 0;
    if( settings->max_pixel_height > 0 &&
        settings->limit_policy == CLIENT_SCALE_LIMIT_ENLARGE_INTERFACE &&
        window_h * 100 / effective > settings->max_pixel_height )
    {
        int needed =
            (window_h * 100 + settings->max_pixel_height - 1) / settings->max_pixel_height;
        if( settings->fit == CLIENT_SCALE_FIT_INTEGER )
            needed = (needed + 99) / 100 * 100;
        if( needed > effective )
        {
            effective = needed;
            out->raised_by_limit = 1;
        }
    }
    out->percent = effective;
    out->w = window_w * 100 / effective;
    out->h = window_h * 100 / effective;
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
    if( settings->max_pixel_height > 0 )
    {
        /* Never below the layout: under the limit's KEEP_INTERFACE policy a
         * layout may be taller than the limit, and drawing it into fewer rows
         * than it has would throw interface pixels away. */
        int const limit =
            settings->max_pixel_height > layout_h ? settings->max_pixel_height : layout_h;
        if( out->render_h > limit )
        {
            if( integer_k >= 1 )
            {
                /* Stay on the layout's grid, so a capped integer frame is
                 * still whole pixels all the way down. */
                int const k = limit / layout_h;
                out->render_w = layout_w * k;
                out->render_h = layout_h * k;
            }
            else
            {
                out->render_w = (int)((long long)out->output.w * limit / out->output.h);
                out->render_h = limit;
                if( out->render_w < 1 )
                    out->render_w = 1;
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
