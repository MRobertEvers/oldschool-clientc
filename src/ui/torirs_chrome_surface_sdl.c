/*
 * The SDL desktop's plugin chrome when there is no embedded browser (Linux):
 * the RuneLite-style rail beside the game, and the selected page beside it or
 * in its own window.
 *
 * Not an executor KIND. The page is the internal BUFFER page -- ToriRSChrome's
 * own retained panel, laid out, hit-tested and drawn by the same display list
 * that draws it in the canvas -- and this vtable adds only the two things the
 * canvas cannot give it:
 *
 *   - the RAIL (rail_sync / rail_icon / rail_poll), drawn here into the
 *     platform's chrome strip (PlatformWindow_ChromeRailOpen, opened by
 *     PlatformWindow_PluginChromeShow) from the copied rail snapshot, skinned
 *     with the baked chrome pieces the browser rail uses; a press is queued as
 *     the same TORIRS_CHROME_RAIL_INTENT_SELECT the browser rail sends, so
 *     selecting, collapsing and replacing go through app_plugin_rail_select
 *     exactly as they do there;
 *
 *   - a SURFACE for the page (surface_size / surface_input / surface_present):
 *     the strip beside the rail (PlatformWindow_ChromeOpen), or, popped out,
 *     the platform's aux window (PlatformWindow_AuxOpen). The App rasterises
 *     the page's display list into it with the software UI path and feeds the
 *     surface's pointer and keys to the model.
 *
 * The pop-out control is the rail's: a baked pop-out mark at the foot of the
 * rail while a page is open (the dock mark while it is out). Closing the aux
 * window docks the page back beside the rail. One page at a time; the rail
 * never moves.
 *
 * Compiled only for an SDL desktop without TORIRS_CHROME_EXEC_BROWSER_AVAILABLE
 * (TORIRS_CHROME_SURFACE_SDL, platform.mk).
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_metrics.h"
#include "torirs_chrome_skin.h"

#include "../platform/platform_window.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The page's opening width beside the rail, and the aux window's, in window
 * points: the browser hosts' WINBROWSER_PAGE_WIDTH. */
#define SURFACE_SDL_PAGE_W 320
#define SURFACE_SDL_PAGE_H 480
#define SURFACE_SDL_RAIL_QUEUE_MAX 32
#define SURFACE_SDL_ICON_MAX TORIRS_CHROME_RAIL_ENTRY_MAX

/* Rail geometry in the authored 1x units of the browser rail
 * (modern.css .tpc-rail): the 6px nine-slice frame, 36px rows, 24px icons. */
#define SURFACE_SDL_FRAME 6
#define SURFACE_SDL_ROW_H 36
#define SURFACE_SDL_ICON 24
#define SURFACE_SDL_POPOUT 16

/* The browser rail's colours (modern.css): the label orange outlines a hovered
 * entry, the pressed stone is darker under its icon. */
#define SURFACE_SDL_HOVER 0xffff981fu
#define SURFACE_SDL_PRESSED 0x60000000u
#define SURFACE_SDL_ATTENTION 0xffffff00u
#define SURFACE_SDL_STONE 0xff3e3529u

struct SurfaceSdlIcon
{
    uint32_t revision;
    int width;
    int height;
    uint32_t argb[TORIRS_CHROME_RAIL_ICON_PIXELS_MAX];
};

struct SurfaceSdl
{
    struct PlatformWindow* platform;

    struct ToriRSChromeRailSnapshot rail;
    int rail_live;
    int rail_hover;
    int rail_pressed;
    /* The press landed on the pop-out mark rather than an entry. */
    int popout_hover;
    int popout_pressed;
    int rail_scroll;
    uint32_t rail_press_generation;
    uint64_t rail_sequence;
    struct ToriRSChromeRailIntent rail_pending[SURFACE_SDL_RAIL_QUEUE_MAX];
    int rail_pending_count;
    /* The strip size the rail was last painted for: a strip the platform
     * re-made (a window resize, a page opening) comes back blank. */
    int painted_w;
    int painted_h;

    /* TORIRS_SIM_CHROME_POPOUT's countdown; 0 when not driven. */
    int sim_popout_polls;
    /* begin() succeeded: a page is up, beside the rail or popped out. */
    int open;
    /* The page is in the aux window rather than the strip. */
    int popped;

    /* The last page the App presented, kept so a rail repaint (which rewrites
     * the whole strip) and a pop-out toggle can put it back without waiting
     * for the App to rasterise again. */
    uint32_t* page_pixels;
    int page_w;
    int page_h;

    struct SurfaceSdlIcon icons[SURFACE_SDL_ICON_MAX];
};

/* One rail, one page: the same single instance the browser executor keeps. */
static struct SurfaceSdl g_surface_sdl;

