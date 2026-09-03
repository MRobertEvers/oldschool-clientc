/*
 * The SDL chrome executor: the plugin shell attached to the game window.
 *
 * A SURFACE executor (see the two kinds in torirs_chrome_exec.h): the widgets
 * are still ToriRSChrome's, laid out by ToriRSChrome and rasterised by the same
 * software path that draws them in the game canvas. What changes is only where
 * the pixels land and where the pointer comes from. That is the whole reason it
 * can be pixel-identical to the in-canvas panel. By default the surface is a
 * pane inside the existing SDL window; TORIRS_CHROME_DETACHED=1 is the
 * explicit developer/user opt-in to the legacy auxiliary window.
 *
 * It is the first thing in this tree to want a second OS window, so the
 * platform API under it (PlatformWindow_Aux*) is deliberately the smallest one
 * that serves exactly this: open, close, a pixel buffer, present, and a close
 * request coming back. A backend that has none of that returns false from
 * begin() and the surface falls back to the buffer executor, which is why none
 * of this is load-bearing for a client that never opens a plugin window.
 *
 * This file is compiled only where the SDL platform is, and
 * TORIRS_CHROME_EXEC_SDL_AVAILABLE is what tells the chooser so.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_skin.h"

#include "../platform/platform_window.h"
#include "uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Opening size, in window POINTS -- a physical size on a desk, not a pixel
 * count. The window is resizable; this is only where it starts.
 *
 * The SURFACE that comes up inside it is the DRAWABLE, which on a HighDPI
 * display is a multiple of this, and it is the surface -- not this -- that the
 * chrome lays out in (PlatformWindow_AuxWidth/Height, handed over by
 * chrome_sdl_surface_size). The chrome's scale is the display's density, so
 * the two rise together: a 2x display gets 2x rows in a 2x buffer, at the same
 * physical size as 1x rows in a 1x one. Sized in pixels here instead, a 2x
 * chrome would be laid out in half the room it needs -- labels under their
 * fields, and half of a settings page past the bottom edge where widgets get a
 * zero box and stop being clickable at all.
 */
#define CHROME_SDL_PAGE_W 320
#define CHROME_SDL_RAIL_W 40
#define CHROME_SDL_H 420
#define CHROME_SDL_RAIL_QUEUE_MAX 32
#define CHROME_SDL_ICON_PLUGIN_MAX 32

struct ChromeSdl
{
    struct PlatformWindow* platform;
    /*
     * How to turn a display list into pixels.
     *
     * Injected rather than called directly, because rasterising needs the
     * scene the baked fonts and the skin were registered in, the frame
     * translator and a software backend -- three things ui/ deliberately does
     * not depend on. The app owns all of them already for the game canvas, so
     * it lends the same one here and the second window is drawn by the same
     * code as the first rather than by a second copy of it.
     */
    ToriRSChromeRasteriseFn rasterise;
    void* rasterise_user;
    /** Set once begin() succeeded, so present/input on a refused executor are
     *  no-ops rather than calls into a window that was never made. */
    int open;
    /** The surface is inside the main native window, not the optional aux. */
    int attached;
    /**
     * The panel this window is showing, latched from PANEL_OPEN.
     *
     * The one thing a surface executor needs out of the command stream, and
     * only because a close has to name what closed. Everything else about the
     * widgets it draws it reads off the display list.
     */
    int panel;
    /** The window's own X was used; the model has not been told yet. */
    int close_pending;
    /**
     * This frame's window-move handles, published by the host after Build.
     *
     * The reason a copy lives here rather than the callback asking the model:
     * SDL calls the hit test from inside its event pump, mid-press, and the
     * model is the frame thread's. @see ToriRSChrome_WindowDragRegion.
     */
    struct ToriRSChromeDragRegion drag;
    /** The frame actually came off. Distinct from the wish below, which the
     *  video driver is allowed to refuse. */
    int borderless;

    /** Page-only scratch. The platform texture is page + far-right rail. */
    int* page_pixels;
    int page_width;
    int page_height;

    struct ToriRSChromeRailSnapshot rail;
    int rail_live;
    int rail_hover;
    int rail_pressed;
    int rail_scroll;
    uint32_t rail_press_generation;
    uint64_t rail_sequence;
    struct ToriRSChromeRailIntent rail_pending[CHROME_SDL_RAIL_QUEUE_MAX];
    int rail_pending_count;
    int layout_pending;

    struct ChromeSdlIcon
    {
        uint32_t revision;
        int width;
        int height;
        uint32_t argb[TORIRS_CHROME_RAIL_ICON_PIXELS_MAX];
    } icons[CHROME_SDL_ICON_PLUGIN_MAX];
};

/* The one instance. A second plugin window is not a thing the sandbox allows,
 * so a registry of them would be a registry with one entry in it. */
static struct ChromeSdl g_chrome_sdl;

