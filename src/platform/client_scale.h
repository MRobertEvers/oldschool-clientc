#ifndef TORIRS_PLATFORM_CLIENT_SCALE_H
#define TORIRS_PLATFORM_CLIENT_SCALE_H

/*
 * Client scaling: the one place that decides the sizes, which nothing else may
 * work out for itself.
 *
 *   LAYOUT  what the interface lays itself out at (UITREE_LAYOUT_ROOT_W/H):
 *           every 2D coordinate the client produces is one of these.
 *   RENDER  the buffer: the pixels every renderer -- Soft3D, GL3, GLES2,
 *           D3D9 -- rasterises into. The 3D world is drawn at its full
 *           resolution; every 2D command is written into it SCALED by
 *           RENDER / LAYOUT. Interface scaling therefore sizes the interface
 *           and never the world.
 *   OUTPUT  the rectangle of the window the buffer is stretched into.
 *
 * One pipeline on every renderer: draw the buffer, then stretch it to the
 * window. A GPU renderer draws into an offscreen target of the buffer's size
 * whenever that differs from the output rectangle, and blits it with the
 * frame filter; the software one uploads it and lets SDL (or GDI) stretch.
 * The buffer is the output rectangle in the HighDPI unit, no larger than the
 * pixel limit (ClientScale_Present), so without a limit it IS the output.
 *
 * Every lane's present, pointer mapping, touch mapping, keyboard inset and
 * readback takes its OUTPUT rectangle from ClientScale_Present. They used to
 * carry four copies of one letterbox, and a fifth in the web page's CSS; a
 * copy that drifted put clicks somewhere other than where the frame was drawn.
 *
 * How the settings size the buffer, in the order they apply:
 *
 *   1. Interface scaling (device option 27): resizable, the window divided by
 *      the percent. Fixed: the classic frame, unchanged -- the window is sized
 *      to frame x percent instead.
 *   2. HighDPI (device option 34) says what that percent is OF. Device pixels:
 *      100% is one buffer pixel per drawable pixel. Window points: 100% is one
 *      buffer pixel per window POINT, so the percent is multiplied by the
 *      display density the platform detected. A fixed window is sized in
 *      points already, so on a fixed frame this step changes nothing.
 *   3. Stretch mode INTEGER never changes a resizable window's percent: 150%
 *      lays out at 150%, and step 6 shows that layout at the largest whole
 *      multiple the window holds, with bars. It used to round the percent DOWN
 *      to a whole 100 first, which on a Retina laptop left 100% the only scale
 *      that took -- every other step became 100% or 200%, and 200% cannot hold
 *      the frame. The official client's integer scaling is the same: it trims
 *      the stretched size to a multiple of the layout and never touches the
 *      layout (client.getStretchedDimensions). A FIXED window is sized by the
 *      percent, so there it is still rounded: @see ClientScale_RoundPercent.
 *   4. The pixel limit (a WxH resolution) caps what 100% is: a window whose
 *      100% buffer is larger than the limit on either axis is treated as the
 *      largest buffer that fits inside it, and interface scaling divides THAT.
 *      The two compose rather than compete: they used to both set one percent
 *      and the larger won, so under a 1366x768 limit on a Retina laptop 100%
 *      and 120% drew the same frame, and under 854x480 every scale did.
 *   5. The frame's minimum size. The window is grown to hold the frame at the
 *      CHOSEN scale (App_ResizableWindowFloor), so the settings win whenever
 *      the display has room. When it has not -- a 200% frame on a laptop
 *      screen, or a window the user holds smaller -- the percent is lowered,
 *      evenly, until the buffer holds the frame (ClientScale_LowerToFloor).
 *      A buffer smaller than the frame crops it: the login screen showed its
 *      top-left quarter at 200% on a Retina display.
 *   6. Stretch mode places the buffer in the window: keep aspect (fractional,
 *      centred), integer (whole multiple, centred; falls back to keep aspect
 *      when the window is smaller than the buffer), stretch (fills it), or
 *      none (the frame at the interface scale, centred; shrunk keeping aspect
 *      only when the window cannot hold it). A fixed frame in a window larger
 *      than its own size had no way to stay at that size before none.
 *   7. The frame filter smooths the buffer onto the output rectangle.
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
    /** The frame at `unstretched_percent`, centred, never enlarged. */
    CLIENT_SCALE_FIT_NONE,
    CLIENT_SCALE_FIT_COUNT
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
    /** 100% is one buffer pixel per drawable pixel. */
    CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS = 0,
    /** 100% is one buffer pixel per window point. */
    CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS,
    CLIENT_SCALE_HIGH_DPI_COUNT
};