/* ---- pixels -------------------------------------------------------------- */

static uint32_t
surface_sdl_over(uint32_t dst, uint32_t src)
{
    unsigned const a = src >> 24;
    unsigned const inv = 255u - a;
    unsigned const r = (((src >> 16) & 0xffu) * a + ((dst >> 16) & 0xffu) * inv) / 255u;
    unsigned const g = (((src >> 8) & 0xffu) * a + ((dst >> 8) & 0xffu) * inv) / 255u;
    unsigned const b = ((src & 0xffu) * a + (dst & 0xffu) * inv) / 255u;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

/** Fill (or, with alpha below 255, blend) a clipped box of the strip. */
static void
surface_sdl_rect(
    int* pixels, int stride, int clip_x0, int clip_x1, int height,
    int x, int y, int w, int h, uint32_t color)
{
    int x1 = x + w;
    int y1 = y + h;
    unsigned const a = color >> 24;

    if( x < clip_x0 )
        x = clip_x0;
    if( y < 0 )
        y = 0;
    if( x1 > clip_x1 )
        x1 = clip_x1;
    if( y1 > height )
        y1 = height;
    for( int py = y; py < y1; py++ )
        for( int px = x; px < x1; px++ )
        {
            int* at = &pixels[py * stride + px];
            *at = a == 255u ? (int)color : (int)surface_sdl_over((uint32_t)*at, color);
        }
}

/** Nearest-neighbour blit of one ARGB image, alpha-blended, clipped to the
 *  rail's columns. */
static void
surface_sdl_blit(
    int* pixels, int stride, int clip_x0, int clip_x1, int height,
    int x, int y, int w, int h,
    uint32_t const* src, int src_w, int src_h)
{
    assert(src);
    if( w <= 0 || h <= 0 || src_w <= 0 || src_h <= 0 )
        return;
    for( int py = 0; py < h; py++ )
    {
        int const dy = y + py;
        int const sy = py * src_h / h;
        if( dy < 0 || dy >= height )
            continue;
        for( int px = 0; px < w; px++ )
        {
            int const dx = x + px;
            uint32_t const source = src[sy * src_w + px * src_w / w];
            if( dx < clip_x0 || dx >= clip_x1 || (source >> 24) == 0 )
                continue;
            pixels[dy * stride + dx] =
                (source >> 24) == 255u
                    ? (int)source
                    : (int)surface_sdl_over((uint32_t)pixels[dy * stride + dx], source);
        }
    }
}

/** One skin piece tiled over a box at `scale`, as CSS repeats it. */
static void
surface_sdl_tile(
    int* pixels, int stride, int clip_x0, int clip_x1, int height,
    int x, int y, int w, int h, int slot, int scale, uint32_t fallback)
{
    struct ToriRSChromeSkin_Sprite const* sprite = ToriRSChromeSkin_ForSlot(slot);

    if( !sprite || sprite->w <= 0 || sprite->h <= 0 )
    {
        surface_sdl_rect(pixels, stride, clip_x0, clip_x1, height, x, y, w, h, fallback);
        return;
    }
    for( int ty = y; ty < y + h; ty += sprite->h * scale )
        for( int tx = x; tx < x + w; tx += sprite->w * scale )
        {
            int const tw = tx + sprite->w * scale > x + w ? x + w - tx : sprite->w * scale;
            int const th = ty + sprite->h * scale > y + h ? y + h - ty : sprite->h * scale;
            /* A partial tile is the sprite's top-left corner, not the whole
             * sprite squeezed: blit the full tile clipped to the box. */
            surface_sdl_blit(
                pixels, stride, tx < clip_x0 ? clip_x0 : tx,
                tx + tw < clip_x1 ? tx + tw : clip_x1,
                ty + th < height ? ty + th : height,
                tx, ty, sprite->w * scale, sprite->h * scale,
                sprite->argb, sprite->w, sprite->h);
        }
}

/* A 3x5 pixel font for the two letters a plugin without authored art is
 * known by, and for badges: the rail is 30px of content wide, and a baked
 * glyph face would need the scene. */
static uint16_t
surface_sdl_glyph(char ch)
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
    return 0;
}