/* The WISH, outside the instance on purpose: ToriRSChromeExec_Sdl clears
 * g_chrome_sdl every time an executor is built, and the shell sets this once at
 * boot -- long before the button that opens the window is pressed. */
static int g_chrome_sdl_want_borderless;

/** The frameless window has been reported once. @see chrome_sdl_begin. */
static int g_chrome_sdl_reported_borderless;

static int
chrome_sdl_page_ensure(struct ChromeSdl* s, int width, int height)
{
    int* pixels;

    assert(s);
    if( width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > SIZE_MAX / sizeof(*pixels) )
        return 0;
    if( s->page_pixels && s->page_width == width && s->page_height == height )
        return 1;
    pixels = realloc(
        s->page_pixels, (size_t)width * (size_t)height * sizeof(*pixels));
    if( !pixels )
        return 0;
    s->page_pixels = pixels;
    s->page_width = width;
    s->page_height = height;
    memset(pixels, 0, (size_t)width * (size_t)height * sizeof(*pixels));
    return 1;
}

static void
chrome_sdl_rect(
    int* pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
{
    int x1;
    int y1;

    if( !pixels || width <= 0 || height <= 0 || w <= 0 || h <= 0 )
        return;
    x1 = x + w;
    y1 = y + h;
    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x1 > width )
        x1 = width;
    if( y1 > height )
        y1 = height;
    for( int py = y; py < y1; py++ )
        for( int px = x; px < x1; px++ )
            pixels[py * width + px] = (int)color;
}