struct ClientScaleSettings
{
    enum ClientScaleFit fit;
    /** Largest buffer, in pixels, per axis. 0: no limit on that axis. */
    int max_pixel_width;
    int max_pixel_height;
    /** Already resolved: "same as the interface filter" is the caller's. */
    enum ClientScaleFilter output_filter;
    enum ClientScaleHighDpi high_dpi;
    /** Drawable pixels per window point, as a percent: 100 on an ordinary
     *  display, 200 on a Retina one. Read only when `high_dpi` is not
     *  DEVICE_PIXELS, and must be positive then. */
    int density_percent;
    /** Drawable pixels per 100 layout pixels when the frame is not stretched:
     *  the size stretch mode NONE shows it at. A fixed window's percent times
     *  the display density; a resizable window's layout percent. Read only
     *  when `fit` is NONE, and must be positive then. */
    int unstretched_percent;
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
    /** Stretch mode INTEGER rounded the chosen percent down: a fixed window
     *  only, whose size the percent is. */
    int rounded_by_integer;
    /** The pixel limit capped the 100% buffer, so the scale is of the limit. */
    int raised_by_limit;
    /** The window could not hold the frame at the percent, so it was lowered. */
    int lowered_to_fit;
};

struct ClientScalePresent
{
    /** Where the frame lands, in the output area's pixels, top-left origin. */
    struct ClientScaleRect output;
    /** The buffer every renderer draws: the output in the HighDPI unit,
     *  inside the pixel limit. 2D commands arrive in layout pixels and are
     *  scaled by render / layout; the world is drawn at these pixels. */
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

/** The percent a FIXED window is sized by under stretch mode INTEGER. A
 *  resizable window's percent is never rounded: see rule 3 above. */
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
 * The buffer a resizable window lays out and renders at.
 *
 * `window_w`/`window_h` is the game area in drawable pixels. No frame floor is
 * applied here: @see ClientScale_LowerToFloor.
 */
static inline void
ClientScale_WindowLayout(
    struct ClientScaleSettings const* settings,
    int percent,
    int window_w,
    int window_h,
    struct ClientScaleLayout* out)
{
    int const density = ClientScale_LayoutDensityPercent(settings);
    int base;
    int effective;

    assert(settings);
    assert(out);
    assert(percent > 0);
    assert(window_w > 0);
    assert(window_h > 0);

    out->rounded_by_integer = 0;
    out->raised_by_limit = 0;
    out->lowered_to_fit = 0;
    /* What 100% is, in drawable pixels: the HighDPI unit, or the limit's. */
    base = density;
    if( settings->max_pixel_width > 0 || settings->max_pixel_height > 0 )
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
        if( needed > base )
        {
            base = needed;
            out->raised_by_limit = 1;
        }
    }
    effective = (int)((long long)percent * base / 100);
    if( effective < 1 )
        effective = 1;

    out->percent = effective;
    out->shown_percent = (int)((long long)effective * 100 / density);
    out->w = (int)((long long)window_w * 100 / effective);
    out->h = (int)((long long)window_h * 100 / effective);
    if( out->w < 1 )
        out->w = 1;
    if( out->h < 1 )
        out->h = 1;
}

/**
 * The percent, in drawable pixels, a resizable window has to be of a frame of
 * `frame_w`x`frame_h` for the frame to fit at the chosen `percent`: the size
 * the window is grown to and held at.
 *
 * Capped where the pixel limit cannot hold the frame at that percent -- a
 * 768-row limit shows a 503-row frame at 152% at most, however large the
 * window -- because a window grown past that buys nothing but a smaller frame
 * in a larger rectangle. Never below 100%: the frame at 1:1 is always owed.
 */
static inline int
ClientScale_WindowFloorPercent(
    struct ClientScaleSettings const* settings,
    int percent,
    int frame_w,
    int frame_h)
{
    int floor_percent;

    assert(settings);
    assert(percent > 0);
    assert(frame_w > 0);
    assert(frame_h > 0);

    floor_percent = percent;
    if( settings->max_pixel_width > 0 )
    {
        int const cap = (int)((long long)settings->max_pixel_width * 100 / frame_w);
        if( cap < floor_percent )
            floor_percent = cap;
    }
    if( settings->max_pixel_height > 0 )
    {
        int const cap = (int)((long long)settings->max_pixel_height * 100 / frame_h);
        if( cap < floor_percent )
            floor_percent = cap;
    }
    if( floor_percent < 100 )
        floor_percent = 100;
    return (int)((long long)floor_percent * ClientScale_LayoutDensityPercent(settings) / 100);
}

/**
 * Lower `layout`'s percent, evenly, until its buffer holds a frame of
 * `floor_w`x`floor_h` layout pixels. Runs after ClientScale_WindowLayout, and
 * after the pixel limit on purpose: a limit below the frame crops the frame,
 * and a cropped frame is not a setting anybody can use.
 *
 * Lowered to exactly the largest percent that fits, in every stretch mode, as
 * the official client does (client.getRealDimensions: min(window / 765,
 * window / 503)); integer mode's whole multiples are the present's business.
 * A window smaller than the frame at 100% gets a buffer larger than the
 * window, which the present then shrinks to fit.
 */