/** Up to three glyphs on a black plate, right-aligned at (right, bottom). */
static void
surface_sdl_caption(
    int* pixels, int stride, int clip_x0, int clip_x1, int height,
    int right, int bottom, char const* text, int scale, uint32_t ink)
{
    char shown[4];
    int count = 0;
    int plate_w;
    int x;
    int y;

    for( int i = 0; text[i] && count < 3; i++ )
        if( surface_sdl_glyph(text[i]) )
            shown[count++] = text[i];
    if( count == 0 )
        return;
    plate_w = (count * 4 + 1) * scale;
    x = right - plate_w;
    y = bottom - 7 * scale;
    surface_sdl_rect(
        pixels, stride, clip_x0, clip_x1, height, x, y, plate_w, 7 * scale, 0xc0000000u);
    for( int c = 0; c < count; c++ )
    {
        uint16_t const bits = surface_sdl_glyph(shown[c]);
        for( int gy = 0; gy < 5; gy++ )
            for( int gx = 0; gx < 3; gx++ )
                if( bits & (1u << (14 - (gy * 3 + gx))) )
                    surface_sdl_rect(
                        pixels, stride, clip_x0, clip_x1, height,
                        x + (1 + c * 4 + gx) * scale, y + (1 + gy) * scale,
                        scale, scale, ink);
    }
}

/* ---- rail geometry ------------------------------------------------------- */

/** Whole drawable pixels per authored rail pixel: 2 on a HiDPI strip. */
static int
surface_sdl_scale(struct SurfaceSdl const* s)
{
    int const rail_w = PlatformWindow_ChromeRailWidth(s->platform);
    int scale = (rail_w + TORIRS_CHROME_M_RAIL_W / 2) / TORIRS_CHROME_M_RAIL_W;
    return scale < 1 ? 1 : scale;
}

/** The pop-out mark's box, rail-local; zero-sized while no page is up. */
static void
surface_sdl_popout_box(struct SurfaceSdl const* s, int* x, int* y, int* side)
{
    int const scale = surface_sdl_scale(s);
    int const rail_w = PlatformWindow_ChromeRailWidth(s->platform);
    int const height = PlatformWindow_ChromeHeight(s->platform);

    *side = s->open ? SURFACE_SDL_POPOUT * scale : 0;
    *x = (rail_w - SURFACE_SDL_POPOUT * scale) / 2;
    *y = height - (SURFACE_SDL_FRAME + 4 + SURFACE_SDL_POPOUT) * scale;
}

static int
surface_sdl_visible_rows(struct SurfaceSdl const* s)
{
    int const scale = surface_sdl_scale(s);
    int room = PlatformWindow_ChromeHeight(s->platform) - 2 * SURFACE_SDL_FRAME * scale;
    int rows;

    /* The pop-out mark has a row of its own at the foot while a page is up. */
    if( s->open )
        room -= (SURFACE_SDL_POPOUT + 8) * scale;
    rows = room / (SURFACE_SDL_ROW_H * scale);
    return rows > 0 ? rows : 1;
}

static int
surface_sdl_scroll_clamp(struct SurfaceSdl const* s, int scroll)
{
    int const visible = surface_sdl_visible_rows(s);
    int const max_scroll = s->rail.entry_count > visible ? s->rail.entry_count - visible : 0;

    if( scroll < 0 )
        return 0;
    return scroll > max_scroll ? max_scroll : scroll;
}

/** The rail entry under a rail-local point, or -1. */
static int
surface_sdl_entry_at(struct SurfaceSdl const* s, int x, int y)
{
    int const scale = surface_sdl_scale(s);
    int const top = SURFACE_SDL_FRAME * scale;
    int row;

    if( x < 0 || x >= PlatformWindow_ChromeRailWidth(s->platform) || y < top )
        return -1;
    row = (y - top) / (SURFACE_SDL_ROW_H * scale);
    if( row >= surface_sdl_visible_rows(s) || s->rail_scroll + row >= s->rail.entry_count )
        return -1;
    return s->rail_scroll + row;
}

static int
surface_sdl_popout_at(struct SurfaceSdl const* s, int x, int y)
{
    int bx;
    int by;
    int side;

    surface_sdl_popout_box(s, &bx, &by, &side);
    return side > 0 && x >= bx && x < bx + side && y >= by && y < by + side;
}

/* ---- painting ------------------------------------------------------------ */

/** The page part of the strip: the App's last picture, or black before one. */
static void
surface_sdl_paint_page(struct SurfaceSdl* s, int* pixels, int width, int height)
{
    int const page_w = PlatformWindow_ChromePageWidth(s->platform);

    if( page_w <= 0 )
        return;
    if( s->open && !s->popped && s->page_pixels && s->page_w == page_w &&
        s->page_h == height )
        for( int y = 0; y < height; y++ )
            memcpy(
                &pixels[y * width], &s->page_pixels[y * page_w],
                (size_t)page_w * sizeof(*pixels));
    else
        surface_sdl_rect(pixels, width, 0, page_w, height, 0, 0, page_w, height, 0xff000000u);
}

/*
 * The rail, as the browser draws it: the stone PanelBody inside the
 * interfaces' nine-slice frame, the icons straight on the stone, the selected
 * entry pressed darker, the hovered one outlined in the label orange.
 */