static uint32_t
chrome_sdl_over(uint32_t dst, uint32_t src)
{
    unsigned const a = src >> 24;
    unsigned const inv = 255u - a;
    unsigned const r = (((src >> 16) & 0xffu) * a + ((dst >> 16) & 0xffu) * inv) / 255u;
    unsigned const g = (((src >> 8) & 0xffu) * a + ((dst >> 8) & 0xffu) * inv) / 255u;
    unsigned const b = ((src & 0xffu) * a + (dst & 0xffu) * inv) / 255u;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void
chrome_sdl_blit_nearest(
    int* dst,
    int dst_w,
    int dst_h,
    int x,
    int y,
    int w,
    int h,
    uint32_t const* src,
    int src_w,
    int src_h)
{
    if( !dst || !src || w <= 0 || h <= 0 || src_w <= 0 || src_h <= 0 )
        return;
    for( int py = 0; py < h; py++ )
    {
        int const dy = y + py;
        int const sy = py * src_h / h;
        if( dy < 0 || dy >= dst_h )
            continue;
        for( int px = 0; px < w; px++ )
        {
            int const dx = x + px;
            uint32_t const source = src[sy * src_w + px * src_w / w];
            unsigned const a = source >> 24;
            if( dx < 0 || dx >= dst_w || a == 0 )
                continue;
            if( a == 255 )
                dst[dy * dst_w + dx] = (int)source;
            else
                dst[dy * dst_w + dx] =
                    (int)chrome_sdl_over((uint32_t)dst[dy * dst_w + dx], source);
        }
    }
}

static uint16_t
chrome_sdl_glyph(char ch)
{
    static uint16_t const digits[10] = {
        0x7b6f, 0x2492, 0x73e7, 0x73cf, 0x5bc9,
        0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf,
    };
    static uint16_t const letters[26] = {
        0x7bed, 0x6bae, 0x7927, 0x6b6e, 0x79e7, 0x79e4, 0x796f,
        0x5bed, 0x7497, 0x124f, 0x5bad, 0x4927, 0x5fed, 0x5f6d,
        0x7b6f, 0x7be4, 0x7b7b, 0x7bec, 0x79cf, 0x7492, 0x5b6f,
        0x5b6a, 0x5f7d, 0x5aad, 0x5a92, 0x72a7,
    };

    if( ch >= '0' && ch <= '9' )
        return digits[ch - '0'];
    if( ch >= 'a' && ch <= 'z' )
        ch = (char)(ch - 'a' + 'A');
    if( ch >= 'A' && ch <= 'Z' )
        return letters[ch - 'A'];
    if( ch == '+' )
        return 0x05d0;
    if( ch == '!' )
        return 0x2492;
    return 0x0002;
}

static void
chrome_sdl_badge(
    int* pixels,
    int width,
    int height,
    int right,
    int bottom,
    char const* text,
    int scale)
{
    int count = 0;
    int glyph_scale = scale > 1 ? scale : 1;
    int badge_w;
    int x;
    int y;

    if( !text || !text[0] )
        return;
    while( count < 3 && text[count] )
        count++;
    badge_w = (count * 4 + 2) * glyph_scale;
    x = right - badge_w;
    y = bottom - 7 * glyph_scale;
    chrome_sdl_rect(pixels, width, height, x, y, badge_w, 7 * glyph_scale, 0xff0e0e0cu);
    chrome_sdl_rect(
        pixels, width, height, x, y, badge_w, glyph_scale, 0xffff981fu);
    for( int c = 0; c < count; c++ )
    {
        uint16_t const bits = chrome_sdl_glyph(text[c]);
        for( int gy = 0; gy < 5; gy++ )
            for( int gx = 0; gx < 3; gx++ )
                if( bits & (1u << (14 - (gy * 3 + gx))) )
                    chrome_sdl_rect(
                        pixels,
                        width,
                        height,
                        x + (1 + c * 4 + gx) * glyph_scale,
                        y + (1 + gy) * glyph_scale,
                        glyph_scale,
                        glyph_scale,
                        0xffffffffu);
    }
}

static int
chrome_sdl_scale(struct ChromeSdl const* s)
{
    int width;

    assert(s);
    width = s->platform ? PlatformWindow_ChromeRailWidth(s->platform) : 0;
    width = (width + CHROME_SDL_RAIL_W / 2) / CHROME_SDL_RAIL_W;
    if( width < 1 )
        width = 1;
    if( width > 4 )
        width = 4;
    return width;
}

static int
chrome_sdl_visible_rows(struct ChromeSdl const* s)
{
    int const scale = chrome_sdl_scale(s);
    int const height = s->platform ? PlatformWindow_ChromeHeight(s->platform) : 0;
    int rows = height > 4 * scale ? (height - 4 * scale) / (40 * scale) : 0;
    return rows > 0 ? rows : 1;
}

static int
chrome_sdl_scroll_clamp(struct ChromeSdl* s, int scroll)
{
    int const max_scroll = s->rail.entry_count > chrome_sdl_visible_rows(s)
                               ? s->rail.entry_count - chrome_sdl_visible_rows(s)
                               : 0;
    if( scroll < 0 )
        return 0;
    if( scroll > max_scroll )
        return max_scroll;
    return scroll;
}

static int
chrome_sdl_selected_page_width(struct ChromeSdl const* s)
{
    int width = CHROME_SDL_PAGE_W;

    assert(s);
    for( int i = 0; i < s->rail.entry_count; i++ )
        if( s->rail.entries[i].plugin_index == s->rail.selected_entry &&
            s->rail.entries[i].preferred_width > 0 )
        {
            width = s->rail.entries[i].preferred_width;
            break;
        }
    if( width < 280 )
        width = 280;
    if( width > 480 )
        width = 480;
    return width;
}

static int
chrome_sdl_rail_entry_at(struct ChromeSdl* s, int y)
{
    int const scale = chrome_sdl_scale(s);
    int const top = 2 * scale;
    int visible;
    int index;

    if( y < top )
        return -1;
    visible = (y - top) / (40 * scale);
    index = s->rail_scroll + visible;
    if( visible < 0 || visible >= chrome_sdl_visible_rows(s) ||
        index < 0 || index >= s->rail.entry_count )
        return -1;
    return index;
}

static void
chrome_sdl_paint(struct ChromeSdl* s)
{
    struct ToriRSChromeSkin_Sprite const* body;
    int* pixels;
    int width;
    int height;
    int rail_w;
    int page_w;
    int rail_x;
    int scale;

    assert(s);
    if( !s->platform || !s->rail_live )
        return;
    pixels = PlatformWindow_ChromePixels(s->platform);
    width = PlatformWindow_ChromeWidth(s->platform);
    height = PlatformWindow_ChromeHeight(s->platform);
    rail_w = PlatformWindow_ChromeRailWidth(s->platform);
    page_w = PlatformWindow_ChromePageWidth(s->platform);
    if( !pixels || width <= 0 || height <= 0 || rail_w <= 0 )
        return;
    rail_x = width - rail_w;
    if( page_w > 0 )
    {
        if( s->open && s->attached && s->page_pixels &&
            s->page_width == page_w && s->page_height == height )
            for( int y = 0; y < height; y++ )
                memcpy(
                    &pixels[y * width],
                    &s->page_pixels[y * page_w],
                    (size_t)page_w * sizeof(*pixels));
        else
            chrome_sdl_rect(pixels, width, height, 0, 0, page_w, height, 0xff000000u);
    }

    body = ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY);
    for( int y = 0; y < height; y++ )
        for( int x = rail_x; x < width; x++ )
            pixels[y * width + x] = body && body->w > 0 && body->h > 0
                                        ? (int)body->argb[(y % body->h) * body->w +
                                                         ((x - rail_x) % body->w)]
                                        : (int)0xff5d5447u;
    chrome_sdl_rect(pixels, width, height, rail_x, 0, 1, height, 0xff0e0e0cu);

    scale = chrome_sdl_scale(s);
    s->rail_scroll = chrome_sdl_scroll_clamp(s, s->rail_scroll);
    for( int visible = 0; visible < chrome_sdl_visible_rows(s); visible++ )
    {
        int const index = s->rail_scroll + visible;
        struct ToriRSChromeRailEntry const* entry;
        struct ToriRSChromeSkin_Sprite const* fallback;
        uint32_t const* icon_pixels;
        int icon_w;
        int icon_h;
        int const y = 2 * scale + visible * 40 * scale;
        int const bx = rail_x + 3 * scale;
        int const by = y + 2 * scale;
        int const bw = rail_w - 6 * scale;
        int const bh = 36 * scale;
        uint32_t border = 0xff474745u;

        if( index >= s->rail.entry_count || by >= height )
            break;
        entry = &s->rail.entries[index];
        if( entry->plugin_index == s->rail.selected_entry )
            border = 0xffffff00u;
        if( index == s->rail_hover )
            border = 0xffff981fu;
        chrome_sdl_rect(pixels, width, height, bx, by, bw, bh, border);
        chrome_sdl_rect(
            pixels, width, height, bx + scale, by + scale,
            bw - 2 * scale, bh - 2 * scale,
            index == s->rail_pressed ? 0xff372e22u : 0xff0e0e0cu);

        fallback = ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PLUGIN_ICON);
        icon_pixels = fallback ? fallback->argb : NULL;
        icon_w = fallback ? fallback->w : 0;
        icon_h = fallback ? fallback->h : 0;
        if( entry->kind == TORIRS_CHROME_RAIL_ENTRY_PLUGIN &&
            entry->plugin_index >= 0 && entry->plugin_index < CHROME_SDL_ICON_PLUGIN_MAX &&
            s->icons[entry->plugin_index].width > 0 &&
            s->icons[entry->plugin_index].height > 0 )
        {
            struct ChromeSdlIcon const* icon = &s->icons[entry->plugin_index];
            icon_pixels = icon->argb;
            icon_w = icon->width;
            icon_h = icon->height;
        }
        if( icon_pixels )
        {
            int const side = 24 * scale;
            chrome_sdl_blit_nearest(
                pixels,
                width,
                height,
                rail_x + (rail_w - side) / 2,
                by + 4 * scale,
                side,
                side,
                icon_pixels,
                icon_w,
                icon_h);
        }
        if( entry->attention )
            chrome_sdl_rect(
                pixels, width, height,
                bx + bw - 5 * scale, by + 2 * scale,
                3 * scale, 3 * scale, 0xffffff00u);
        chrome_sdl_badge(
            pixels,
            width,
            height,
            bx + bw - scale,
            by + bh - scale,
            entry->badge,
            scale);
    }
    PlatformWindow_ChromePresent(s->platform);
}