static inline void
ClientScale_LowerToFloor(
    struct ClientScaleSettings const* settings,
    int floor_w,
    int floor_h,
    int window_w,
    int window_h,
    struct ClientScaleLayout* layout)
{
    int const density = ClientScale_LayoutDensityPercent(settings);
    int fit;

    assert(settings);
    assert(layout);
    assert(floor_w > 0);
    assert(floor_h > 0);
    assert(window_w > 0);
    assert(window_h > 0);

    if( layout->w >= floor_w && layout->h >= floor_h )
        return;
    fit = (int)((long long)window_w * 100 / floor_w);
    {
        int const fit_h = (int)((long long)window_h * 100 / floor_h);
        if( fit_h < fit )
            fit = fit_h;
    }
    if( fit < 1 )
        fit = 1;
    if( fit >= layout->percent )
        return;

    layout->lowered_to_fit = 1;
    layout->percent = fit;
    layout->shown_percent = (int)((long long)fit * 100 / density);
    layout->w = (int)((long long)window_w * 100 / fit);
    layout->h = (int)((long long)window_h * 100 / fit);
    if( layout->w < floor_w )
        layout->w = floor_w;
    if( layout->h < floor_h )
        layout->h = floor_h;
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

/*
 * The buffer for an output rectangle already placed in `out`.
 *
 * The output in the HighDPI unit -- drawable pixels, or window points -- and
 * shrunk evenly until it fits the pixel limit. Integer mode keeps every step
 * a whole one: the buffer is a whole multiple of the layout that divides the
 * output's multiple, so layout -> buffer -> output are all integer scales.
 */
static inline void
client_scale_render_size(
    struct ClientScaleSettings const* settings,
    int layout_w,
    int layout_h,
    struct ClientScalePresent* out)
{
    int const density = ClientScale_LayoutDensityPercent(settings);

    assert(settings);
    assert(out);
    assert(layout_w > 0);
    assert(layout_h > 0);

    if( settings->fit == CLIENT_SCALE_FIT_INTEGER && !out->integer_fell_back &&
        out->output.w % layout_w == 0 && out->output.h % layout_h == 0 &&
        out->output.w / layout_w == out->output.h / layout_h )
    {
        int const multiple = out->output.w / layout_w;
        int most = multiple * 100 / density;
        int buffer_multiple = 1;

        if( settings->max_pixel_width > 0 && most > settings->max_pixel_width / layout_w )
            most = settings->max_pixel_width / layout_w;
        if( settings->max_pixel_height > 0 && most > settings->max_pixel_height / layout_h )
            most = settings->max_pixel_height / layout_h;
        for( int m = most; m >= 1; m-- )
        {
            if( multiple % m == 0 )
            {
                buffer_multiple = m;
                break;
            }
        }
        out->render_w = layout_w * buffer_multiple;
        out->render_h = layout_h * buffer_multiple;
        return;
    }

    {
        int base = density;
        if( settings->max_pixel_width > 0 )
        {
            int const needed = client_scale_percent_to_fit(out->output.w, settings->max_pixel_width);
            if( needed > base )
                base = needed;
        }
        if( settings->max_pixel_height > 0 )
        {
            int const needed = client_scale_percent_to_fit(out->output.h, settings->max_pixel_height);
            if( needed > base )
                base = needed;
        }
        out->render_w = (int)((long long)out->output.w * 100 / base);
        out->render_h = (int)((long long)out->output.h * 100 / base);
        if( out->render_w < 1 )
            out->render_w = 1;
        if( out->render_h < 1 )
            out->render_h = 1;
    }
}

/**
 * Where the frame lands in an output area, and the buffer every renderer
 * draws into for it. `layout_w`/`layout_h` is the interface's size; the
 * output keeps its shape.
 */
static inline void
ClientScale_Present(
    struct ClientScaleSettings const* settings,
    int layout_w,
    int layout_h,
    int area_w,
    int area_h,
    struct ClientScalePresent* out)
{
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
        int const integer_k = kx < ky ? kx : ky;
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
    case CLIENT_SCALE_FIT_NONE:
    {
        long long const w = (long long)layout_w * settings->unstretched_percent / 100;
        long long const h = (long long)layout_h * settings->unstretched_percent / 100;
        assert(settings->unstretched_percent > 0);
        if( w <= area_w && h <= area_h )
        {
            out->output.w = (int)w;
            out->output.h = (int)h;
            out->output.x = (area_w - out->output.w) / 2;
            out->output.y = (area_h - out->output.h) / 2;
            break;
        }
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

    client_scale_render_size(settings, layout_w, layout_h, out);
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