static void
surface_sdl_paint_rail(struct SurfaceSdl* s, int* pixels, int width, int height)
{
    int const rail_w = PlatformWindow_ChromeRailWidth(s->platform);
    int const x0 = width - rail_w;
    int const scale = surface_sdl_scale(s);
    int const frame = SURFACE_SDL_FRAME * scale;
    int const visible = surface_sdl_visible_rows(s);
    int bx;
    int by;
    int side;

    surface_sdl_tile(
        pixels, width, x0, width, height, x0, 0, rail_w, height,
        TORIRS_CHROME_SKIN_PANEL_BODY, scale, SURFACE_SDL_STONE);
    /* The nine-slice frame, laid inside the rail's box as the page lays it. */
    surface_sdl_tile(pixels, width, x0, width, height, x0, 0, rail_w, frame,
        TORIRS_CHROME_SKIN_FRAME_TOP, scale, 0xff0e0e0cu);
    surface_sdl_tile(pixels, width, x0, width, height, x0, height - frame, rail_w, frame,
        TORIRS_CHROME_SKIN_FRAME_BOTTOM, scale, 0xff0e0e0cu);
    surface_sdl_tile(pixels, width, x0, width, height, x0, 0, frame, height,
        TORIRS_CHROME_SKIN_FRAME_LEFT, scale, 0xff0e0e0cu);
    surface_sdl_tile(pixels, width, x0, width, height, width - frame, 0, frame, height,
        TORIRS_CHROME_SKIN_FRAME_RIGHT, scale, 0xff0e0e0cu);
    {
        static int const corners[4] = {
            TORIRS_CHROME_SKIN_FRAME_TOP_LEFT, TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT,
            TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT, TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT,
        };
        for( int c = 0; c < 4; c++ )
        {
            struct ToriRSChromeSkin_Sprite const* sprite = ToriRSChromeSkin_ForSlot(corners[c]);
            if( !sprite )
                continue;
            surface_sdl_blit(
                pixels, width, x0, width, height,
                (c & 1) ? width - sprite->w * scale : x0,
                (c & 2) ? height - sprite->h * scale : 0,
                sprite->w * scale, sprite->h * scale, sprite->argb, sprite->w, sprite->h);
        }
    }

    s->rail_scroll = surface_sdl_scroll_clamp(s, s->rail_scroll);
    for( int row = 0; row < visible; row++ )
    {
        int const index = s->rail_scroll + row;
        struct ToriRSChromeRailEntry const* entry;
        uint32_t const* icon = NULL;
        int icon_w = 0;
        int icon_h = 0;
        int authored = 0;
        int const cy = frame + row * SURFACE_SDL_ROW_H * scale;
        int const cx = x0 + frame;
        int const cw = rail_w - 2 * frame;
        int const ch = SURFACE_SDL_ROW_H * scale;

        if( index >= s->rail.entry_count )
            break;
        entry = &s->rail.entries[index];
        if( entry->plugin_index == s->rail.selected_entry && s->rail.expanded )
            surface_sdl_rect(pixels, width, x0, width, height, cx, cy, cw, ch, SURFACE_SDL_PRESSED);
        if( index == s->rail_pressed )
            surface_sdl_rect(pixels, width, x0, width, height, cx, cy, cw, ch, SURFACE_SDL_PRESSED);
        if( entry->kind == TORIRS_CHROME_RAIL_ENTRY_PLUGIN && entry->plugin_index >= 0 &&
            entry->plugin_index < SURFACE_SDL_ICON_MAX &&
            s->icons[entry->plugin_index].width > 0 && s->icons[entry->plugin_index].height > 0 )
        {
            icon = s->icons[entry->plugin_index].argb;
            icon_w = s->icons[entry->plugin_index].width;
            icon_h = s->icons[entry->plugin_index].height;
            authored = 1;
        }
        else
        {
            struct ToriRSChromeSkin_Sprite const* wrench =
                ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PLUGIN_ICON);
            if( wrench )
            {
                icon = wrench->argb;
                icon_w = wrench->w;
                icon_h = wrench->h;
            }
        }
        if( icon )
            surface_sdl_blit(
                pixels, width, x0, width, height,
                cx + (cw - SURFACE_SDL_ICON * scale) / 2, cy + (ch - SURFACE_SDL_ICON * scale) / 2,
                SURFACE_SDL_ICON * scale, SURFACE_SDL_ICON * scale, icon, icon_w, icon_h);
        /* A plugin with no art of its own wears the shared wrench, so its
         * title's first letters tell it from the next one -- the title the
         * browser rail shows as the tooltip. Manage Plugins is the wrench. */
        if( !authored && entry->kind == TORIRS_CHROME_RAIL_ENTRY_PLUGIN )
            surface_sdl_caption(
                pixels, width, x0, width, height, cx + cw, cy + ch - scale,
                entry->title, scale, 0xffffffffu);
        if( entry->badge[0] )
            surface_sdl_caption(
                pixels, width, x0, width, height, cx + cw, cy + 8 * scale,
                entry->badge, scale, SURFACE_SDL_HOVER);
        /* The badge takes the top-right corner, so attention takes the
         * top-left one. */
        if( entry->attention )
            surface_sdl_rect(pixels, width, x0, width, height,
                cx + scale, cy + scale, 3 * scale, 3 * scale, SURFACE_SDL_ATTENTION);
        if( index == s->rail_hover )
        {
            surface_sdl_rect(pixels, width, x0, width, height, cx, cy, cw, scale, SURFACE_SDL_HOVER);
            surface_sdl_rect(pixels, width, x0, width, height, cx, cy + ch - scale, cw, scale,
                SURFACE_SDL_HOVER);
            surface_sdl_rect(pixels, width, x0, width, height, cx, cy, scale, ch, SURFACE_SDL_HOVER);
            surface_sdl_rect(pixels, width, x0, width, height, cx + cw - scale, cy, scale, ch,
                SURFACE_SDL_HOVER);
        }
    }

    surface_sdl_popout_box(s, &bx, &by, &side);
    if( side > 0 )
    {
        int const slot = s->popped
                             ? (s->popout_hover ? TORIRS_CHROME_SKIN_DOCK_OVER : TORIRS_CHROME_SKIN_DOCK)
                             : (s->popout_hover ? TORIRS_CHROME_SKIN_POPOUT_OVER
                                                : TORIRS_CHROME_SKIN_POPOUT);
        struct ToriRSChromeSkin_Sprite const* mark = ToriRSChromeSkin_ForSlot(slot);
        if( mark )
            surface_sdl_blit(
                pixels, width, x0, width, height, x0 + bx, by, side, side,
                mark->argb, mark->w, mark->h);
        else
            surface_sdl_rect(pixels, width, x0, width, height, x0 + bx, by, side, side,
                s->popout_hover ? SURFACE_SDL_HOVER : 0xff0e0e0cu);
    }
}