static uint64_t
chrome_sdl_sequence_next(struct ChromeSdl* s)
{
    s->rail_sequence++;
    if( s->rail_sequence == 0 )
        s->rail_sequence++;
    return s->rail_sequence;
}

static void
chrome_sdl_queue_select(struct ChromeSdl* s, int entry, uint32_t generation)
{
    struct ToriRSChromeRailIntent intent;

    if( entry < 0 || entry >= s->rail.entry_count || generation == 0 )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
    intent.plugin_index = s->rail.entries[entry].plugin_index;
    intent.selection_generation = generation;
    intent.sequence = chrome_sdl_sequence_next(s);
    if( s->rail_pending_count >= CHROME_SDL_RAIL_QUEUE_MAX )
        s->rail_pending[CHROME_SDL_RAIL_QUEUE_MAX - 1] = intent;
    else
        s->rail_pending[s->rail_pending_count++] = intent;
}

static void
chrome_sdl_rail_sync(
    void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct ChromeSdl* s = user;
    int selected = -1;
    int visible;

    assert(s);
    assert(snapshot);
    if( !s->platform ||
        !PlatformWindow_ChromeRailOpen(
            s->platform, CHROME_SDL_RAIL_W, "Plugins") )
        return;
    s->rail = *snapshot;
    s->rail_live = 1;
    s->layout_pending = 1;
    if( s->open && s->attached )
        (void)PlatformWindow_ChromeSetPageWidth(
            s->platform, chrome_sdl_selected_page_width(s));
    visible = chrome_sdl_visible_rows(s);
    for( int i = 0; i < s->rail.entry_count; i++ )
        if( s->rail.entries[i].plugin_index == s->rail.selected_entry )
        {
            selected = i;
            break;
        }
    if( selected >= 0 )
    {
        if( selected < s->rail_scroll )
            s->rail_scroll = selected > 0 ? selected - 1 : 0;
        else if( selected >= s->rail_scroll + visible )
            s->rail_scroll = selected - visible + 1;
    }
    s->rail_scroll = chrome_sdl_scroll_clamp(s, s->rail_scroll);
    s->rail_hover = -1;
    s->rail_pressed = -1;
    chrome_sdl_paint(s);
}