/** Repaint the whole strip -- page part and rail -- and hand it over. */
static void
surface_sdl_paint(struct SurfaceSdl* s)
{
    int* pixels;
    int width;
    int height;

    assert(s);
    if( !s->rail_live )
        return;
    pixels = PlatformWindow_ChromePixels(s->platform);
    width = PlatformWindow_ChromeWidth(s->platform);
    height = PlatformWindow_ChromeHeight(s->platform);
    if( !pixels || width <= 0 || height <= 0 || PlatformWindow_ChromeRailWidth(s->platform) <= 0 )
        return;
    surface_sdl_paint_page(s, pixels, width, height);
    surface_sdl_paint_rail(s, pixels, width, height);
    s->painted_w = width;
    s->painted_h = height;
    PlatformWindow_ChromePresent(s->platform);
}

/* ---- the rail ------------------------------------------------------------ */

static void
surface_sdl_queue_select(struct SurfaceSdl* s, int entry, uint32_t generation)
{
    struct ToriRSChromeRailIntent intent;

    assert(s);
    if( entry < 0 || entry >= s->rail.entry_count || generation == 0 )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
    intent.plugin_index = s->rail.entries[entry].plugin_index;
    intent.selection_generation = generation;
    if( ++s->rail_sequence == 0 )
        s->rail_sequence = 1;
    intent.sequence = s->rail_sequence;
    /* Selection is desired state: a full burst keeps its last slot for the
     * newest destination, as the browser executor's queue does. */
    if( s->rail_pending_count >= SURFACE_SDL_RAIL_QUEUE_MAX )
        s->rail_pending[SURFACE_SDL_RAIL_QUEUE_MAX - 1] = intent;
    else
        s->rail_pending[s->rail_pending_count++] = intent;
}

static int
surface_sdl_rail_sync(void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct SurfaceSdl* s = user;
    int selected = -1;
    int visible;

    assert(s);
    assert(snapshot);
    /* The strip is the platform's to open (PlatformWindow_PluginChromeShow,
     * once the host has started). Until it has, there is nowhere to draw, and
     * refusing keeps the rail sync asking. */
    if( PlatformWindow_ChromeRailWidth(s->platform) <= 0 )
        return 0;
    s->rail = *snapshot;
    s->rail_live = 1;
    visible = surface_sdl_visible_rows(s);
    for( int i = 0; i < s->rail.entry_count; i++ )
        if( s->rail.entries[i].plugin_index == s->rail.selected_entry )
        {
            selected = i;
            break;
        }
    if( selected >= 0 && selected < s->rail_scroll )
        s->rail_scroll = selected;
    else if( selected >= s->rail_scroll + visible )
        s->rail_scroll = selected - visible + 1;
    s->rail_scroll = surface_sdl_scroll_clamp(s, s->rail_scroll);
    s->rail_pressed = -1;
    surface_sdl_paint(s);
    return 1;
}

static int
surface_sdl_rail_icon(void* user, struct ToriRSChromeRailIcon const* update)
{
    struct SurfaceSdl* s = user;
    struct SurfaceSdlIcon* icon;

    assert(s);
    assert(update);
    if( update->plugin_index < 0 || update->plugin_index >= SURFACE_SDL_ICON_MAX ||
        update->width < 0 || update->height < 0 ||
        update->width > TORIRS_CHROME_RAIL_ICON_SIDE_MAX ||
        update->height > TORIRS_CHROME_RAIL_ICON_SIDE_MAX )
        return 0;
    icon = &s->icons[update->plugin_index];
    icon->revision = update->revision;
    icon->width = update->width;
    icon->height = update->height;
    if( update->width > 0 && update->height > 0 )
        memcpy(icon->argb, update->argb,
            (size_t)update->width * (size_t)update->height * sizeof(icon->argb[0]));
    surface_sdl_paint(s);
    return 1;
}

static void surface_sdl_set_popped(struct SurfaceSdl* s, int popped);

/** The rail's own pointer: hover, press, wheel scroll, the pop-out mark. */
static void
surface_sdl_rail_input(struct SurfaceSdl* s)
{
    struct PlatformWindow_AuxInput input;
    int repaint = 0;
    int hover;
    int popout_hover;

    assert(s);
    /* A strip the platform re-made since the last paint is blank. */
    if( s->rail_live && (PlatformWindow_ChromeWidth(s->platform) != s->painted_w ||
                         PlatformWindow_ChromeHeight(s->platform) != s->painted_h) )
        repaint = 1;
    if( !PlatformWindow_ChromeTakeRailInput(s->platform, &input) )
    {
        if( repaint )
            surface_sdl_paint(s);
        return;
    }
    if( input.resized )
        repaint = 1;
    if( input.wheel )
    {
        int const next = surface_sdl_scroll_clamp(s, s->rail_scroll - input.wheel);
        if( next != s->rail_scroll )
        {
            s->rail_scroll = next;
            repaint = 1;
        }
    }
    hover = surface_sdl_entry_at(s, input.mouse_x, input.mouse_y);
    popout_hover = surface_sdl_popout_at(s, input.mouse_x, input.mouse_y);
    if( hover != s->rail_hover || popout_hover != s->popout_hover )
    {
        s->rail_hover = hover;
        s->popout_hover = popout_hover;
        repaint = 1;
    }
    if( input.mouse_down )
    {
        s->rail_pressed = hover;
        s->popout_pressed = popout_hover;
        s->rail_press_generation = s->rail.selection_generation;
        repaint = 1;
    }
    if( input.mouse_up )
    {
        if( hover >= 0 && hover == s->rail_pressed )
            surface_sdl_queue_select(s, hover, s->rail_press_generation);
        else if( popout_hover && s->popout_pressed && s->open )
            surface_sdl_set_popped(s, !s->popped);
        s->rail_pressed = -1;
        s->popout_pressed = 0;
        s->rail_press_generation = 0;
        repaint = 1;
    }
    if( repaint )
        surface_sdl_paint(s);
}

static int
surface_sdl_rail_poll(void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct SurfaceSdl* s = user;
    int count;

    assert(s);
    assert(out);
    if( max <= 0 )
        return 0;
    /* The aux window's close box docks the page; asked here because the rail
     * is polled every frame, open page or not. */
    if( s->popped && PlatformWindow_AuxTakeCloseRequest(s->platform) )
        surface_sdl_set_popped(s, 0);
    /*
     * Headless drive: press the pop-out mark after the page has been up for
     * TORIRS_SIM_CHROME_POPOUT polls -- the same toggle the press makes, for a
     * run with no pointer to make it (SDL_VIDEODRIVER=dummy).
     */
    if( s->open && s->sim_popout_polls > 0 && --s->sim_popout_polls == 0 )
        surface_sdl_set_popped(s, !s->popped);
    surface_sdl_rail_input(s);
    count = s->rail_pending_count < max ? s->rail_pending_count : max;
    for( int i = 0; i < count; i++ )
        out[i] = s->rail_pending[i];
    for( int i = count; i < s->rail_pending_count; i++ )
        s->rail_pending[i - count] = s->rail_pending[i];
    s->rail_pending_count -= count;
    return count;
}

/* ---- the page ------------------------------------------------------------ */