static void
chrome_sdl_rail_icon(void* user, struct ToriRSChromeRailIcon const* update)
{
    struct ChromeSdl* s = user;
    struct ChromeSdlIcon* icon;
    int pixels;

    assert(s);
    assert(update);
    if( update->plugin_index < 0 ||
        update->plugin_index >= CHROME_SDL_ICON_PLUGIN_MAX ||
        update->width < 0 || update->height < 0 ||
        update->width > TORIRS_CHROME_RAIL_ICON_SIDE_MAX ||
        update->height > TORIRS_CHROME_RAIL_ICON_SIDE_MAX )
        return;
    icon = &s->icons[update->plugin_index];
    if( icon->revision == update->revision )
        return;
    pixels = update->width * update->height;
    icon->revision = update->revision;
    icon->width = update->width;
    icon->height = update->height;
    if( pixels > 0 )
        memcpy(icon->argb, update->argb, (size_t)pixels * sizeof(icon->argb[0]));
    chrome_sdl_paint(s);
}

static void
chrome_sdl_rail_input(struct ChromeSdl* s)
{
    struct PlatformWindow_AuxInput input;
    int repaint = 0;
    int hover;

    assert(s);
    if( !s->platform ||
        !PlatformWindow_ChromeTakeRailInput(s->platform, &input) )
        return;
    if( input.resized )
    {
        s->rail_scroll = chrome_sdl_scroll_clamp(s, s->rail_scroll);
        s->layout_pending = 1;
        repaint = 1;
    }
    if( input.wheel )
    {
        int const next = chrome_sdl_scroll_clamp(
            s, s->rail_scroll - input.wheel * 3);
        if( next != s->rail_scroll )
        {
            s->rail_scroll = next;
            repaint = 1;
        }
    }
    hover = input.mouse_x >= 0 && input.mouse_y >= 0
                ? chrome_sdl_rail_entry_at(s, input.mouse_y)
                : -1;
    if( hover != s->rail_hover )
    {
        s->rail_hover = hover;
        repaint = 1;
    }
    if( input.mouse_down )
    {
        s->rail_pressed = hover;
        s->rail_press_generation = s->rail.selection_generation;
        repaint = 1;
    }
    if( input.mouse_up )
    {
        if( hover >= 0 && hover == s->rail_pressed )
            chrome_sdl_queue_select(s, hover, s->rail_press_generation);
        s->rail_pressed = -1;
        s->rail_press_generation = 0;
        repaint = 1;
    }
    if( repaint )
        chrome_sdl_paint(s);
}

static int
chrome_sdl_rail_poll(
    void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct ChromeSdl* s = user;
    int count;

    assert(s);
    if( !out || max <= 0 )
        return 0;
    chrome_sdl_rail_input(s);
    count = s->rail_pending_count < max ? s->rail_pending_count : max;
    for( int i = 0; i < count; i++ )
        out[i] = s->rail_pending[i];
    for( int i = count; i < s->rail_pending_count; i++ )
        s->rail_pending[i - count] = s->rail_pending[i];
    s->rail_pending_count -= count;
    if( count < max && s->layout_pending && s->rail.selection_generation != 0 )
    {
        struct ToriRSChromeRailIntent* layout = &out[count++];
        int const density = s->platform
                                ? PlatformWindow_PixelDensity(s->platform)
                                : 1;
        int width = 0;
        int height = 0;

        if( s->open )
        {
            width = s->attached
                        ? PlatformWindow_ChromePageWidth(s->platform)
                        : PlatformWindow_AuxWidth(s->platform);
            height = s->attached
                         ? PlatformWindow_ChromeHeight(s->platform)
                         : PlatformWindow_AuxHeight(s->platform);
        }
        memset(layout, 0, sizeof(*layout));
        layout->kind = TORIRS_CHROME_RAIL_INTENT_LAYOUT;
        layout->plugin_index = s->rail.selected_entry;
        layout->selection_generation = s->rail.selection_generation;
        layout->sequence = chrome_sdl_sequence_next(s);
        layout->width = density > 0 ? width / density : width;
        layout->height = density > 0 ? height / density : height;
        layout->scale_milli = (density > 0 ? density : 1) * 1000;
        layout->size_class = layout->width < 320 ? 0 : (layout->width >= 480 ? 2 : 1);
        layout->visible = s->open && s->rail.expanded ? 1 : 0;
        layout->game_visible = 1;
        s->layout_pending = 0;
    }
    return count;
}

/*
 * The point test SDL's hit test ends up in.
 *
 * Everything it touches is the published snapshot: a dozen rectangles and two
 * counts. It must stay that way -- this runs on the pump's stack while the
 * window manager is deciding what a press is.
 */
static int
chrome_sdl_drag_at(void* user, int x, int y)
{
    struct ChromeSdl* s = user;

    if( !s )
        return 0;
    return ToriRSChromeDragRegion_Contains(&s->drag, x, y);
}

static void
chrome_sdl_set_drag_region(void* user, struct ToriRSChromeDragRegion const* region)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(region);
    s->drag = *region;
}

/** The wish, with the env var over the top of it -- the precedence
 *  TORIRS_CHROME_EXECUTOR and TORIRS_CHROME_THEME already set. */
static int
chrome_sdl_borderless_wanted(void)
{
    char const* env = getenv("TORIRS_CHROME_BORDERLESS");
    if( env && env[0] )
        return env[0] != '0';
    return g_chrome_sdl_want_borderless != 0;
}

static int
chrome_sdl_begin(void* user)
{
    struct ChromeSdl* s = user;
    int const detached = getenv("TORIRS_CHROME_DETACHED") != NULL;
    int const page_width = chrome_sdl_selected_page_width(s);

    assert(s);
    if( !s->platform )
        return 0;
    if( !PlatformWindow_ChromeRailOpen(
            s->platform, CHROME_SDL_RAIL_W, "Plugins") )
        return 0;
    if( detached )
    {
        if( !PlatformWindow_AuxOpen(
                s->platform, page_width, CHROME_SDL_H, "Plugins") )
            return 0;
        s->attached = 0;
    }
    else
    {
        if( !PlatformWindow_ChromeOpen(
                s->platform,
                page_width,
                CHROME_SDL_H,
                "Plugins") )
            return 0;
        s->attached = 1;
    }
    s->open = 1;
    s->panel = -1;
    s->close_pending = 0;
    s->borderless = 0;
    s->layout_pending = 1;
    /* Whatever the last window was told about is gone with it. A region left
     * standing would be a band of the NEW window swallowing presses over
     * whatever the old one had a strip at. */
    memset(&s->drag, 0, sizeof(s->drag));

    if( !s->attached && chrome_sdl_borderless_wanted() )
    {
        /*
         * The provider goes on before the frame comes off, and the frame is
         * allowed not to come off: PlatformWindow_AuxSetBorderless refuses on a
         * video driver with no hit test, because a frameless window nobody can
         * move is worse than the frame it was asked to hide. The window is
         * usable either way -- what changes is which title bar drags it.
         */
        PlatformWindow_AuxSetDragHandleProvider(s->platform, chrome_sdl_drag_at, s);
        s->borderless = PlatformWindow_AuxSetBorderless(s->platform, true) ? 1 : 0;

        /*
         * Once per ANSWER, not once per open -- the executor comes down with
         * the window, so every show runs this, and a line per open makes a
         * session that toggles the window say the same sentence twenty times.
         * The same rule the executor's own bind line follows.
         *
         * Only the success is said here. A refusal already printed its reason
         * from the platform, which is the layer that knows what the driver
         * said, and repeating it would be two lines for one fact.
         */
        if( s->borderless && !g_chrome_sdl_reported_borderless )
        {
            g_chrome_sdl_reported_borderless = 1;
            fprintf(
                stderr,
                "chrome: plugin window has no OS frame; its title bar and tab strip move it\n");
        }
    }
    chrome_sdl_paint(s);
    return 1;
}

static void
chrome_sdl_end(void* user)
{
    struct ChromeSdl* s = user;

    assert(s);
    if( !s->open )
        return;
    if( s->attached )
        PlatformWindow_ChromeClose(s->platform);
    else
        PlatformWindow_AuxClose(s->platform);
    s->open = 0;
    s->attached = 0;
    s->borderless = 0;
    s->rail_hover = -1;
    s->rail_pressed = -1;
    s->rail_press_generation = 0;
    s->layout_pending = 1;
    chrome_sdl_paint(s);
}

static void
chrome_sdl_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(cmd);
    /* Almost nothing: a surface executor draws the chrome's own display list,
     * so the widget-level stream has nothing to tell it. See the two kinds of
     * executor in the header.
     *
     * The exception is WHICH PANEL is in the window. A close coming back the
     * other way has to name one -- an intent addresses the model, and "the
     * panel that was showing" is not something the model can infer -- and the
     * only place that fact crosses this seam is PANEL_OPEN. */
    if( cmd->kind == TORIRS_CHROME_CMD_PANEL_OPEN )
        s->panel = cmd->panel;
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE && cmd->panel == s->panel )
        s->panel = -1;
}

/*
 * Rasterise the display list into the aux window's buffer.
 *
 * Deliberately NOT a second implementation of prim drawing: it hands the same
 * list to the same ToriRS_Frame translator and the same software backend the
 * in-canvas path uses. A second rasteriser here would be a second set of
 * rounding, a second baseline convention, and a second place for the chrome to
 * be almost right.
 */