/*
 * Move the page between the strip and the aux window. The strip's page width
 * is given back while it is out (ChromeClose keeps the rail) and asked for
 * again when it docks; the App re-lays the page out at the new surface's size
 * and rasterises it there on its next frame.
 */
static void
surface_sdl_set_popped(struct SurfaceSdl* s, int popped)
{
    assert(s);
    if( popped == s->popped || !s->open )
        return;
    if( popped )
    {
        if( !PlatformWindow_AuxOpen(
                s->platform, SURFACE_SDL_PAGE_W, SURFACE_SDL_PAGE_H, "Plugins") )
        {
            fprintf(stderr, "chrome: no second window on this video driver; the page stays docked\n");
            return;
        }
        s->popped = 1;
        PlatformWindow_ChromeClose(s->platform);
        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(stderr, "chrome: page popped out into its own window (%dx%d)\n",
                PlatformWindow_AuxWidth(s->platform), PlatformWindow_AuxHeight(s->platform));
    }
    else
    {
        PlatformWindow_AuxClose(s->platform);
        s->popped = 0;
        if( !PlatformWindow_ChromeOpen(
                s->platform, SURFACE_SDL_PAGE_W, SURFACE_SDL_PAGE_H, "Plugins") )
            fprintf(stderr, "chrome: the page could not dock beside the rail\n");
        else if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(stderr, "chrome: page docked beside the rail\n");
    }
    s->popout_hover = 0;
    surface_sdl_paint(s);
    /* The window just opened may be the size the page was last drawn at, in
     * which case the App sees nothing to redraw: put the picture it has in. */
    if( s->popped && s->page_pixels && PlatformWindow_AuxPixels(s->platform) &&
        PlatformWindow_AuxWidth(s->platform) == s->page_w &&
        PlatformWindow_AuxHeight(s->platform) == s->page_h )
    {
        memcpy(PlatformWindow_AuxPixels(s->platform), s->page_pixels,
            (size_t)s->page_w * (size_t)s->page_h * sizeof(*s->page_pixels));
        PlatformWindow_AuxPresent(s->platform);
    }
}

static int
surface_sdl_begin(void* user)
{
    struct SurfaceSdl* s = user;

    assert(s);
    if( PlatformWindow_ChromeRailWidth(s->platform) <= 0 ||
        !PlatformWindow_ChromeOpen(
            s->platform, SURFACE_SDL_PAGE_W, SURFACE_SDL_PAGE_H, "Plugins") )
        return 0;
    s->open = 1;
    s->popped = 0;
    s->page_w = 0;
    s->page_h = 0;
    surface_sdl_paint(s);
    return 1;
}

static void
surface_sdl_end(void* user)
{
    struct SurfaceSdl* s = user;

    assert(s);
    if( !s->open )
        return;
    if( s->popped )
        PlatformWindow_AuxClose(s->platform);
    else
        PlatformWindow_ChromeClose(s->platform);
    s->open = 0;
    s->popped = 0;
    s->popout_hover = 0;
    s->popout_pressed = 0;
    surface_sdl_paint(s);
}

static int
surface_sdl_size(void* user, int* out_w, int* out_h)
{
    struct SurfaceSdl* s = user;
    int w;
    int h;

    assert(s);
    assert(out_w);
    assert(out_h);
    if( !s->open )
        return 0;
    w = s->popped ? PlatformWindow_AuxWidth(s->platform) : PlatformWindow_ChromePageWidth(s->platform);
    h = s->popped ? PlatformWindow_AuxHeight(s->platform) : PlatformWindow_ChromeHeight(s->platform);
    if( w <= 0 || h <= 0 )
        return 0;
    *out_w = w;
    *out_h = h;
    return 1;
}

/*
 * The editing-key values platform/ reports and the ones ui/ understands are the
 * same numbers, restated on each side of a layer boundary neither may cross.
 * Pinned here, where both headers are included, so a value added to one enum
 * and not the other fails to COMPILE rather than mapping Home to Delete.
 */
_Static_assert((int)PLATFORM_AUX_KEY_NONE == (int)TORIRS_CHROME_KEY_NONE, "aux key: none");
_Static_assert(
    (int)PLATFORM_AUX_KEY_BACKSPACE == (int)TORIRS_CHROME_KEY_BACKSPACE, "aux key: bksp");