static void
chrome_sdl_present(void* user, struct ToriRSChromePrim const* prims, int count)
{
    struct ChromeSdl* s = user;
    int* pixels;
    int w;
    int h;

    assert(s);
    if( !s->open || !prims )
        return;

    pixels = s->attached ? s->page_pixels : PlatformWindow_AuxPixels(s->platform);
    w = s->attached ? PlatformWindow_ChromePageWidth(s->platform)
                    : PlatformWindow_AuxWidth(s->platform);
    h = s->attached ? PlatformWindow_ChromeHeight(s->platform)
                    : PlatformWindow_AuxHeight(s->platform);
    if( w <= 0 || h <= 0 )
        return;
    if( s->attached )
    {
        if( !chrome_sdl_page_ensure(s, w, h) )
            return;
        pixels = s->page_pixels;
    }
    if( !pixels )
        return;

    /*
     * Cleared, unlike the game canvas.
     *
     * The world viewport is never cleared because the 3D pass covers every
     * pixel of it; this window has nothing behind the chrome at all, so
     * whatever a panel stopped covering when it moved would otherwise smear.
     */
    memset(pixels, 0, (size_t)w * (size_t)h * sizeof(*pixels));

    if( s->rasterise )
        s->rasterise(s->rasterise_user, pixels, w, h, prims, count);
    if( s->attached )
        chrome_sdl_paint(s);
    else
        PlatformWindow_AuxPresent(s->platform);
}

/*
 * The window's size, which is what makes the chrome fill it.
 *
 * This window holds the panel and nothing else -- no game canvas behind it, no
 * strip beside it -- so the panel is stretched over the whole of it rather than
 * floating at the coordinates it uses in the canvas, where it had something to
 * float over. Answering this is the whole of that opt-in; the rule itself is
 * ToriRSChromeSync_FillSurface's, shared with every other window-owning
 * presentation.
 *
 * Zero while the window is down, so a closed aux window leaves the panel's
 * geometry alone instead of collapsing it to nothing.
 */
static int
chrome_sdl_surface_size(void* user, int* out_w, int* out_h)
{
    struct ChromeSdl* s = user;
    int w;
    int h;

    assert(s);
    assert(out_w);
    assert(out_h);
    if( !s->open )
        return 0;
    w = s->attached ? PlatformWindow_ChromePageWidth(s->platform)
                    : PlatformWindow_AuxWidth(s->platform);
    h = s->attached ? PlatformWindow_ChromeHeight(s->platform)
                    : PlatformWindow_AuxHeight(s->platform);
    if( w <= 0 || h <= 0 )
        return 0;
    *out_w = w;
    *out_h = h;
    return 1;
}

/*
 * The editing-key values platform/ reports and the ones ui/ understands are the
 * same numbers, restated on each side of a layer boundary neither may cross.
 * Pinned here, where both headers are already included, so a value added to one
 * enum and not the other fails to COMPILE rather than silently mapping Home to
 * Delete.
 */
_Static_assert((int)PLATFORM_AUX_KEY_NONE == (int)TORIRS_CHROME_KEY_NONE, "aux key: none");
_Static_assert(
    (int)PLATFORM_AUX_KEY_BACKSPACE == (int)TORIRS_CHROME_KEY_BACKSPACE,
    "aux key: bksp");
_Static_assert((int)PLATFORM_AUX_KEY_DELETE == (int)TORIRS_CHROME_KEY_DELETE, "aux key: del");
_Static_assert((int)PLATFORM_AUX_KEY_LEFT == (int)TORIRS_CHROME_KEY_LEFT, "aux key: left");
_Static_assert((int)PLATFORM_AUX_KEY_RIGHT == (int)TORIRS_CHROME_KEY_RIGHT, "aux key: right");
_Static_assert((int)PLATFORM_AUX_KEY_HOME == (int)TORIRS_CHROME_KEY_HOME, "aux key: home");
_Static_assert((int)PLATFORM_AUX_KEY_END == (int)TORIRS_CHROME_KEY_END, "aux key: end");
_Static_assert((int)PLATFORM_AUX_KEY_ENTER == (int)TORIRS_CHROME_KEY_ENTER, "aux key: enter");
_Static_assert((int)PLATFORM_AUX_KEY_ESCAPE == (int)TORIRS_CHROME_KEY_ESCAPE, "aux key: esc");
_Static_assert((int)PLATFORM_AUX_KEY_UP == (int)TORIRS_CHROME_KEY_UP, "aux key: up");
_Static_assert((int)PLATFORM_AUX_KEY_DOWN == (int)TORIRS_CHROME_KEY_DOWN, "aux key: down");