_Static_assert((int)PLATFORM_AUX_KEY_DELETE == (int)TORIRS_CHROME_KEY_DELETE, "aux key: del");
_Static_assert((int)PLATFORM_AUX_KEY_LEFT == (int)TORIRS_CHROME_KEY_LEFT, "aux key: left");
_Static_assert((int)PLATFORM_AUX_KEY_RIGHT == (int)TORIRS_CHROME_KEY_RIGHT, "aux key: right");
_Static_assert((int)PLATFORM_AUX_KEY_HOME == (int)TORIRS_CHROME_KEY_HOME, "aux key: home");
_Static_assert((int)PLATFORM_AUX_KEY_END == (int)TORIRS_CHROME_KEY_END, "aux key: end");
_Static_assert((int)PLATFORM_AUX_KEY_ENTER == (int)TORIRS_CHROME_KEY_ENTER, "aux key: enter");
_Static_assert((int)PLATFORM_AUX_KEY_ESCAPE == (int)TORIRS_CHROME_KEY_ESCAPE, "aux key: esc");
_Static_assert((int)PLATFORM_AUX_KEY_UP == (int)TORIRS_CHROME_KEY_UP, "aux key: up");
_Static_assert((int)PLATFORM_AUX_KEY_DOWN == (int)TORIRS_CHROME_KEY_DOWN, "aux key: down");
_Static_assert(
    sizeof(((struct ToriRSChromeSurfaceInput*)0)->text) ==
        sizeof(((struct PlatformWindow_AuxInput*)0)->text),
    "surface input: text sizes");

static int
surface_sdl_input(void* user, struct ToriRSChromeSurfaceInput* out)
{
    struct SurfaceSdl* s = user;
    struct PlatformWindow_AuxInput aux;

    assert(s);
    assert(out);
    if( !s->open ||
        !(s->popped ? PlatformWindow_AuxTakeInput(s->platform, &aux)
                    : PlatformWindow_ChromeTakeInput(s->platform, &aux)) )
        return 0;
    /* The aux surface follows its window; the strip is the platform's own. */
    if( s->popped && aux.resized )
        (void)PlatformWindow_AuxResize(s->platform, aux.width, aux.height);
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
    memcpy(out->text, aux.text, sizeof(out->text));
    out->text[sizeof(out->text) - 1] = '\0';
    return 1;
}

static void
surface_sdl_present(void* user, uint32_t const* argb, int width, int height)
{
    struct SurfaceSdl* s = user;
    size_t const count = (size_t)width * (size_t)height;

    assert(s);
    assert(argb);
    assert(width > 0);
    assert(height > 0);
    if( !s->open )
        return;
    if( s->page_w != width || s->page_h != height )
    {
        s->page_pixels = (uint32_t*)ToriMem_Realloc(
            ToriMem_Ui(), s->page_pixels, count * sizeof(*s->page_pixels));
        assert(s->page_pixels);
        s->page_w = width;
        s->page_h = height;
    }
    memcpy(s->page_pixels, argb, count * sizeof(*argb));
    if( s->popped )
    {
        int* pixels = PlatformWindow_AuxPixels(s->platform);
        /* A picture for a size the window has already left is dropped; the
         * App re-rasterises at the new size on its next frame. */
        if( !pixels || PlatformWindow_AuxWidth(s->platform) != width ||
            PlatformWindow_AuxHeight(s->platform) != height )
            return;
        memcpy(pixels, argb, count * sizeof(*argb));
        PlatformWindow_AuxPresent(s->platform);
        return;
    }
    surface_sdl_paint(s);
}

void
ToriRSChromeExec_SdlSurfaceShutdown(void)
{
    ToriMem_Free(ToriMem_Ui(), g_surface_sdl.page_pixels);
    g_surface_sdl.page_pixels = NULL;
    g_surface_sdl.page_w = 0;
    g_surface_sdl.page_h = 0;
}

struct ToriRSChromeExec
ToriRSChromeExec_SdlSurface(void* platform)
{
    struct ToriRSChromeExec exec;

    assert(platform);
    ToriMem_Free(ToriMem_Ui(), g_surface_sdl.page_pixels);
    memset(&g_surface_sdl, 0, sizeof(g_surface_sdl));
    g_surface_sdl.platform = platform;
    g_surface_sdl.rail_hover = -1;
    g_surface_sdl.rail_pressed = -1;
    {
        char const* sim = getenv("TORIRS_SIM_CHROME_POPOUT");
        g_surface_sdl.sim_popout_polls = sim && sim[0] ? atoi(sim) : 0;
    }

    memset(&exec, 0, sizeof(exec));
    exec.user = &g_surface_sdl;
    /* No apply and no poll: the page is BUFFER's retained model, drawn from
     * its display list, so the semantic command stream has no consumer. */
    exec.begin = surface_sdl_begin;
    exec.end = surface_sdl_end;
    exec.rail_sync = surface_sdl_rail_sync;
    exec.rail_icon = surface_sdl_rail_icon;
    exec.rail_poll = surface_sdl_rail_poll;
    exec.surface_size = surface_sdl_size;
    exec.surface_input = surface_sdl_input;
    exec.surface_present = surface_sdl_present;
    return exec;
}