static int
chrome_sdl_surface_input(void* user, struct ToriRSChromeSurfaceInput* out)
{
    struct ChromeSdl* s = user;
    struct PlatformWindow_AuxInput aux;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;

    /*
     * A close from the window's own title bar drops the OS window at once --
     * continuing to present into it would be drawing into a window that no
     * longer exists -- and is REPORTED, so the model hides the panel and the
     * host learns the window went away.
     *
     * Both halves are needed. Dropping it silently leaves the host convinced
     * the window is still up, and its toggle then spends a press "closing"
     * something the user already closed. Reporting it without dropping it is
     * the GDI rule, which can afford to wait for the model because its window
     * is still there to wait in; this one is not.
     */
    if( !s->attached && PlatformWindow_AuxTakeCloseRequest(s->platform) )
    {
        PlatformWindow_AuxClose(s->platform);
        s->open = 0;
        s->borderless = 0;
        s->close_pending = 1;
        return 0;
    }

    if( !(s->attached ? PlatformWindow_ChromeTakeInput(s->platform, &aux)
                      : PlatformWindow_AuxTakeInput(s->platform, &aux)) )
        return 0;

    /* The platform's POD across to the chrome's; see the _Static_asserts. */
    memset(out, 0, sizeof(*out));
    out->mouse_x = aux.mouse_x;
    out->mouse_y = aux.mouse_y;
    out->mouse_down = aux.mouse_down;
    out->mouse_up = aux.mouse_up;
    out->wheel = aux.wheel;
    out->edit_key = aux.edit_key;
    out->resized = aux.resized;
    out->width = aux.width;
    out->height = aux.height;
    memcpy(out->text, aux.text, sizeof(out->text) < sizeof(aux.text) ? sizeof(out->text)
                                                                    : sizeof(aux.text));
    out->text[sizeof(out->text) - 1] = '\0';

    /* A resize is applied to the SURFACE here rather than left to the caller:
     * the next present writes into a buffer that must already be the window's
     * size, and the chrome does not care -- it lays out where it was put.
     *
     * The platform reports the new size in PIXELS (the drawable), which is
     * what the surface is measured in, so this is a straight handover -- and
     * it also fires when only the DENSITY changed, which is a window dragged
     * to a display of another kind. */
    if( out->resized )
    {
        if( !s->attached )
            PlatformWindow_AuxResize(s->platform, out->width, out->height);
        s->layout_pending = 1;
    }
    return 1;
}

/*
 * The window's X, on its way to the model.
 *
 * The only intent this executor has: every other gesture is a pointer in the
 * chrome's own space, which surface_input hands over raw for the chrome to hit
 * test itself. A window closing is the one thing that happens to the
 * PRESENTATION rather than inside it, so it is the one thing that has to be
 * said in the model's own vocabulary.
 */
static int
chrome_sdl_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(out);
    if( max <= 0 || !s->close_pending )
        return 0;
    s->close_pending = 0;
    if( s->panel < 0 )
        return 0;

    memset(out, 0, sizeof(*out));
    out[0].kind = TORIRS_CHROME_INTENT_CLOSE;
    out[0].panel = s->panel;
    out[0].widget = -1;
    s->panel = -1;
    return 1;
}

struct ToriRSChromeExec
ToriRSChromeExec_Sdl(void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    free(g_chrome_sdl.page_pixels);
    memset(&g_chrome_sdl, 0, sizeof(g_chrome_sdl));
    g_chrome_sdl.platform = platform;
    g_chrome_sdl.rasterise = rasterise;
    g_chrome_sdl.rasterise_user = rasterise_user;
    g_chrome_sdl.rail_hover = -1;
    g_chrome_sdl.rail_pressed = -1;

    exec.user = &g_chrome_sdl;
    exec.begin = chrome_sdl_begin;
    exec.apply = chrome_sdl_apply;
    exec.poll = chrome_sdl_poll;
    exec.end = chrome_sdl_end;
    exec.present = chrome_sdl_present;
    exec.surface_input = chrome_sdl_surface_input;
    exec.surface_size = chrome_sdl_surface_size;
    exec.rail_sync = chrome_sdl_rail_sync;
    exec.rail_icon = chrome_sdl_rail_icon;
    exec.rail_poll = chrome_sdl_rail_poll;
    /*
     * Offered unconditionally, not only when the frame is off.
     *
     * Whether this window ends up frameless is not known until begin() has
     * asked the video driver, and the entry is what a host looks at to decide
     * whether to publish at all. Answering "no handles" by never being told
     * about them is the same answer as being told an empty region, at the cost
     * of a table entry that changes under the host.
     */
    exec.set_drag_region = chrome_sdl_set_drag_region;
    exec.is_surface = 1;
    return exec;
}

int
ToriRSChromeExecSdl_IsOpen(void)
{
    return g_chrome_sdl.open;
}

void
ToriRSChromeExecSdl_SetBorderless(int borderless)
{
    g_chrome_sdl_want_borderless = borderless ? 1 : 0;
}

int
ToriRSChromeExecSdl_IsBorderless(void)
{
    return g_chrome_sdl.borderless;
}
