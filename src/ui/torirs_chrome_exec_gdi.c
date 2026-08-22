/*
 * The Win32 chrome executor: the plugin window as real Windows controls.
 *
 * A NATIVE-WIDGET executor (see the two kinds in torirs_chrome_exec.h), and the
 * only one whose widgets are the operating system's own: a BUTTON with
 * BS_AUTOCHECKBOX is a checkbox, an EDIT is a text field, a COMBOBOX is a
 * dropdown. The chrome's display list is not used at all -- none of those can
 * be reconstructed from rectangles, which is the entire reason this kind of
 * executor exists.
 *
 * ONE OWNED TOOL WINDOW, NEVER A RENDER TARGET. WS_EX_TOOLWINDOW keeps it off
 * the taskbar and `hwnd_owner` keeps it above the game and minimising with it.
 * No renderer is ever bound to it: see COMMON-CHROME-001, and the narrow
 * amendment to WINDOWS-HOST-001 that permits this window precisely because it
 * holds controls rather than a device.
 *
 * NO COMCTL32. The five control classes used here -- BUTTON, EDIT, STATIC,
 * COMBOBOX and the tab strip, which is drawn as a row of BUTTONs -- are all in
 * USER32, present since Win32 began. A real WC_TABCONTROL would mean linking
 * comctl32, shipping a manifest for the v6 common controls, and adding both to
 * the lane's import audit; a row of buttons is the same affordance with none of
 * that, on a window that already looks like a tool palette. The XP lane's
 * one-file artifact contract (WINXP-ABI-001) is what makes that trade worth
 * taking.
 *
 * THE CONTROLS ARE THE GAME'S, NOT THE SHELL'S. Every one of them is drawn
 * with the same baked cache art the CS2 executor builds its panel out of:
 * tradebacking behind the window and every field, the interfaces' 17x17 tick
 * and cross for a boolean, the scrollbar's own arrow on a dropdown, and the
 * nine-slice panel frame. They are still USER32 controls -- an EDIT is an
 * EDIT, with its caret and its selection and its keyboard -- they are simply
 * BS_OWNERDRAW and WM_CTLCOLOR* the whole way down. That is what a native
 * executor is for: the platform's own controls, wearing the application's
 * look, rather than a second rasteriser.
 *
 * GDI32 ONLY, no msimg32. AlphaBlend and TransparentBlt live in msimg32, which
 * would be another import on a lane whose whole contract is a one-file
 * artifact -- so the sprites are composited in SOFTWARE into a scratch DIB
 * (chrome_gdi_blit) and blitted back opaque. That is forty lines against a new
 * dependency, and it also makes the nearest-neighbour scaling ours rather than
 * the driver's, which is what keeps a 2x checkbox crisp instead of smeared.
 *
 * Compiled only in the Windows lanes; TORIRS_CHROME_EXEC_GDI_AVAILABLE tells
 * the chooser it is here.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_metrics.h"
#include "torirs_chrome_mirror.h"
#include "torirs_chrome_skin.h"

#include "../platform/platform_sdl2.h"

#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char const CHROME_GDI_WNDCLASS[] = "TorirsChromeToolWindow";

/**
 * Chrome pixels to window pixels.
 *
 * TWO, like the web executor's, and for the same reason: the authored geometry
 * is the game's at 1x, and an 18-pixel row with a 17-pixel checkbox is
 * unreadably small in a desktop window beside the shell's own UI font. Every
 * sprite is scaled nearest-neighbour by chrome_gdi_blit, so doubling is the
 * blow-up the game does at interface scale 2 rather than a blur.
 */
#define CHROME_GDI_K 2

/*
 * Layout: the shared chrome metrics, scaled.
 *
 * These USED to be numbers of this file's own -- 22-pixel rows, a 110-pixel
 * label column -- on the grounds that Windows controls at the system font want
 * their own geometry. That was true while the controls looked like Windows.
 * They are drawn with the game's art now, so they get the game's grid, out of
 * the same torirs_chrome_metrics.h the in-canvas chrome and the CS2 executor
 * lay out from. The one thing that stays the system's is the FONT: a bitmap
 * face cannot be given to an EDIT, and mixing a baked p12 in the owner-drawn
 * halves with the shell's font in the typed ones would read worse than one
 * font throughout.
 */
#define CHROME_GDI_PX(px) ((px) * CHROME_GDI_K)
#define CHROME_GDI_PAD CHROME_GDI_PX(TORIRS_CHROME_M_PAD)
#define CHROME_GDI_ROW_H CHROME_GDI_PX(TORIRS_CHROME_M_ROW_H)
#define CHROME_GDI_ROW_GAP CHROME_GDI_PX(TORIRS_CHROME_M_ROW_GAP)
#define CHROME_GDI_LABEL_W CHROME_GDI_PX(TORIRS_CHROME_M_LABEL_W)
#define CHROME_GDI_TAB_H CHROME_GDI_PX(TORIRS_CHROME_M_TAB_H)
#define CHROME_GDI_TAB_W CHROME_GDI_PX(48)
/** Checkbox edge at 1x, for the art this window was told to wear -- @see
 *  chrome_gdi_box. A macro would have to name a style it cannot see. */
#define CHROME_GDI_BOX_TICK CHROME_GDI_PX(TORIRS_CHROME_M_BOX)
#define CHROME_GDI_BOX_SQUARE CHROME_GDI_PX(TORIRS_CHROME_M_BOX_SQUARE)
#define CHROME_GDI_CHECK_GAP CHROME_GDI_PX(TORIRS_CHROME_M_CHECK_GAP)
#define CHROME_GDI_TOGGLE_W CHROME_GDI_PX(TORIRS_CHROME_M_TOGGLE_W)
#define CHROME_GDI_ROW_ICON CHROME_GDI_PX(TORIRS_CHROME_M_ROW_ICON)
#define CHROME_GDI_ROW_ICON_GAP CHROME_GDI_PX(TORIRS_CHROME_M_ROW_ICON_GAP)
#define CHROME_GDI_ROW_NAME_GAP CHROME_GDI_PX(TORIRS_CHROME_M_ROW_NAME_GAP)
#define CHROME_GDI_DOT CHROME_GDI_PX(TORIRS_CHROME_M_DOT)
#define CHROME_GDI_DOT_PITCH CHROME_GDI_PX(TORIRS_CHROME_M_DOT_PITCH)
#define CHROME_GDI_DOT_INSET CHROME_GDI_PX(TORIRS_CHROME_M_DOT_INSET)
#define CHROME_GDI_SWATCH CHROME_GDI_PX(TORIRS_CHROME_M_SWATCH)
#define CHROME_GDI_SWATCH_GAP CHROME_GDI_PX(TORIRS_CHROME_M_SWATCH_GAP)
#define CHROME_GDI_FRAME CHROME_GDI_PX(TORIRS_CHROME_M_FRAME)
/** Side of one frame corner tile. It carries the rail along each of its two
 *  outer edges, so it blits square at THIS rather than at the rail's width --
 *  @see chrome_gdi_frame. */
#define CHROME_GDI_FRAME_CORNER CHROME_GDI_PX(TORIRS_CHROME_M_FRAME_CORNER)
#define CHROME_GDI_FIELD_PAD_X CHROME_GDI_PX(TORIRS_CHROME_M_FIELD_PAD_X)
#define CHROME_GDI_FIELD_INSET CHROME_GDI_PX(TORIRS_CHROME_M_FIELD_INSET)
#define CHROME_GDI_RULE CHROME_GDI_PX(1)
/** A multiline field's own two numbers. @see TORIRS_CHROME_M_TEXTAREA_LINE --
 *  the pitch is authored rather than measured because this window lays out in
 *  the shared metrics and asks the EDIT nothing. */
#define CHROME_GDI_TEXTAREA_LINE CHROME_GDI_PX(TORIRS_CHROME_M_TEXTAREA_LINE)
#define CHROME_GDI_TEXTAREA_PAD_Y CHROME_GDI_PX(TORIRS_CHROME_M_TEXTAREA_PAD_Y)
/** One wheel/scrollbar line. The ordinary row including its gap is the least
 * surprising distance: a notch always reveals another complete setting. */
#define CHROME_GDI_SCROLL_LINE (CHROME_GDI_ROW_H + CHROME_GDI_ROW_GAP)
#define CHROME_GDI_SCROLL_W CHROME_GDI_PX(TORIRS_CHROME_M_SCROLL_W)
#define CHROME_GDI_SCROLL_CAP_H CHROME_GDI_PX(TORIRS_CHROME_M_SCROLL_CAP_H)
#define CHROME_GDI_SCROLL_GRIP_MIN CHROME_GDI_PX(TORIRS_CHROME_M_SCROLL_GRIP_MIN)
#define CHROME_GDI_SCROLL_GAP CHROME_GDI_PX(2)
/** The drawn title uses the minimenu face's authored 16px line box. The block
 * also includes the body gap and separator under that black bar, verbatim from
 * dbg_menu_layout. */
#define CHROME_GDI_TITLE_H CHROME_GDI_PX(16)
#define CHROME_GDI_TITLE_GAP CHROME_GDI_PX(2)
#define CHROME_GDI_HEADER_H                                                                    \
    (CHROME_GDI_FRAME - CHROME_GDI_RULE + CHROME_GDI_TITLE_H + CHROME_GDI_TITLE_GAP +       \
     CHROME_GDI_RULE)
#define CHROME_GDI_CLOSE_PAD CHROME_GDI_PX(TORIRS_CHROME_M_CLOSE_PAD)

/** Opening size: the label column, a field beside it, and the pads. The window
 *  is resizable; this is only where it starts. */
#define CHROME_GDI_W (2 * CHROME_GDI_PAD + 2 * CHROME_GDI_LABEL_W + 40)
#define CHROME_GDI_H CHROME_GDI_PX(240)

/** The palette, from torirs_chrome_metrics.h, as COLORREFs. */
#define CHROME_GDI_RGB(x) RGB(((x) >> 16) & 0xFF, ((x) >> 8) & 0xFF, (x) & 0xFF)

/**
 * Control ids. Chrome handles are small and dense, so the id IS the handle
 * plus a base -- which makes the WM_COMMAND route a subtraction rather than a
 * search, and makes an id collision impossible by construction.
 *
 * A LISTROW owns TWO controls -- its switch and its settings affordance -- and
 * they report different intents, so the second gets a parallel block at the
 * same offset. Exactly what CS2_ID_ACTION_BASE does in the CS2 executor, and
 * for the same reason: the handle stays the index into both.
 */
#define CHROME_GDI_ID_BASE 0x4000
#define CHROME_GDI_ID_TAB_BASE 0x5000
#define CHROME_GDI_ID_ACTION_BASE 0x6000
/** The window's own close button. One control, so one id rather than a block. */
#define CHROME_GDI_ID_CLOSE 0x7000
/**
 * The close button's box: the sprite, doubled.
 *
 * Sized to the ART rather than to a row, because it is the one control here
 * whose art is a fixed 16x16 with no scale variants in the cache -- so the
 * only size that stays crisp is an integer multiple, and CHROME_GDI_K is
 * already that multiple everywhere else in this window.
 */
#define CHROME_GDI_CLOSE_SIDE CHROME_GDI_PX(16)

struct ChromeGdi
{
    HWND owner;
    HWND hwnd;
    HFONT font;
    int open;
    /** The panel this one-window executor is presenting. Unlike `tab_panel`,
     * this is also set for a paged window with no TABSTRIP, so either close X
     * can address a real panel. */
    int window_panel;
    struct ToriRSChromeMirror mirror;
    /** The panel whose tabs the strip shows, and its titles. One window, so
     *  one strip; a list would be a list with one entry. */
    int tab_panel;
    int tab_strip_widget;
    char tabs[16][64];
    int tab_count;
    HWND tab_buttons[16];
    /**
     * enum ToriRSChromeCheckStyle, as TORIRS_CHROME_CMD_CHECK_STYLE last said.
     *
     * Held rather than read off a model: this executor cannot see one. Zero is
     * TICK, so a client too old to send the command draws what it always drew.
     */
    int check_style;
    /**
     * A COLORPICK's sample: the STATIC in front of its EDIT, and the brush
     * WM_CTLCOLORSTATIC hands back for it.
     *
     * The brush is OWNED here and recreated whenever the value changes,
     * because a control colour in Win32 is a GDI object rather than a
     * property: there is nowhere to put an RGB, only somewhere to return a
     * brush from. Held per widget so the window's paint can find the right one
     * from the HWND the message names.
     */
    HWND swatch[TORIRS_CHROME_MAX_WIDGETS];
    HBRUSH swatch_brush[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * The STATIC carrying a labelled control's caption.
     *
     * Held because the layout has to PLACE it. It was created and then never
     * positioned, so every label in this window sat at 0,0 in the 10x10 box
     * CreateWindowEx was given -- three captions stacked in the top-left
     * corner and nothing beside the fields they name. Nobody had seen it: this
     * executor compiles on Windows and is exercised nowhere else.
     */
    HWND label[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * A LISTROW's settings affordance -- the three-dot well.
     *
     * A second control, not a zone of the first: the row has two outcomes and
     * a BS_OWNERDRAW BUTTON reports a click without saying WHERE, so one
     * control could never tell "open the page" from "flip the switch". The CS2
     * executor splits it the same way and for the same reason.
     */
    HWND action[TORIRS_CHROME_MAX_WIDGETS];
    /**
     * State the CONTROLS no longer hold, now that they are owner-drawn.
     *
     * BS_AUTOCHECKBOX kept its own tick and answered BM_GETCHECK; BS_OWNERDRAW
     * draws whatever we say and knows nothing, so the checked bit lives here
     * and the click toggles it. `row_action` is a LISTROW's shape and rides
     * the ADD, exactly as it does in the CS2 executor.
     */
    unsigned char checked[TORIRS_CHROME_MAX_WIDGETS];
    unsigned char row_action[TORIRS_CHROME_MAX_WIDGETS];
    /** A TEXTAREA's line count -- the other half of the ADD's shape fields.
     *  The layout needs it, because a multiline row is the one row here that is
     *  not CHROME_GDI_ROW_H tall. */
    unsigned char rows[TORIRS_CHROME_MAX_WIDGETS];
    /** Resolved widget colour, 0 meaning the kind's palette default. */
    uint32_t color[TORIRS_CHROME_MAX_WIDGETS];
    /** Set by the last layout only for rows actually inside the scroll
     * viewport. WM_ERASEBKGND uses it so a clipped EDIT cannot leave a field
     * frame painted at its old position. */
    unsigned char presented[TORIRS_CHROME_MAX_WIDGETS];
    int scroll_y;
    int content_h;
    unsigned char scrollbar_visible;
    unsigned char scrollbar_dragging;
    int scrollbar_drag_offset;
    /**
     * The skin, once.
     *
     * `tile_brush` is a pattern brush over the tradebacking, which is the only
     * way to give a CONTROL a textured background in Win32 -- WM_CTLCOLOR*
     * returns a brush, not a paint callback. `scratch` is the compositing
     * buffer chrome_gdi_blit works in; it grows to the largest box asked for
     * and is never shrunk, because the largest is the window and the window is
     * repainted constantly.
     */
    /** The window's own X, inside the drawn title band. */
    HWND close_button;
    int skin_ok;
    HBITMAP tile_bitmap;
    HBRUSH tile_brush;
    HDC scratch_dc;
    HBITMAP scratch_bitmap;
    uint32_t* scratch_pixels;
    int scratch_w;
    int scratch_h;
};

static struct ChromeGdi g_chrome_gdi;

/* ---- the baked skin -------------------------------------------------------
 *
 * The window's whole appearance, and the only reason any of these controls is
 * owner-drawn. See the note at the top of the file on why the compositing is
 * done in software rather than with msimg32's AlphaBlend.
 */

/** 0xRRGGBB -> a COLORREF, which is 0x00BBGGRR. The one place the two byte
 *  orders meet; getting it wrong swaps red and blue in every swatch. */
static COLORREF
chrome_gdi_colorref(uint32_t rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/**
 * A top-down 32-bit DIB and a DC selecting it, at least `w` x `h`.
 *
 * Top-down (a NEGATIVE biHeight) so row 0 is the top one and the compositing
 * loop below can index it the way every other raster in this tree is indexed.
 * A bottom-up DIB would composite upside down and blit back upside down, which
 * cancels out for a solid fill and does not for a tick.
 */
static int
chrome_gdi_scratch(struct ChromeGdi* s, int w, int h)
{
    BITMAPINFO bi;
    HBITMAP made;
    void* bits = NULL;

    if( w <= 0 || h <= 0 )
        return 0;
    if( s->scratch_bitmap && s->scratch_w >= w && s->scratch_h >= h )
        return 1;
    if( !s->scratch_dc )
        s->scratch_dc = CreateCompatibleDC(NULL);
    if( !s->scratch_dc )
        return 0;

    if( w < s->scratch_w )
        w = s->scratch_w;
    if( h < s->scratch_h )
        h = s->scratch_h;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    made = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if( !made || !bits )
        return 0;

    SelectObject(s->scratch_dc, made);
    if( s->scratch_bitmap )
        DeleteObject(s->scratch_bitmap);
    s->scratch_bitmap = made;
    s->scratch_pixels = bits;
    s->scratch_w = w;
    s->scratch_h = h;
    return 1;
}

/**
 * One baked sprite, scaled into a box and composited over what is already
 * there.
 *
 * The destination is READ BACK first (a BitBlt into the scratch) rather than
 * assumed: these sprites have soft edges and rounded corners, and a tick
 * composited against a guess at the background gets a halo of that guess
 * around it wherever the guess was wrong -- over the tradebacking, which is
 * noise, that is every pixel.
 *
 * Nearest neighbour, deliberately. The art is baked at 1x with its outline
 * already in the pixels (see the note on DBG_CHECK_SIZE), so a smooth scale
 * produces the speckled edge that outline was drawn to avoid.
 */
static void
chrome_gdi_blit(struct ChromeGdi* s, HDC dc, int x, int y, int w, int h, int slot)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_ForSlot(slot);

    if( !spr || spr->w <= 0 || spr->h <= 0 || w <= 0 || h <= 0 )
        return;
    if( !chrome_gdi_scratch(s, w, h) )
        return;

    BitBlt(s->scratch_dc, 0, 0, w, h, dc, x, y, SRCCOPY);
    for( int j = 0; j < h; j++ )
    {
        uint32_t* dst = s->scratch_pixels + (size_t)j * (size_t)s->scratch_w;
        int const sy = (int)((long)j * spr->h / h);
        for( int i = 0; i < w; i++ )
        {
            uint32_t const src = spr->argb[(size_t)sy * (size_t)spr->w + (size_t)((long)i * spr->w / w)];
            unsigned const a = (src >> 24) & 0xFF;
            unsigned dr;
            unsigned dg;
            unsigned db;

            if( a == 0 )
                continue;
            if( a == 255 )
            {
                dst[i] = src & 0xFFFFFFu;
                continue;
            }
            dr = (dst[i] >> 16) & 0xFF;
            dg = (dst[i] >> 8) & 0xFF;
            db = dst[i] & 0xFF;
            dr = (((src >> 16) & 0xFF) * a + dr * (255 - a)) / 255;
            dg = (((src >> 8) & 0xFF) * a + dg * (255 - a)) / 255;
            db = ((src & 0xFF) * a + db * (255 - a)) / 255;
            dst[i] = (dr << 16) | (dg << 8) | db;
        }
    }
    BitBlt(dc, x, y, w, h, s->scratch_dc, 0, 0, SRCCOPY);
}

/** A flat rectangle, filled or outlined, in a 0xRRGGBB the chrome palette
 *  names. The one primitive that never needs the skin. */
static void
chrome_gdi_rect(HDC dc, int x, int y, int w, int h, uint32_t rgb)
{
    RECT r;
    HBRUSH brush;

    if( w <= 0 || h <= 0 )
        return;
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;
    brush = CreateSolidBrush(chrome_gdi_colorref(rgb));
    if( !brush )
        return;
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void
chrome_gdi_outline(HDC dc, int x, int y, int w, int h, uint32_t rgb)
{
    if( w <= 0 || h <= 0 )
        return;
    chrome_gdi_rect(dc, x, y, w, CHROME_GDI_RULE, rgb);
    chrome_gdi_rect(dc, x, y + h - CHROME_GDI_RULE, w, CHROME_GDI_RULE, rgb);
    chrome_gdi_rect(dc, x, y, CHROME_GDI_RULE, h, rgb);
    chrome_gdi_rect(dc, x + w - CHROME_GDI_RULE, y, CHROME_GDI_RULE, h, rgb);
}

/** The tradebacking, repeated across a box. The panel body, and the inside of
 *  every field box -- the reference tiles graphic_297 in both. */
static void
chrome_gdi_tile(struct ChromeGdi* s, HDC dc, int x, int y, int w, int h)
{
    struct ToriRSChromeSkin_Sprite const* tile =
        ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY);
    int const tw = tile ? tile->w * CHROME_GDI_K : 0;
    int const th = tile ? tile->h * CHROME_GDI_K : 0;

    /* The flat brown first either way: the tile carries transparent pixels at
     * its edges, so tiling it onto bare window would show through them. */
    chrome_gdi_rect(dc, x, y, w, h, TORIRS_CHROME_C_BODY);
    if( tw <= 0 || th <= 0 )
        return;
    for( int j = y; j < y + h; j += th )
        for( int i = x; i < x + w; i += tw )
        {
            int const cw = i + tw > x + w ? x + w - i : tw;
            int const ch = j + th > y + h ? y + h - j : th;
            chrome_gdi_blit(s, dc, i, j, cw, ch, TORIRS_CHROME_SKIN_PANEL_BODY);
        }
}

/**
 * The interfaces' nine-slice panel border.
 *
 * dbg_push_frame, in GDI, and it needs BOTH of the frame's numbers. A corner
 * is a 32px tile carrying an L of 6px rail along its two outer edges, and an
 * edge is a bare 6px rail: so a corner blits SQUARE AT ITS BAKED SIZE and an
 * edge stretches along the run between two corners at the rail's thickness.
 * Blitting the corners at the rail instead squashes 32px of tile into 6 and
 * takes the mitred junction and its rounded outer pixel with it -- which is
 * the whole of what makes this read as the interfaces' border rather than as
 * a brown line.
 *
 * It is still the RAIL, not the corner, that the content column is inset by
 * (chrome_gdi_layout): the corner's extra 26px lie along the edges, over the
 * window's own tile, not over its rows.
 *
 * The baked centre is never drawn, because the window's tile is already under
 * it.
 */
static void
chrome_gdi_frame(struct ChromeGdi* s, HDC dc, int w, int h)
{
    int const t = CHROME_GDI_FRAME;
    int const c = CHROME_GDI_FRAME_CORNER;
    int const mid_w = w - 2 * c;
    int const mid_h = h - 2 * c;
    /* Two different insets, for the same reason there are two numbers: a
     * corner hangs 32 in from its edge and a rail 6. */
    int const corner_r = w - c;
    int const corner_b = h - c;
    int const rail_r = w - t;
    int const rail_b = h - t;

    if( !ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) )
    {
        /* No frame baked: the black edge every interface in this game has. */
        chrome_gdi_outline(dc, 0, 0, w, h, TORIRS_CHROME_C_CHROME);
        return;
    }
    /* A window narrower or shorter than two of its own corners has no run to
     * stretch and the corners OVERLAP -- not a degenerate case to guard
     * against but what the reference's own 42px popout strip does with this
     * art. They still read as a frame; the run is simply gone. */
    chrome_gdi_blit(s, dc, 0, 0, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT);
    chrome_gdi_blit(s, dc, corner_r, 0, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT);
    chrome_gdi_blit(s, dc, 0, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT);
    chrome_gdi_blit(s, dc, corner_r, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT);
    if( mid_w > 0 )
    {
        chrome_gdi_blit(s, dc, c, 0, mid_w, t, TORIRS_CHROME_SKIN_FRAME_TOP);
        chrome_gdi_blit(s, dc, c, rail_b, mid_w, t, TORIRS_CHROME_SKIN_FRAME_BOTTOM);
    }
    if( mid_h > 0 )
    {
        chrome_gdi_blit(s, dc, 0, c, t, mid_h, TORIRS_CHROME_SKIN_FRAME_LEFT);
        chrome_gdi_blit(s, dc, rail_r, c, t, mid_h, TORIRS_CHROME_SKIN_FRAME_RIGHT);
    }
}

/**
 * The chrome a settings field wears -- script_3850's box.
 *
 * Tiled tradebacking under a near-black frame with a grey inset one pixel
 * inside it, shared by a text input, a dropdown, a button and a roster row's
 * settings well, because the reference shares it exactly that way.
 */
static void
chrome_gdi_field(struct ChromeGdi* s, HDC dc, int x, int y, int w, int h)
{
    chrome_gdi_tile(s, dc, x, y, w, h);
    chrome_gdi_outline(dc, x, y, w, h, TORIRS_CHROME_C_FRAME);
    chrome_gdi_outline(
        dc, x + CHROME_GDI_RULE, y + CHROME_GDI_RULE, w - 2 * CHROME_GDI_RULE,
        h - 2 * CHROME_GDI_RULE, TORIRS_CHROME_C_FRAME_INSET);
}

/**
 * A multiline field's box, and the whole row it makes.
 *
 * The field itself is `rows` lines plus its padding; the ROW is that with the
 * caption band above it, because a multiline row wears its caption on a line of
 * its own rather than in the label column. @see TORIRS_CHROME_W_TEXTAREA.
 */
static int
chrome_gdi_textarea_h(struct ChromeGdi const* s, int widget)
{
    int const rows = s->rows[widget] > 0 ? s->rows[widget] : TORIRS_CHROME_M_TEXTAREA_ROWS;
    return rows * CHROME_GDI_TEXTAREA_LINE + 2 * CHROME_GDI_TEXTAREA_PAD_Y +
           2 * CHROME_GDI_RULE;
}

/** Total height of one row. CHROME_GDI_ROW_H for every kind but one -- and the
 *  fit is why this exists: the placement and the advance have to agree. */
static int
chrome_gdi_row_h(struct ChromeGdi const* s, int kind, int widget)
{
    if( kind != TORIRS_CHROME_W_TEXTAREA )
        return CHROME_GDI_ROW_H;
    return (s->label[widget] ? CHROME_GDI_ROW_H : 0) + chrome_gdi_textarea_h(s, widget);
}

/* ---- line endings ---------------------------------------------------------
 *
 * The model's newline is '\n' and an EDIT control's is "\r\n", and neither
 * side may see the other's. Text written into the control without the '\r'
 * draws as one run with a box glyph where the break should be; text read back
 * WITH it puts a stray '\r' into every plugin config value -- which is then
 * written to the ini and read back by whatever the plugin does with it.
 *
 * Truncation is a plain clamp rather than an assert: a value grown past the
 * cap by its own line endings is the control's doing, not a caller's contract
 * violation, and the model's cap is what decides how much of it survives.
 */

/** '\n' -> "\r\n", for text going INTO a control. */
static void
chrome_gdi_to_crlf(char* dst, int cap, char const* src)
{
    int o = 0;

    if( !src )
        src = "";
    for( int i = 0; src[i] && o < cap - 1; i++ )
    {
        if( src[i] == '\n' && o < cap - 2 )
            dst[o++] = '\r';
        dst[o++] = src[i];
    }
    dst[o] = '\0';
}

/** "\r\n" -> '\n', for text coming back OUT of one. A lone '\r' goes too:
 *  the model has no use for one and the store it feeds has less. */
static void
chrome_gdi_to_lf(char* dst, int cap, char const* src)
{
    int o = 0;

    for( int i = 0; src[i] && o < cap - 1; i++ )
        if( src[i] != '\r' )
            dst[o++] = src[i];
    dst[o] = '\0';
}

/* ---- layout --------------------------------------------------------------- */

static void chrome_gdi_layout(struct ChromeGdi* s);

/** Top of the scrolling row viewport. Tabs and the close button stay above
 * it, fixed, while the settings move underneath. */
static int
chrome_gdi_rows_top(struct ChromeGdi const* s)
{
    int const strip_h = s->tab_count > 1 ? CHROME_GDI_TAB_H : 0;

    return CHROME_GDI_HEADER_H + CHROME_GDI_PAD +
           (strip_h > 0 ? strip_h + CHROME_GDI_ROW_GAP : 0);
}

/** Natural client height of the visible rows, before scrolling. Native
 * executors do not receive the model's pixel scroll state, so this window owns
 * its scroll position and derives its range from the mirrored rows. */
static int
chrome_gdi_measure_content(struct ChromeGdi* s)
{
    int order[TORIRS_CHROME_MAX_WIDGETS];
    int const ordered =
        ToriRSChromeMirror_Order(&s->mirror, order, TORIRS_CHROME_MAX_WIDGETS);
    int y = chrome_gdi_rows_top(s);
    int rows = 0;

    for( int oi = 0; oi < ordered; oi++ )
    {
        int const i = order[oi];
        struct ToriRSChromeMirrorWidget const* w =
            ToriRSChromeMirror_Widget(&s->mirror, i);

        if( !w || !w->native || !ToriRSChromeMirror_Shown(&s->mirror, i) )
            continue;
        y += chrome_gdi_row_h(s, w->kind, i) + CHROME_GDI_ROW_GAP;
        rows++;
    }
    if( rows > 0 )
        y -= CHROME_GDI_ROW_GAP;
    return y + CHROME_GDI_FRAME + CHROME_GDI_PAD;
}

struct ChromeGdiScrollbar
{
    int x;
    int top;
    int bottom;
    int track_y;
    int track_h;
    int grip_y;
    int grip_h;
    int max_scroll;
};

/** The client-area scrollbar assembled from ~script31's six skin pieces. */
static int
chrome_gdi_scrollbar_geometry(
    struct ChromeGdi const* s, RECT const* client, struct ChromeGdiScrollbar* bar)
{
    int viewport;

    assert(s);
    assert(client);
    assert(bar);
    memset(bar, 0, sizeof(*bar));
    bar->x = client->right - CHROME_GDI_FRAME - CHROME_GDI_PAD - CHROME_GDI_SCROLL_W;
    bar->top = chrome_gdi_rows_top(s);
    bar->bottom = client->bottom - CHROME_GDI_FRAME - CHROME_GDI_PAD;
    bar->track_y = bar->top + CHROME_GDI_SCROLL_W;
    bar->track_h = bar->bottom - bar->top - 2 * CHROME_GDI_SCROLL_W;
    bar->max_scroll = s->content_h - client->bottom;
    if( bar->max_scroll < 0 )
        bar->max_scroll = 0;
    viewport = bar->bottom - bar->top;
    if( bar->max_scroll <= 0 || viewport <= 0 || bar->track_h <= 0 )
        return 0;

    bar->grip_h = bar->track_h * viewport / (viewport + bar->max_scroll);
    if( bar->grip_h < CHROME_GDI_SCROLL_GRIP_MIN )
        bar->grip_h = CHROME_GDI_SCROLL_GRIP_MIN;
    if( bar->grip_h > bar->track_h )
        bar->grip_h = bar->track_h;
    bar->grip_y = bar->track_y;
    if( bar->track_h > bar->grip_h )
        bar->grip_y +=
            (bar->track_h - bar->grip_h) * s->scroll_y / bar->max_scroll;
    return 1;
}

static int
chrome_gdi_clamp_scroll(struct ChromeGdi* s, int requested)
{
    RECT client;
    int maximum;

    assert(s);
    if( !s->hwnd || !GetClientRect(s->hwnd, &client) )
        return 0;
    maximum = s->content_h - client.bottom;
    if( maximum < 0 )
        maximum = 0;
    if( requested < 0 )
        requested = 0;
    if( requested > maximum )
        requested = maximum;
    return requested;
}

static void
chrome_gdi_scroll_to(struct ChromeGdi* s, int requested)
{
    int const clamped = chrome_gdi_clamp_scroll(s, requested);

    if( clamped == s->scroll_y )
        return;
    s->scroll_y = clamped;
    chrome_gdi_layout(s);
}

/** Hide every HWND belonging to a row. They are retained so focus, text and
 * selection survive a tab switch or a trip outside the scroll viewport. */
static HDWP
chrome_gdi_hide_row(struct ChromeGdi* s, HDWP dwp, int widget, HWND control)
{
    dwp = DeferWindowPos(dwp, control, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
    if( s->label[widget] )
        dwp = DeferWindowPos(dwp, s->label[widget], NULL, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
    if( s->action[widget] )
        dwp = DeferWindowPos(dwp, s->action[widget], NULL, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
    if( s->swatch[widget] )
        dwp = DeferWindowPos(dwp, s->swatch[widget], NULL, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
    return dwp;
}

/**
 * Place every visible control down the window, in handle order.
 *
 * A full relayout rather than an incremental one, because the set of VISIBLE
 * rows changes wholesale on a tab switch and there is no incremental version of
 * that. It runs on shape changes only -- an add, a remove, a hide, a tab -- not
 * per frame, so the cost is a few dozen SetWindowPos calls when something
 * actually moved.
 *
 * Deferred through BeginDeferWindowPos so the window repaints once instead of
 * once per control, which is the difference between a tab switch and a flicker.
 */
static void
chrome_gdi_layout(struct ChromeGdi* s)
{
    RECT client;
    HDWP dwp;
    int y;
    int width;
    int rows_top;
    int rows_bottom;
    int live = 0;

    if( !s->hwnd )
        return;
    GetClientRect(s->hwnd, &client);

    /* The scrollbar is part of this client-area skin, never non-client chrome.
     * Compute its need before the row widths so the content column reserves
     * exactly the baked bar's width when it is present. */
    s->content_h = chrome_gdi_measure_content(s);
    s->scroll_y = chrome_gdi_clamp_scroll(s, s->scroll_y);
    s->scrollbar_visible = s->content_h > client.bottom;
    if( !s->scrollbar_visible )
        s->scrollbar_dragging = 0;
    width = client.right - client.left;
    rows_top = chrome_gdi_rows_top(s);
    rows_bottom = client.bottom - CHROME_GDI_FRAME - CHROME_GDI_PAD;
    memset(s->presented, 0, sizeof(s->presented));

    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        if( ToriRSChromeMirror_Widget(&s->mirror, i) )
            live++;

    /* Three per row at the worst -- a roster row is a name, a well and a
     * switch -- plus the strip. Over-reserving costs nothing; under-reserving
     * makes DeferWindowPos reallocate mid-batch. */
    dwp = BeginDeferWindowPos(3 * live + s->tab_count + 1);
    if( !dwp )
        return;

    /* The strip first, pinned above the rows -- the same rule the in-canvas
     * chrome follows, and for the same reason: a strip that scrolled away with
     * its rows would take the only way back with it. */
    for( int t = 0; t < s->tab_count; t++ )
        if( s->tab_buttons[t] )
            dwp = DeferWindowPos(
                dwp,
                s->tab_buttons[t],
                NULL,
                CHROME_GDI_FRAME + CHROME_GDI_PAD + t * (CHROME_GDI_TAB_W + CHROME_GDI_RULE),
                CHROME_GDI_HEADER_H + CHROME_GDI_PAD,
                CHROME_GDI_TAB_W,
                CHROME_GDI_TAB_H,
                SWP_NOZORDER | SWP_SHOWWINDOW);

    /*
     * The close button, pinned to the top-right corner of the client area --
     * where a window's close box goes, and where dbg_panel_close_box puts the
     * in-canvas one.
     *
     * Placed BEFORE `width` loses its frame inset below, because this is
     * measured from the client edge rather than from the content column.
     */
    if( s->close_button )
        dwp = DeferWindowPos(
            dwp,
            s->close_button,
            NULL,
            width - CHROME_GDI_FRAME - CHROME_GDI_CLOSE_PAD - CHROME_GDI_CLOSE_SIDE,
            CHROME_GDI_FRAME + (CHROME_GDI_TITLE_H - CHROME_GDI_CLOSE_SIDE) / 2,
            CHROME_GDI_CLOSE_SIDE,
            CHROME_GDI_CLOSE_SIDE,
            SWP_NOZORDER | SWP_SHOWWINDOW);

    /*
     * The rows start below the drawn title block and the optional tab strip.
     * The close button lives IN the title and therefore reserves no second,
     * empty band of its own.
     */
    y = rows_top - s->scroll_y;
    width -= 2 * CHROME_GDI_FRAME;

    /* In ROW order, not handle order -- see ToriRSChromeMirrorWidget::order.
     * A free-list-recycled handle walked by index lays the window out in an
     * order the model never had. */
    int order[TORIRS_CHROME_MAX_WIDGETS];
    int const ordered = ToriRSChromeMirror_Order(&s->mirror, order, TORIRS_CHROME_MAX_WIDGETS);
    for( int oi = 0; oi < ordered; oi++ )
    {
        int const i = order[oi];
        struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
        HWND control;
        int row_h;
        int x = CHROME_GDI_FRAME + CHROME_GDI_PAD;
        int row_w = width - 2 * CHROME_GDI_PAD -
                    (s->scrollbar_visible ? CHROME_GDI_SCROLL_W + CHROME_GDI_SCROLL_GAP : 0);

        if( !w || !w->native )
            continue;
        row_h = chrome_gdi_row_h(s, w->kind, i);
        control = (HWND)w->native;

        if( !ToriRSChromeMirror_Shown(&s->mirror, i) )
        {
            /* Hidden, not destroyed: the control keeps its text and its
             * selection, so switching back to a tab restores what was on it
             * rather than a rebuilt blank. The row's OTHER controls have to go
             * with it -- a roster row whose switch hid and whose name did not
             * is a caption floating over the row below. */
            dwp = chrome_gdi_hide_row(s, dwp, i, control);
            continue;
        }

        /* Keep scrolling rows out of the fixed close/tab band and the bottom
         * frame. Hiding a partially visible row is preferable to letting a
         * child HWND paint over the window furniture; at either scroll end
         * the range aligns the first/last row exactly with the viewport. */
        if( y < rows_top || y + row_h > rows_bottom )
        {
            dwp = chrome_gdi_hide_row(s, dwp, i, control);
            y += row_h + CHROME_GDI_ROW_GAP;
            continue;
        }
        s->presented[i] = 1;

        /*
         * A roster row: the name at the left, a settings well and a switch
         * pinned to the right so a column of them lines up however long the
         * names are. Right to left, exactly as the in-canvas chrome and the
         * CS2 executor both place it.
         */
        if( w->kind == TORIRS_CHROME_W_LISTROW )
        {
            int const tog_x = x + row_w - CHROME_GDI_TOGGLE_W;
            int const icon_x = tog_x - CHROME_GDI_ROW_ICON_GAP - CHROME_GDI_ROW_ICON;
            int const name_w =
                (s->action[i] ? icon_x : tog_x) - CHROME_GDI_ROW_NAME_GAP - x;

            if( s->label[i] )
                dwp = DeferWindowPos(
                    dwp, s->label[i], NULL, x, y, name_w > 0 ? name_w : 1, row_h,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
            if( s->action[i] )
                dwp = DeferWindowPos(
                    dwp, s->action[i], NULL, icon_x,
                    y + (row_h - CHROME_GDI_ROW_ICON) / 2, CHROME_GDI_ROW_ICON,
                    CHROME_GDI_ROW_ICON, SWP_NOZORDER | SWP_SHOWWINDOW);
            dwp = DeferWindowPos(
                dwp, control, NULL, tog_x, y, CHROME_GDI_TOGGLE_W, row_h,
                SWP_NOZORDER | SWP_SHOWWINDOW);
            y += row_h + CHROME_GDI_ROW_GAP;
            continue;
        }

        /*
         * A multiline row: the caption on a line of its own, the box under it
         * across the whole column.
         *
         * Before the label-column branch below, not inside it, because this
         * row does not use that column at all -- 104 pixels of caption beside
         * a four-line list takes the width the list is for. @see
         * TORIRS_CHROME_W_TEXTAREA.
         */
        if( w->kind == TORIRS_CHROME_W_TEXTAREA )
        {
            int const cap_h = s->label[i] ? CHROME_GDI_ROW_H : 0;

            if( s->label[i] )
                dwp = DeferWindowPos(
                    dwp, s->label[i], NULL, x, y, row_w, CHROME_GDI_ROW_H,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
            /* Inset inside its field box for the same reason a one-line EDIT
             * is: the parent draws the frame under it in WM_ERASEBKGND, and a
             * control sitting exactly on the box covers it. */
            dwp = DeferWindowPos(
                dwp, control, NULL, x + CHROME_GDI_FIELD_PAD_X,
                y + cap_h + CHROME_GDI_TEXTAREA_PAD_Y,
                row_w - 2 * CHROME_GDI_FIELD_PAD_X,
                chrome_gdi_textarea_h(s, i) - 2 * CHROME_GDI_TEXTAREA_PAD_Y,
                SWP_NOZORDER | SWP_SHOWWINDOW);
            y += row_h + CHROME_GDI_ROW_GAP;
            continue;
        }

        /* Labelled controls start at the shared label column; the caption
         * STATIC created by the ADD is placed in the column beside them. */
        if( w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_DROPDOWN ||
            w->kind == TORIRS_CHROME_W_COLORPICK )
        {
            if( s->label[i] )
                dwp = DeferWindowPos(
                    dwp, s->label[i], NULL, x, y, CHROME_GDI_LABEL_W, row_h,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
            x += CHROME_GDI_LABEL_W;
            row_w -= CHROME_GDI_LABEL_W;
        }
        /* The sample sits inside the field box at its left, so a colour row is
         * a text row with a swatch in front of it rather than a second box. */
        if( w->kind == TORIRS_CHROME_W_COLORPICK && s->swatch[i] )
        {
            dwp = DeferWindowPos(
                dwp, s->swatch[i], NULL, x + CHROME_GDI_FIELD_INSET,
                y + (row_h - CHROME_GDI_SWATCH) / 2, CHROME_GDI_SWATCH,
                CHROME_GDI_SWATCH, SWP_NOZORDER | SWP_SHOWWINDOW);
            x += CHROME_GDI_FIELD_INSET + CHROME_GDI_SWATCH + CHROME_GDI_SWATCH_GAP;
            row_w -= CHROME_GDI_FIELD_INSET + CHROME_GDI_SWATCH + CHROME_GDI_SWATCH_GAP;
        }
        if( row_w < CHROME_GDI_PX(16) )
            row_w = CHROME_GDI_PX(16);

        /*
         * A typed control is INSET inside its field box.
         *
         * The box is script_3850's two-colour frame, and the parent draws it in
         * WM_ERASEBKGND from this control's own rect grown by the inset. The
         * control sitting exactly on the box would cover the frame it is
         * supposed to be wearing.
         */
        if( w->kind == TORIRS_CHROME_W_TEXTINPUT || w->kind == TORIRS_CHROME_W_COLORPICK )
            dwp = DeferWindowPos(
                dwp, control, NULL, x + CHROME_GDI_FIELD_PAD_X,
                y + CHROME_GDI_FIELD_INSET, row_w - 2 * CHROME_GDI_FIELD_PAD_X,
                row_h - 2 * CHROME_GDI_FIELD_INSET, SWP_NOZORDER | SWP_SHOWWINDOW);
        else
            dwp = DeferWindowPos(dwp, control, NULL, x, y, row_w, row_h,
                                 SWP_NOZORDER | SWP_SHOWWINDOW);
        y += row_h + CHROME_GDI_ROW_GAP;
    }

    EndDeferWindowPos(dwp);

    /*
     * Repaint the window and everything in it, every time rows move.
     *
     * The background is drawn against the CONTROLS' boxes -- the field frame
     * under every EDIT is placed from that control's rect in WM_ERASEBKGND --
     * so a row that moved leaves its old frame painted where it was and its
     * new one unpainted. The symptom is an empty box floating beside every
     * field, and it survives until something else happens to invalidate the
     * window. Here rather than at each of the half-dozen callers, so a new one
     * cannot forget.
     */
    /* Child moves/shows already invalidate the controls that need repainting.
     * Invalidating them all a second time exposes the textured parent between
     * their erase and paint passes, which is the page-switch flicker. */
    RedrawWindow(s->hwnd, NULL, NULL, RDW_INVALIDATE);
}

/* ---- drawing the controls -------------------------------------------------
 *
 * One WM_DRAWITEM handler for every owner-drawn control in the window. Which
 * one it is comes from the control ID's block -- a tab, a row's settings
 * affordance, or a widget -- and then from the widget's kind, which is what
 * the mirror is for.
 */

/** The caption a control is carrying. Read from the CONTROL rather than
 *  shadowed: SetWindowTextA already put it there, and a second copy here would
 *  be a second thing for WIDGET_LABEL to have to update. */
static void
chrome_gdi_caption(HWND control, char* out, int cap)
{
    out[0] = '\0';
    if( control )
        GetWindowTextA(control, out, cap);
}

/** A caption drawn into a box, in one of the palette's colours. */
static void
chrome_gdi_text(HDC dc, RECT box, char const* text, uint32_t rgb, UINT align)
{
    if( !text || !text[0] )
        return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, chrome_gdi_colorref(rgb));
    DrawTextA(
        dc, text, -1, &box,
        align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/** Edge of the box a checkbox reserves, for the art this window was told to
 *  wear -- sized to the sprite, because these blit at 1:1 or nearest-neighbour
 *  and an off-size one speckles. */
static int
chrome_gdi_box(struct ChromeGdi const* s)
{
    return s->check_style == TORIRS_CHROME_CHECK_STYLE_BOX ? CHROME_GDI_BOX_SQUARE
                                                           : CHROME_GDI_BOX_TICK;
}

/**
 * The interfaces' on/off pair, right-aligned in whatever box it is given.
 *
 * Right-aligned rather than centred because a LISTROW's switch is a 24-wide
 * hit box around 17-wide art and the slack belongs on the left, between the
 * sprite and the settings well -- the same placement the in-canvas chrome and
 * the CS2 executor both use. A checkbox's box is exactly the art, so for that
 * one the two are the same thing.
 */
static void
chrome_gdi_draw_mark(struct ChromeGdi* s, HDC dc, RECT box, int on)
{
    int const slot = ToriRSChrome_CheckSlot(s->check_style, on);
    int const h = box.bottom - box.top;
    int const art = chrome_gdi_box(s);
    int const side = h < art ? h : art;
    int const x = box.right - side;
    int const y = box.top + (h - side) / 2;

    if( ToriRSChromeSkin_ForSlot(slot) )
    {
        chrome_gdi_blit(s, dc, x, y, side, side, slot);
        return;
    }
    /* No skin: the flat box-and-blob every other presentation falls back to. */
    chrome_gdi_rect(dc, x, y, side, side, TORIRS_CHROME_C_FIELD_BG);
    chrome_gdi_outline(dc, x, y, side, side, TORIRS_CHROME_C_FRAME_INSET);
    if( on )
        chrome_gdi_rect(
            dc, x + CHROME_GDI_PX(3), y + CHROME_GDI_PX(3), side - CHROME_GDI_PX(6),
            side - CHROME_GDI_PX(6), TORIRS_CHROME_C_ON);
}

/**
 * The window's X: the interfaces' own button, or a flat one when nothing baked.
 *
 * The hover is IN THE ART -- the pair is one button lit from opposite corners
 * -- so the pressed state swaps the sprite and draws no outline over it. A
 * control carrying two hover indications reads as selected rather than as
 * under the cursor, which is the same rule the in-canvas chrome follows.
 */
static void
chrome_gdi_draw_close(struct ChromeGdi* s, HDC dc, RECT box, int hot)
{
    int const slot = hot ? TORIRS_CHROME_SKIN_CLOSE_OVER : TORIRS_CHROME_SKIN_CLOSE;
    int const w = box.right - box.left;
    int const h = box.bottom - box.top;

    if( ToriRSChromeSkin_ForSlot(slot) )
    {
        chrome_gdi_blit(s, dc, box.left, box.top, w, h, slot);
        return;
    }
    /* No skin: a framed box in the dismiss colour, so the way out still exists
     * on a build that baked no art. The outline comes back with it -- a flat
     * box has no bevel to invert. */
    chrome_gdi_field(s, dc, box.left, box.top, w, h);
    if( hot )
        chrome_gdi_outline(dc, box.left, box.top, w, h, TORIRS_CHROME_C_ACCENT);
    chrome_gdi_text(dc, box, "X", TORIRS_CHROME_C_TEXT, DT_CENTER);
}

/** The borderless window's non-client chrome, drawn in its client area.
 * Geometry matches the minimenu header used by the in-canvas presentation:
 * black title bar, body-coloured title, a short body gap, then a black rule. */
static void
chrome_gdi_draw_title(struct ChromeGdi* s, HDC dc, int width)
{
    RECT text;
    char caption[TORIRS_CHROME_TEXT_MAX];
    int const x = CHROME_GDI_FRAME;
    int const y = CHROME_GDI_FRAME;
    int const w = width - 2 * CHROME_GDI_FRAME;

    if( w <= 0 )
        return;
    chrome_gdi_rect(dc, x, y, w, CHROME_GDI_TITLE_H, TORIRS_CHROME_C_CHROME);
    chrome_gdi_rect(
        dc, x, y + CHROME_GDI_TITLE_H + CHROME_GDI_TITLE_GAP, w,
        CHROME_GDI_RULE, TORIRS_CHROME_C_CHROME);
    chrome_gdi_caption(s->hwnd, caption, (int)sizeof(caption));
    text.left = x + CHROME_GDI_FIELD_PAD_X;
    text.top = y;
    text.right = x + w - CHROME_GDI_CLOSE_PAD - CHROME_GDI_CLOSE_SIDE;
    text.bottom = y + CHROME_GDI_TITLE_H;
    chrome_gdi_text(dc, text, caption, TORIRS_CHROME_C_BODY, DT_LEFT);
}

/** ~script31's scrollbar: arrow buttons, tiled track, and three-piece grip. */
static void
chrome_gdi_draw_scrollbar(struct ChromeGdi* s, HDC dc, RECT const* client)
{
    struct ChromeGdiScrollbar bar;

    if( !s->scrollbar_visible || !chrome_gdi_scrollbar_geometry(s, client, &bar) )
        return;

    if( ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_SCROLL_TRACK) )
        chrome_gdi_blit(
            s, dc, bar.x, bar.track_y, CHROME_GDI_SCROLL_W, bar.track_h,
            TORIRS_CHROME_SKIN_SCROLL_TRACK);
    else
        chrome_gdi_rect(
            dc, bar.x, bar.track_y, CHROME_GDI_SCROLL_W, bar.track_h,
            TORIRS_CHROME_C_SCROLL_TRACK);

    if( ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_SCROLL_GRIP_MID) )
    {
        chrome_gdi_blit(
            s, dc, bar.x, bar.grip_y, CHROME_GDI_SCROLL_W, bar.grip_h,
            TORIRS_CHROME_SKIN_SCROLL_GRIP_MID);
        chrome_gdi_blit(
            s, dc, bar.x, bar.grip_y, CHROME_GDI_SCROLL_W,
            CHROME_GDI_SCROLL_CAP_H, TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP);
        chrome_gdi_blit(
            s, dc, bar.x, bar.grip_y + bar.grip_h - CHROME_GDI_SCROLL_CAP_H,
            CHROME_GDI_SCROLL_W, CHROME_GDI_SCROLL_CAP_H,
            TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM);
    }
    else
    {
        chrome_gdi_rect(
            dc, bar.x, bar.grip_y, CHROME_GDI_SCROLL_W, bar.grip_h,
            TORIRS_CHROME_C_SCROLL_GRIP);
        chrome_gdi_rect(
            dc, bar.x, bar.grip_y, CHROME_GDI_RULE, bar.grip_h,
            TORIRS_CHROME_C_SCROLL_GRIP_HI);
        chrome_gdi_rect(
            dc, bar.x, bar.grip_y, CHROME_GDI_SCROLL_W, CHROME_GDI_RULE,
            TORIRS_CHROME_C_SCROLL_GRIP_HI);
        chrome_gdi_rect(
            dc, bar.x + CHROME_GDI_SCROLL_W - CHROME_GDI_RULE, bar.grip_y,
            CHROME_GDI_RULE, bar.grip_h, TORIRS_CHROME_C_SCROLL_GRIP_LO);
        chrome_gdi_rect(
            dc, bar.x, bar.grip_y + bar.grip_h - CHROME_GDI_RULE,
            CHROME_GDI_SCROLL_W, CHROME_GDI_RULE, TORIRS_CHROME_C_SCROLL_GRIP_LO);
    }

    if( ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_SCROLL_UP) )
        chrome_gdi_blit(
            s, dc, bar.x, bar.top, CHROME_GDI_SCROLL_W, CHROME_GDI_SCROLL_W,
            TORIRS_CHROME_SKIN_SCROLL_UP);
    else
    {
        RECT box = { bar.x, bar.top, bar.x + CHROME_GDI_SCROLL_W,
                     bar.top + CHROME_GDI_SCROLL_W };
        chrome_gdi_rect(
            dc, bar.x, bar.top, CHROME_GDI_SCROLL_W, CHROME_GDI_SCROLL_W,
            TORIRS_CHROME_C_SCROLL_GRIP);
        chrome_gdi_text(dc, box, "^", TORIRS_CHROME_C_ACCENT, DT_CENTER);
    }
    if( ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_SCROLL_DOWN) )
        chrome_gdi_blit(
            s, dc, bar.x, bar.bottom - CHROME_GDI_SCROLL_W,
            CHROME_GDI_SCROLL_W, CHROME_GDI_SCROLL_W,
            TORIRS_CHROME_SKIN_SCROLL_DOWN);
    else
    {
        RECT box = { bar.x, bar.bottom - CHROME_GDI_SCROLL_W,
                     bar.x + CHROME_GDI_SCROLL_W, bar.bottom };
        chrome_gdi_rect(
            dc, box.left, box.top, CHROME_GDI_SCROLL_W, CHROME_GDI_SCROLL_W,
            TORIRS_CHROME_C_SCROLL_GRIP);
        chrome_gdi_text(dc, box, "v", TORIRS_CHROME_C_ACCENT, DT_CENTER);
    }
}

/** A roster row's settings affordance: three dots in a field-chrome well. */
static void
chrome_gdi_draw_dots(struct ChromeGdi* s, HDC dc, RECT box, int hot)
{
    int const w = box.right - box.left;
    int const h = box.bottom - box.top;

    chrome_gdi_field(s, dc, box.left, box.top, w, h);
    if( hot )
        chrome_gdi_outline(dc, box.left, box.top, w, h, TORIRS_CHROME_C_ACCENT);
    for( int d = 0; d < 3; d++ )
        chrome_gdi_rect(
            dc,
            box.left + CHROME_GDI_DOT_INSET + d * CHROME_GDI_DOT_PITCH,
            box.top + h / 2 - CHROME_GDI_RULE,
            CHROME_GDI_DOT,
            CHROME_GDI_DOT,
            TORIRS_CHROME_C_LABEL);
}

/** A pressable box: the settings field with a caption centred in it, which is
 *  how the reference draws a button (script_3850's own Save is this shape). */
static void
chrome_gdi_draw_button(
    struct ChromeGdi* s, HDC dc, RECT box, char const* caption, int pressed, uint32_t rgb)
{
    int const nudge = pressed ? CHROME_GDI_RULE : 0;

    chrome_gdi_field(
        s, dc, box.left, box.top, box.right - box.left, box.bottom - box.top);
    if( pressed )
        chrome_gdi_outline(
            dc, box.left, box.top, box.right - box.left, box.bottom - box.top,
            TORIRS_CHROME_C_ACCENT);
    box.left += CHROME_GDI_FIELD_INSET + nudge;
    box.right -= CHROME_GDI_FIELD_INSET;
    box.top += nudge;
    chrome_gdi_text(dc, box, caption, rgb, DT_CENTER);
}

/**
 * A tab, as the in-canvas strip draws one.
 *
 * The selected tab is the panel body itself; an unselected one is that body
 * under a veil, which here is a flat darkening rather than a translucent rect
 * because GDI has no per-primitive alpha without msimg32. The rules around it
 * are what make a strip read as tabs rather than as a row of buttons.
 */
static void
chrome_gdi_draw_tab(struct ChromeGdi* s, HDC dc, RECT box, char const* caption, int on)
{
    int const w = box.right - box.left;
    int const h = box.bottom - box.top;

    if( on )
        chrome_gdi_tile(s, dc, box.left, box.top, w, h);
    else
        chrome_gdi_rect(dc, box.left, box.top, w, h, 0x1B1813);
    chrome_gdi_rect(dc, box.left, box.top, w, CHROME_GDI_RULE, TORIRS_CHROME_C_CHROME);
    chrome_gdi_rect(dc, box.left, box.top, CHROME_GDI_RULE, h, TORIRS_CHROME_C_CHROME);
    chrome_gdi_rect(
        dc, box.right - CHROME_GDI_RULE, box.top, CHROME_GDI_RULE, h,
        TORIRS_CHROME_C_CHROME);
    /* No bottom rule on the selected tab: the gap is the joint to the content
     * below, and it is the whole of what makes this a strip. */
    if( !on )
        chrome_gdi_rect(
            dc, box.left, box.bottom - CHROME_GDI_RULE, w, CHROME_GDI_RULE,
            TORIRS_CHROME_C_CHROME);
    box.left += CHROME_GDI_PX(TORIRS_CHROME_M_TAB_PAD_X);
    box.right -= CHROME_GDI_PX(TORIRS_CHROME_M_TAB_PAD_X);
    chrome_gdi_text(
        dc, box, caption, on ? TORIRS_CHROME_C_TEXT : TORIRS_CHROME_C_LABEL, DT_CENTER);
}

/**
 * The closed dropdown, and one row of its open list.
 *
 * A COMBOBOX raises WM_DRAWITEM for both, told apart by ODS_COMBOBOXEDIT. The
 * closed button gets the field box with the scrollbar's own down arrow at the
 * right and the value centred in the settings orange -- script_3850's layout,
 * arrow included, because the reference literally reuses that sprite. A list
 * row gets the cache's own lighter list tile.
 */
static void
chrome_gdi_draw_combo(struct ChromeGdi* s, DRAWITEMSTRUCT const* di)
{
    RECT box = di->rcItem;
    int const w = box.right - box.left;
    int const h = box.bottom - box.top;
    char caption[TORIRS_CHROME_TEXT_MAX];

    caption[0] = '\0';
    if( (int)di->itemID >= 0 )
        SendMessageA(di->hwndItem, CB_GETLBTEXT, (WPARAM)di->itemID, (LPARAM)caption);

    if( di->itemState & ODS_COMBOBOXEDIT )
    {
        /*
         * The closed button, and the ONE place this window cannot match the
         * CS2 panel.
         *
         * A CBS_DROPDOWNLIST combo draws its own drop-down arrow, in the
         * shell's 3D style, in a strip to the right that is NOT part of the
         * owner-draw rect -- Windows keeps that button for itself and there is
         * no style that takes it away. So the scrollbar's own down-arrow
         * sprite (which is what script_3850 puts there, and what every other
         * presentation of this window draws) is deliberately NOT blitted: two
         * arrows side by side reads worse than one that is the wrong shape.
         *
         * The alternative was to drop the COMBOBOX for a button that CYCLES
         * its options -- which is what the CS2 executor does, having no popup
         * at all. Not taken: a real list is what a Windows user reaches for,
         * and the point of a native executor is the platform's own controls.
         */
        chrome_gdi_field(s, di->hDC, box.left, box.top, w, h);
        box.left += CHROME_GDI_FIELD_INSET;
        box.right -= CHROME_GDI_FIELD_INSET;
        chrome_gdi_text(di->hDC, box, caption, TORIRS_CHROME_C_LABEL, DT_CENTER);
        return;
    }

    /* An open row. The cache backs the floating list with its OWN, lighter
     * tile -- a different image from the panel body, which is why the bake
     * carries both. */
    if( ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_DROPDOWN_BODY) )
        chrome_gdi_blit(
            s, di->hDC, box.left, box.top, w, h, TORIRS_CHROME_SKIN_DROPDOWN_BODY);
    else
        chrome_gdi_rect(di->hDC, box.left, box.top, w, h, TORIRS_CHROME_C_BODY);
    /* The hovered row is a veil lifted rather than a colour painted on, which
     * is how script_9114 highlights one. Flat here, for the same reason the
     * tab veil is. */
    if( di->itemState & ODS_SELECTED )
        chrome_gdi_outline(di->hDC, box.left, box.top, w, h, TORIRS_CHROME_C_ACCENT);
    box.left += CHROME_GDI_FIELD_PAD_X;
    chrome_gdi_text(di->hDC, box, caption, TORIRS_CHROME_C_TEXT, DT_LEFT);
}

/**
 * The colour a STATIC's text is set in.
 *
 * script_3850 sets an ENABLED setting's caption in 0xff981f, so a labelled
 * row's caption is orange and everything else -- a roster row's name, a bare
 * LABEL -- is body white. Answered from which widget the HWND belongs to,
 * because both are the same control class and there is nothing else to ask.
 */
static uint32_t
chrome_gdi_static_color(struct ChromeGdi const* s, HWND control)
{
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
    {
        if( s->label[i] == control )
        {
            struct ToriRSChromeMirrorWidget const* w = &s->mirror.widgets[i];
            if( s->color[i] )
                return s->color[i];
            return w->kind == TORIRS_CHROME_W_LISTROW ? TORIRS_CHROME_C_TEXT
                                                       : TORIRS_CHROME_C_LABEL;
        }
        if( s->mirror.widgets[i].native == (intptr_t)control )
            return s->color[i] ? s->color[i] : TORIRS_CHROME_C_TEXT;
    }
    return TORIRS_CHROME_C_TEXT;
}

/** Dispatch one owner-draw. */
static void
chrome_gdi_drawitem(struct ChromeGdi* s, DRAWITEMSTRUCT const* di)
{
    char caption[TORIRS_CHROME_TEXT_MAX];
    int const pressed = (di->itemState & ODS_SELECTED) != 0;
    int handle;

    if( di->CtlType == ODT_COMBOBOX )
    {
        chrome_gdi_draw_combo(s, di);
        return;
    }

    if( (int)di->CtlID >= CHROME_GDI_ID_TAB_BASE &&
        (int)di->CtlID < CHROME_GDI_ID_TAB_BASE + 16 )
    {
        int const t = (int)di->CtlID - CHROME_GDI_ID_TAB_BASE;
        struct ToriRSChromeMirrorPanel* panel =
            s->tab_panel >= 0 ? &s->mirror.panels[s->tab_panel] : NULL;
        chrome_gdi_draw_tab(
            s, di->hDC, di->rcItem, s->tabs[t], panel && panel->active_tab == t);
        return;
    }

    if( (int)di->CtlID == CHROME_GDI_ID_CLOSE )
    {
        chrome_gdi_draw_close(s, di->hDC, di->rcItem, pressed);
        return;
    }

    if( (int)di->CtlID >= CHROME_GDI_ID_ACTION_BASE &&
        (int)di->CtlID < CHROME_GDI_ID_ACTION_BASE + TORIRS_CHROME_MAX_WIDGETS )
    {
        chrome_gdi_draw_dots(s, di->hDC, di->rcItem, pressed);
        return;
    }

    handle = (int)di->CtlID - CHROME_GDI_ID_BASE;
    if( handle < 0 || handle >= TORIRS_CHROME_MAX_WIDGETS )
        return;
    {
        struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, handle);
        RECT box = di->rcItem;

        if( !w )
            return;
        chrome_gdi_caption(di->hwndItem, caption, (int)sizeof(caption));
        switch( w->kind )
        {
        case TORIRS_CHROME_W_CHECKBOX:
        {
            RECT mark = box;
            int const side = chrome_gdi_box(s);
            /* WS_CLIPCHILDREN prevents the parent from erasing through this
             * control. Paint the tile here before transparent mark/text, or a
             * moved checkbox leaves every prior caption underneath it. */
            chrome_gdi_tile(
                s, di->hDC, box.left, box.top, box.right - box.left,
                box.bottom - box.top);
            mark.right = mark.left + side;
            chrome_gdi_draw_mark(s, di->hDC, mark, s->checked[handle]);
            box.left += side + CHROME_GDI_CHECK_GAP;
            chrome_gdi_text(
                di->hDC, box, caption,
                s->color[handle] ? s->color[handle] : TORIRS_CHROME_C_TEXT, DT_LEFT);
            break;
        }

        case TORIRS_CHROME_W_LISTROW:
            /* Only the SWITCH: a roster row's name is a STATIC of its own and
             * its settings well is the parallel control, so this box is just
             * the toggle's hit area. */
            chrome_gdi_tile(
                s, di->hDC, box.left, box.top, box.right - box.left,
                box.bottom - box.top);
            chrome_gdi_draw_mark(s, di->hDC, box, s->checked[handle]);
            break;

        case TORIRS_CHROME_W_BUTTON:
        case TORIRS_CHROME_W_MENUITEM:
            chrome_gdi_draw_button(
                s, di->hDC, box, caption, pressed,
                s->color[handle] ? s->color[handle] : TORIRS_CHROME_C_TEXT);
            break;

        case TORIRS_CHROME_W_SEPARATOR:
            chrome_gdi_tile(
                s, di->hDC, box.left, box.top, box.right - box.left,
                box.bottom - box.top);
            chrome_gdi_rect(
                di->hDC, box.left, box.top + (box.bottom - box.top) / 2,
                box.right - box.left, CHROME_GDI_RULE, TORIRS_CHROME_C_CHROME);
            break;

        case TORIRS_CHROME_W_MODELVIEW:
            /* A native executor has no scene or model pixels. Preserve the
             * component as an explicit framed preview placeholder, like the
             * DOM executor, instead of silently turning it into a bare label. */
            chrome_gdi_field(
                s, di->hDC, box.left, box.top, box.right - box.left,
                box.bottom - box.top);
            chrome_gdi_text(
                di->hDC, box, caption[0] ? caption : "model preview",
                s->color[handle] ? s->color[handle] : TORIRS_CHROME_C_LABEL,
                DT_CENTER);
            break;

        default:
            break;
        }
    }
}

/* ---- the window ----------------------------------------------------------- */

static LRESULT CALLBACK
chrome_gdi_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    struct ChromeGdi* s = &g_chrome_gdi;

    switch( msg )
    {
    case WM_COMMAND:
    {
        int const id = LOWORD(wp);
        int const notify = HIWORD(wp);

        /*
         * The client-area X. Reported exactly as the caption's X is (WM_CLOSE,
         * below) and through the same intent, so the two ways out of this
         * window cannot come to mean different things -- and neither destroys
         * anything here: the MODEL decides whether the window is up.
         */
        if( id == CHROME_GDI_ID_CLOSE )
        {
            struct ToriRSChromeIntent intent;

            if( notify != BN_CLICKED )
                return 0;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_INTENT_CLOSE;
            intent.panel = s->window_panel;
            intent.widget = -1;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            return 0;
        }

        /* A tab button: reported, not acted on. The MODEL decides which tab is
         * up -- it answers with a PANEL_TAB command -- and switching here as
         * well would make the window and the model disagree the moment
         * anything else moved the tab. */
        if( id >= CHROME_GDI_ID_TAB_BASE && id < CHROME_GDI_ID_TAB_BASE + 16 )
        {
            struct ToriRSChromeIntent intent;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_INTENT_TAB;
            intent.panel = s->tab_panel;
            intent.widget = s->tab_strip_widget;
            intent.value = id - CHROME_GDI_ID_TAB_BASE;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            return 0;
        }

        /* A roster row's settings affordance. Its own id block, because the
         * row's two zones report DIFFERENT intents -- ACTION opens the entry's
         * page, TOGGLE flips it -- and one owner-drawn BUTTON reports a click
         * without saying where in itself it landed. */
        if( id >= CHROME_GDI_ID_ACTION_BASE &&
            id < CHROME_GDI_ID_ACTION_BASE + TORIRS_CHROME_MAX_WIDGETS )
        {
            int const handle = id - CHROME_GDI_ID_ACTION_BASE;
            struct ToriRSChromeMirrorWidget* w =
                ToriRSChromeMirror_Widget(&s->mirror, handle);
            struct ToriRSChromeIntent intent;

            if( !w || notify != BN_CLICKED )
                return 0;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_INTENT_ACTION;
            intent.panel = w->panel;
            intent.widget = handle;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            return 0;
        }

        if( id >= CHROME_GDI_ID_BASE && id < CHROME_GDI_ID_BASE + TORIRS_CHROME_MAX_WIDGETS )
        {
            int const handle = id - CHROME_GDI_ID_BASE;
            struct ToriRSChromeMirrorWidget* w =
                ToriRSChromeMirror_Widget(&s->mirror, handle);
            HWND control = (HWND)lp;

            if( !w )
                return 0;

            switch( w->kind )
            {
            case TORIRS_CHROME_W_CHECKBOX:
            case TORIRS_CHROME_W_LISTROW:
                /*
                 * The state is OURS now, not the control's.
                 *
                 * BS_AUTOCHECKBOX kept its own tick and answered BM_GETCHECK;
                 * a BS_OWNERDRAW button draws what it is told and knows
                 * nothing, so the click flips the shadow bit and the redraw
                 * shows it. Reported either way, and the MODEL is still what
                 * decides -- a WIDGET_CHECKED that disagrees overwrites this
                 * on the next command, which is how a refused toggle snaps
                 * back.
                 */
                if( notify != BN_CLICKED )
                    break;
                s->checked[handle] = s->checked[handle] ? 0 : 1;
                InvalidateRect(control, NULL, TRUE);
                ToriRSChromeMirror_PushToggle(
                    &s->mirror, w->panel, handle, s->checked[handle]);
                break;

            case TORIRS_CHROME_W_TEXTINPUT:
            case TORIRS_CHROME_W_TEXTAREA:
            case TORIRS_CHROME_W_COLORPICK:
                /* EN_KILLFOCUS, not EN_CHANGE: an intent per keystroke would
                 * send the model a value for every half-typed state, and the
                 * chrome's own input commits the same way. A colour row's EDIT
                 * commits by the same route -- the model is what turns the hex
                 * into a palette entry, so there is nothing extra to do here. */
                if( notify == EN_SETFOCUS )
                {
                    struct ToriRSChromeIntent intent;
                    memset(&intent, 0, sizeof(intent));
                    intent.kind = w->kind == TORIRS_CHROME_W_COLORPICK
                                      ? TORIRS_CHROME_INTENT_ACTION
                                      : TORIRS_CHROME_INTENT_ACTIVATE;
                    intent.panel = w->panel;
                    intent.widget = handle;
                    ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
                }
                else if( notify == EN_KILLFOCUS )
                {
                    /* Read at the CONTROL's size and normalised down to the
                     * model's: a multiline value carries a '\r' per line that
                     * the model has no room and no use for. */
                    char raw[TORIRS_CHROME_TEXT_MAX * 2];
                    char buf[TORIRS_CHROME_TEXT_MAX];
                    GetWindowTextA(control, raw, (int)sizeof(raw));
                    chrome_gdi_to_lf(buf, (int)sizeof(buf), raw);
                    ToriRSChromeMirror_PushText(&s->mirror, w->panel, handle, buf);
                }
                break;

            case TORIRS_CHROME_W_DROPDOWN:
                if( notify == CBN_SELCHANGE )
                {
                    struct ToriRSChromeIntent intent;
                    int const sel = (int)SendMessageA(control, CB_GETCURSEL, 0, 0);
                    memset(&intent, 0, sizeof(intent));
                    intent.kind = TORIRS_CHROME_INTENT_PICK;
                    intent.panel = w->panel;
                    intent.widget = handle;
                    intent.value = sel;
                    SendMessageA(control, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)intent.text);
                    ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
                }
                break;

            default:
                if( notify == BN_CLICKED )
                    ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
                break;
            }
        }
        return 0;
    }

    case WM_SIZE:
        chrome_gdi_layout(s);
        /* Everything is painted against the window's own box -- the tiling, the
         * frame, the field boxes under the EDITs -- so a resize invalidates all
         * of it rather than the newly exposed strip Windows would repaint. */
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE);
        return 0;

    case WM_NCHITTEST:
    {
        /* The window has no operating-system frame. Its black title band is
         * the non-client area now: returning HTCAPTION lets USER32 perform the
         * move loop, including capture, monitor transitions and snapping,
         * without reimplementing any of those from mouse messages. Explicitly
         * leave Close as a client-area hole: WM_NCHITTEST reaches the parent
         * before USER32 decides which child owns the point. */
        POINT point;
        POINT screen;
        RECT client;
        RECT close;
        screen.x = (LONG)(short)LOWORD(lp);
        screen.y = (LONG)(short)HIWORD(lp);
        if( s->close_button && GetWindowRect(s->close_button, &close) &&
            screen.x >= close.left && screen.x < close.right &&
            screen.y >= close.top && screen.y < close.bottom )
            return HTCLIENT;
        point = screen;
        ScreenToClient(hwnd, &point);
        GetClientRect(hwnd, &client);
        if( point.x >= CHROME_GDI_FRAME && point.x < client.right - CHROME_GDI_FRAME &&
            point.y >= CHROME_GDI_FRAME &&
            point.y < CHROME_GDI_FRAME + CHROME_GDI_TITLE_H )
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN:
    {
        RECT client;
        struct ChromeGdiScrollbar bar;
        int const x = (LONG)(short)LOWORD(lp);
        int const y = (LONG)(short)HIWORD(lp);

        GetClientRect(hwnd, &client);
        if( !s->scrollbar_visible || !chrome_gdi_scrollbar_geometry(s, &client, &bar) ||
            x < bar.x || x >= bar.x + CHROME_GDI_SCROLL_W ||
            y < bar.top || y >= bar.bottom )
            break;
        if( y < bar.track_y )
            chrome_gdi_scroll_to(s, s->scroll_y - CHROME_GDI_SCROLL_LINE);
        else if( y >= bar.bottom - CHROME_GDI_SCROLL_W )
            chrome_gdi_scroll_to(s, s->scroll_y + CHROME_GDI_SCROLL_LINE);
        else if( y >= bar.grip_y && y < bar.grip_y + bar.grip_h )
        {
            s->scrollbar_dragging = 1;
            s->scrollbar_drag_offset = y - bar.grip_y;
            SetCapture(hwnd);
        }
        else
            chrome_gdi_scroll_to(
                s, s->scroll_y + (y < bar.grip_y ? -1 : 1) * (bar.bottom - bar.top));
        return 0;
    }

    case WM_MOUSEMOVE:
        if( s->scrollbar_dragging )
        {
            RECT client;
            struct ChromeGdiScrollbar bar;
            int const y = (LONG)(short)HIWORD(lp);
            int travel;
            int grip_top;

            GetClientRect(hwnd, &client);
            if( chrome_gdi_scrollbar_geometry(s, &client, &bar) )
            {
                travel = bar.track_h - bar.grip_h;
                grip_top = y - s->scrollbar_drag_offset;
                if( travel > 0 )
                    chrome_gdi_scroll_to(
                        s, (grip_top - bar.track_y) * bar.max_scroll / travel);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if( s->scrollbar_dragging )
        {
            s->scrollbar_dragging = 0;
            if( GetCapture() == hwnd )
                ReleaseCapture();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        s->scrollbar_dragging = 0;
        break;

    case WM_MOUSEWHEEL:
    {
        int const notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;

        chrome_gdi_scroll_to(s, s->scroll_y - notches * CHROME_GDI_SCROLL_LINE);
        return 0;
    }

    case WM_DRAWITEM:
        chrome_gdi_drawitem(s, (DRAWITEMSTRUCT const*)lp);
        return 1;

    case WM_MEASUREITEM:
        /* An owner-drawn list keeps the FONT's item height unless it is told
         * otherwise, so without this the rows of an open dropdown are a
         * different pitch from the rows of the panel behind it. */
        ((MEASUREITEMSTRUCT*)lp)->itemHeight = CHROME_GDI_ROW_H;
        return 1;

    /*
     * The whole background, in one pass: tradebacking, the nine-slice border,
     * and the field box behind every EDIT.
     *
     * An EDIT cannot be owner-drawn -- it is the one control here whose
     * painting Windows keeps -- so its frame is drawn by the PARENT, under it,
     * and the control is placed inset by the frame's thickness so it covers
     * only the middle. That is also why this is WM_ERASEBKGND and not
     * WM_PAINT: erase runs before the children paint, so their own pixels land
     * on top of ours instead of being wiped by them.
     */
    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wp;
        RECT client;
        int order[TORIRS_CHROME_MAX_WIDGETS];
        int ordered;

        GetClientRect(hwnd, &client);
        chrome_gdi_tile(s, dc, 0, 0, client.right, client.bottom);
        chrome_gdi_draw_title(s, dc, client.right);
        chrome_gdi_draw_scrollbar(s, dc, &client);
        chrome_gdi_frame(s, dc, client.right, client.bottom);

        ordered = ToriRSChromeMirror_Order(&s->mirror, order, TORIRS_CHROME_MAX_WIDGETS);
        for( int oi = 0; oi < ordered; oi++ )
        {
            int const i = order[oi];
            struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
            RECT box;

            if( !w || !w->native || !s->presented[i] )
                continue;
            if( w->kind != TORIRS_CHROME_W_TEXTINPUT &&
                w->kind != TORIRS_CHROME_W_TEXTAREA &&
                w->kind != TORIRS_CHROME_W_COLORPICK )
                continue;
            if( !GetWindowRect((HWND)w->native, &box) )
                continue;
            {
                POINT tl;
                tl.x = box.left;
                tl.y = box.top;
                ScreenToClient(hwnd, &tl);
                chrome_gdi_field(
                    s,
                    dc,
                    (int)tl.x - CHROME_GDI_FIELD_INSET,
                    (int)tl.y - CHROME_GDI_FIELD_INSET,
                    (int)(box.right - box.left) + 2 * CHROME_GDI_FIELD_INSET,
                    (int)(box.bottom - box.top) + 2 * CHROME_GDI_FIELD_INSET);
            }
        }
        return 1;
    }

    case WM_CLOSE:
    {
        /* Reported, and the window stays up until the model says otherwise --
         * the same rule the tab buttons follow. Destroying it here would leave
         * the model presenting into a window that no longer exists. */
        struct ToriRSChromeIntent intent;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_CLOSE;
        intent.panel = s->window_panel;
        intent.widget = -1;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        /* A colour row's sample: the ONLY way to give a control a colour in
         * Win32 is to hand back a brush from here, so the swatches are found
         * by HWND and painted with the brush the last WIDGET_SELECTED made. */
        for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
            if( s->swatch[i] && s->swatch[i] == (HWND)lp && s->swatch_brush[i] )
                return (LRESULT)s->swatch_brush[i];
        /*
         * Every other STATIC is a caption over the tradebacking.
         *
         * A labelled row's caption is the settings orange script_3850 sets it
         * in; a roster row's name is body white. Told apart by the widget the
         * HWND belongs to, because the two are the same control class.
         *
         * The brush is the TILE, not the hollow one. Hollow was the first
         * answer -- the parent has already tiled this area, so why paint it
         * again -- and it is wrong for one reason: a STATIC erases with this
         * brush when its caption changes, and a brush that paints nothing
         * leaves the OLD caption on screen under the new one. A plugin renamed
         * by a reload came back as two names overprinted.
         *
         * The pattern's origin will not line up with the parent's tiling. It
         * does not matter: the tradebacking is noise, and a seam in noise is
         * invisible.
         */
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, chrome_gdi_colorref(chrome_gdi_static_color(s, (HWND)lp)));
        if( s->tile_brush )
            return (LRESULT)s->tile_brush;
        return (LRESULT)GetStockObject(NULL_BRUSH);

    case WM_CTLCOLORBTN:
        /* Owner-drawn buttons paint themselves; this only stops Windows
         * erasing the parchment under them first. */
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);

    /*
     * The typed controls: white on the tradebacking.
     *
     * An EDIT and a COMBOBOX's list keep their own painting, so the most this
     * can do is choose the ink and the ground. The ground is a PATTERN brush
     * over the tile, which is the only way to get a texture behind a control
     * in Win32 -- there is nowhere to put a paint callback, only somewhere to
     * return a brush from. Its origin will not line up with the parent's
     * tiling, and it does not matter: the tradebacking is noise, and a seam in
     * noise is invisible.
     */
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, chrome_gdi_colorref(TORIRS_CHROME_C_TEXT));
        if( s->tile_brush )
            return (LRESULT)s->tile_brush;
        return (LRESULT)GetStockObject(NULL_BRUSH);

    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/**
 * A pattern brush over the tradebacking, at the window's scale.
 *
 * The ONLY way to put a texture behind a Win32 control: WM_CTLCOLOR* returns a
 * brush, and there is nowhere to hand it a paint callback. So the tile is
 * blown up to CHROME_GDI_K, composited flat against the body brown (a brush
 * has no alpha either), and handed to CreatePatternBrush.
 *
 * Flattened against the brown rather than left transparent because the tile's
 * edges carry transparent pixels -- as a brush those would come out black.
 */
static void
chrome_gdi_make_tile_brush(struct ChromeGdi* s)
{
    struct ToriRSChromeSkin_Sprite const* tile =
        ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY);
    int const w = tile ? tile->w * CHROME_GDI_K : 0;
    int const h = tile ? tile->h * CHROME_GDI_K : 0;
    BITMAPINFO bi;
    uint32_t* pixels = NULL;
    HBITMAP bitmap;

    if( !tile || w <= 0 || h <= 0 )
        return;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    if( !bitmap || !pixels )
        return;

    for( int j = 0; j < h; j++ )
        for( int i = 0; i < w; i++ )
        {
            uint32_t const src =
                tile->argb[(size_t)(j / CHROME_GDI_K) * (size_t)tile->w +
                           (size_t)(i / CHROME_GDI_K)];
            unsigned const a = (src >> 24) & 0xFF;
            unsigned const br = (TORIRS_CHROME_C_BODY >> 16) & 0xFF;
            unsigned const bg = (TORIRS_CHROME_C_BODY >> 8) & 0xFF;
            unsigned const bb = TORIRS_CHROME_C_BODY & 0xFF;
            unsigned const r = (((src >> 16) & 0xFF) * a + br * (255 - a)) / 255;
            unsigned const g = (((src >> 8) & 0xFF) * a + bg * (255 - a)) / 255;
            unsigned const b = ((src & 0xFF) * a + bb * (255 - a)) / 255;
            pixels[(size_t)j * (size_t)w + (size_t)i] = (r << 16) | (g << 8) | b;
        }

    s->tile_bitmap = bitmap;
    s->tile_brush = CreatePatternBrush(bitmap);
}

static int
chrome_gdi_begin(void* user)
{
    struct ChromeGdi* s = user;
    WNDCLASSA wc;
    NONCLIENTMETRICSA ncm;

    assert(s);
    if( s->hwnd )
        return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = chrome_gdi_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* NO class brush: WM_ERASEBKGND tiles the whole client area itself, and a
     * class brush would flash the dialog face under it on every repaint. */
    wc.hbrBackground = NULL;
    wc.lpszClassName = CHROME_GDI_WNDCLASS;
    /* A duplicate class is not an error: begin() can run again after the model
     * closed the window, and the class outlives the window. */
    if( !RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
        return 0;

    s->hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_COMPOSITED,
        CHROME_GDI_WNDCLASS,
        "Plugins",
        /* Borderless by construction. WM_NCHITTEST turns the title we draw in
         * the client into USER32's drag handle; a native caption or resize
         * rail here would be a second, platform-themed chrome around it. */
        WS_POPUP | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CHROME_GDI_W,
        CHROME_GDI_H,
        s->owner,
        NULL,
        GetModuleHandleA(NULL),
        NULL);
    if( !s->hwnd )
        return 0;

    /* The shell's own UI font, so the window reads as part of the system
     * rather than as the 1990s SYSTEM_FONT a control defaults to. */
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if( SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) )
    {
        /* Geometry is deliberately 2x chrome. The unscaled message font was
         * the reason the screenshot had 36px fields around 11px captions.
         * Match the other native executor's 8px authored face at K=2. */
        ncm.lfMessageFont.lfHeight = -CHROME_GDI_PX(8);
        s->font = CreateFontIndirectA(&ncm.lfMessageFont);
    }

    chrome_gdi_make_tile_brush(s);

    /*
     * The close button, made once with the window rather than per relayout:
     * it is furniture, not a row, so nothing in the command stream creates or
     * destroys it. BS_OWNERDRAW for the same reason every other control here
     * is -- the art is the cache's and Windows cannot draw it.
     *
     * Created hidden, like every other control here: the layout pass is what
     * shows a control (SWP_SHOWWINDOW), and one that arrived visible would
     * flash at 0,0 in the corner until the first relayout moved it.
     */
    s->close_button = CreateWindowExA(
        0, "BUTTON", "", WS_CHILD | BS_OWNERDRAW, 0, 0,
        CHROME_GDI_CLOSE_SIDE, CHROME_GDI_CLOSE_SIDE, s->hwnd, (HMENU)(INT_PTR)CHROME_GDI_ID_CLOSE,
        GetModuleHandleA(NULL), NULL);

    ToriRSChromeMirror_Init(&s->mirror);
    s->window_panel = -1;
    s->tab_panel = -1;
    s->tab_strip_widget = -1;
    s->tab_count = 0;
    s->open = 1;
    ShowWindow(s->hwnd, SW_SHOWNOACTIVATE);
    return 1;
}

static void
chrome_gdi_end(void* user)
{
    struct ChromeGdi* s = user;

    assert(s);
    if( !s->open )
        return;
    /* The children go with the parent; Windows destroys them for us, which is
     * why nothing here walks the mirror to destroy controls one at a time. */
    if( s->hwnd )
        DestroyWindow(s->hwnd);
    if( s->font )
        DeleteObject(s->font);
    /* Every GDI object this window made. A tool window opened and closed a
     * dozen times a session leaks a bitmap, a brush and a DC each time
     * otherwise -- and GDI handles are a per-process quota, not merely
     * memory. */
    if( s->tile_brush )
        DeleteObject(s->tile_brush);
    if( s->tile_bitmap )
        DeleteObject(s->tile_bitmap);
    if( s->scratch_bitmap )
        DeleteObject(s->scratch_bitmap);
    if( s->scratch_dc )
        DeleteDC(s->scratch_dc);
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        if( s->swatch_brush[i] )
        {
            DeleteObject(s->swatch_brush[i]);
            s->swatch_brush[i] = NULL;
        }
    s->hwnd = NULL;
    s->close_button = NULL; /* a child: destroyed with the parent above. */
    s->font = NULL;
    s->tile_brush = NULL;
    s->tile_bitmap = NULL;
    s->scratch_bitmap = NULL;
    s->scratch_dc = NULL;
    s->scratch_pixels = NULL;
    s->scratch_w = 0;
    s->scratch_h = 0;
    s->open = 0;
    s->window_panel = -1;
    s->scroll_y = 0;
    s->content_h = 0;
    s->scrollbar_visible = 0;
    s->scrollbar_dragging = 0;
    s->scrollbar_drag_offset = 0;
    s->tab_count = 0;
    memset(s->tab_buttons, 0, sizeof(s->tab_buttons));
    memset(s->label, 0, sizeof(s->label));
    memset(s->action, 0, sizeof(s->action));
    memset(s->swatch, 0, sizeof(s->swatch));
}

/* ---- creating controls ---------------------------------------------------- */

static HWND
chrome_gdi_child(
    struct ChromeGdi* s, char const* cls, DWORD style, char const* text, int id)
{
    HWND h = CreateWindowExA(
        0,
        cls,
        text ? text : "",
        WS_CHILD | style,
        0,
        0,
        10,
        10,
        s->hwnd,
        (HMENU)(INT_PTR)id,
        GetModuleHandleA(NULL),
        NULL);
    if( h && s->font )
        SendMessageA(h, WM_SETFONT, (WPARAM)s->font, TRUE);
    return h;
}

/**
 * Destroy the controls a row owns BESIDE the one the mirror knows about.
 *
 * The mirror holds exactly one native per widget (see its header: it knows
 * what a widget is, not what it currently says), and a labelled row here owns
 * two more -- its caption STATIC and, on a colour row, its sample and the
 * brush that paints it. Leaving them behind is not a leak that shows up as
 * memory: it is a caption and a coloured square still sitting where a removed
 * row used to be, over whatever now occupies that space.
 */
static void
chrome_gdi_drop_extras(struct ChromeGdi* s, int widget)
{
    if( widget < 0 || widget >= TORIRS_CHROME_MAX_WIDGETS )
        return;
    if( s->label[widget] )
        DestroyWindow(s->label[widget]);
    s->label[widget] = NULL;
    if( s->action[widget] )
        DestroyWindow(s->action[widget]);
    s->action[widget] = NULL;
    if( s->swatch[widget] )
        DestroyWindow(s->swatch[widget]);
    s->swatch[widget] = NULL;
    if( s->swatch_brush[widget] )
        DeleteObject(s->swatch_brush[widget]);
    s->swatch_brush[widget] = NULL;
}

static void
chrome_gdi_add(struct ChromeGdi* s, struct ToriRSChromeCmd const* cmd)
{
    struct ToriRSChromeMirrorWidget* w =
        ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
    int const id = CHROME_GDI_ID_BASE + cmd->widget;
    HWND control = NULL;

    if( !w )
        return;

    s->checked[cmd->widget] = 0;
    s->color[cmd->widget] = cmd->color;
    s->presented[cmd->widget] = 0;
    /* The ADD is the one command carrying a widget's SHAPE, and `w` is a
     * LISTROW's settings affordance -- the same field the CS2 executor reads
     * it out of. A row that gained or lost one is re-added, not updated. */
    s->row_action[cmd->widget] = cmd->w ? 1 : 0;
    /* ...and `h` is a TEXTAREA's line count. Clamped on the way in, because
     * the number reaches the seam from a plugin manifest. */
    s->rows[cmd->widget] =
        (unsigned char)(cmd->h > 0
                            ? (cmd->h < TORIRS_CHROME_M_TEXTAREA_ROWS_MAX
                                   ? cmd->h
                                   : TORIRS_CHROME_M_TEXTAREA_ROWS_MAX)
                            : TORIRS_CHROME_M_TEXTAREA_ROWS);

    switch( cmd->value )
    {
    case TORIRS_CHROME_W_CHECKBOX:
        /* BS_OWNERDRAW, not BS_AUTOCHECKBOX: the mark is the interfaces' own
         * 17x17 tick/cross sprite, and there is no drawn checkbox anywhere in
         * this game to let Windows imitate. The auto-check behaviour goes with
         * it -- see the WM_COMMAND handler, which now owns the state. */
        control = chrome_gdi_child(s, "BUTTON", BS_OWNERDRAW, cmd->label, id);
        break;

    case TORIRS_CHROME_W_LISTROW:
        /*
         * A roster row is THREE controls, not one: the name, the settings
         * well, and the switch.
         *
         * Split because the row has two OUTCOMES and a single owner-drawn
         * button reports a click without saying where in itself it landed --
         * so one control could never tell "open this plugin's page" from "turn
         * it off". The CS2 executor splits it the same way and for the same
         * reason, and the ids line up: the switch takes the handle's own slot,
         * the well takes the parallel ACTION block.
         *
         * Before this, a LISTROW fell through to the default branch and became
         * a STATIC -- a row of plugin names with no switch and no way in.
         */
        s->label[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        if( cmd->w )
            s->action[cmd->widget] = chrome_gdi_child(
                s, "BUTTON", BS_OWNERDRAW, "", CHROME_GDI_ID_ACTION_BASE + cmd->widget);
        control = chrome_gdi_child(s, "BUTTON", BS_OWNERDRAW, "", id);
        break;

    case TORIRS_CHROME_W_TEXTINPUT:
        /* The label is a STATIC of its own, placed by the layout's label
         * column. It carries no id: nothing routes to it, and giving it one
         * would put a second control in the handle's id slot.
         *
         * No WS_BORDER: the field box around it is script_3850's two-colour
         * frame, drawn by the PARENT in WM_ERASEBKGND, and Windows' own etched
         * border inside that would be a third edge. */
        s->label[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        control = chrome_gdi_child(s, "EDIT", ES_AUTOHSCROLL, cmd->text, id);
        break;

    case TORIRS_CHROME_W_TEXTAREA:
    {
        /*
         * The same EDIT, multiline -- which is what Windows has been offering
         * for this since 1985, and the whole reason a native-widget executor
         * exists. It brings its own wrapping, its own caret, its own selection
         * and its own scrollbar; none of the model's line arithmetic reaches
         * here, and none of it has to.
         *
         * ES_WANTRETURN, and it is the one flag that is easy to miss: without
         * it a multiline EDIT in a dialog-style parent hands Return to the
         * default button instead of inserting a line, so the control looks
         * multiline and cannot be typed into on more than one line.
         *
         * The caption is placed ABOVE it rather than in the label column --
         * see TORIRS_CHROME_W_TEXTAREA -- but it is still an ordinary STATIC
         * with no id, exactly as a one-line row's is.
         */
        char text[TORIRS_CHROME_TEXT_MAX * 2];
        s->label[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        chrome_gdi_to_crlf(text, (int)sizeof(text), cmd->text);
        control = chrome_gdi_child(
            s, "EDIT",
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, text, id);
        break;
    }

    case TORIRS_CHROME_W_COLORPICK:
        /*
         * A coloured sample and the hex beside it.
         *
         * NOT the model's three axis bars, and not comdlg32's ChooseColor
         * either. The bars are prims this executor cannot draw; ChooseColor is
         * a MODAL dialog, and an executor is forbidden to block -- the client's
         * frame loop runs these, so a window that waits on a dialog stalls the
         * game. What is left is the same trade every other control here makes:
         * the platform's own idiom for the job, which for a value you can also
         * type is a field with a sample in front of it.
         *
         * Typing a hex is therefore how a colour is CHOSEN here, and the model
         * quantises it onto the palette and echoes back the entry it landed on
         * -- so the sample is always a colour the renderer can produce.
         */
        s->label[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        s->swatch[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT | SS_NOTIFY, "", -1);
        control = chrome_gdi_child(s, "EDIT", ES_AUTOHSCROLL, cmd->text, id);
        break;

    case TORIRS_CHROME_W_DROPDOWN:
        /* CBS_OWNERDRAWFIXED so both halves are ours -- the closed button gets
         * the field box with the scrollbar's own down arrow, an open row gets
         * the cache's lighter list tile. CBS_HASSTRINGS keeps CB_ADDSTRING and
         * CB_GETLBTEXT working, which is what an owner-drawn combo otherwise
         * gives up and what every command below still uses. */
        s->label[cmd->widget] = chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        control = chrome_gdi_child(
            s, "COMBOBOX",
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, NULL, id);
        break;

    case TORIRS_CHROME_W_BUTTON:
    case TORIRS_CHROME_W_MENUITEM:
        control = chrome_gdi_child(
            s, "BUTTON", BS_OWNERDRAW, cmd->text[0] ? cmd->text : cmd->label, id);
        break;

    case TORIRS_CHROME_W_SEPARATOR:
        control = chrome_gdi_child(s, "STATIC", SS_OWNERDRAW, NULL, id);
        break;

    case TORIRS_CHROME_W_MODELVIEW:
        control = chrome_gdi_child(
            s, "STATIC", SS_OWNERDRAW,
            cmd->label[0] ? cmd->label : "model preview", id);
        break;

    case TORIRS_CHROME_W_TABSTRIP:
        /* No control of its own: the strip is a row of buttons the OPTION
         * commands below build, and this only records who owns it. */
        s->tab_panel = cmd->panel;
        s->tab_strip_widget = cmd->widget;
        return;

    case TORIRS_CHROME_W_LABEL:
    default:
        control = chrome_gdi_child(
            s, "STATIC", SS_LEFT, cmd->text[0] ? cmd->text : cmd->label, -1);
        break;
    }

    w->native = (intptr_t)control;
}

static void
chrome_gdi_rebuild_tabs(struct ChromeGdi* s)
{
    for( int i = 0; i < 16; i++ )
    {
        if( s->tab_buttons[i] )
            DestroyWindow(s->tab_buttons[i]);
        s->tab_buttons[i] = NULL;
    }
    if( s->tab_count < 2 )
        return;
    for( int i = 0; i < s->tab_count && i < 16; i++ )
        s->tab_buttons[i] = chrome_gdi_child(
            s, "BUTTON", BS_OWNERDRAW, s->tabs[i], CHROME_GDI_ID_TAB_BASE + i);
}

static void
chrome_gdi_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeGdi* s = user;
    struct ToriRSChromeMirrorWidget* w;
    int shape;

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    /*
     * Destroy BEFORE the mirror folds the command in, not after.
     *
     * The mirror clears a slot on REMOVE and clears a panel's whole set on
     * CLOSE -- deliberately, so a recycled handle cannot inherit a stale
     * native id. That means the HWND is unreachable the instant Apply returns,
     * so taking it afterwards leaks the window and leaves a dead control
     * sitting in the tool window with nothing behind it.
     */
    if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE )
    {
        struct ToriRSChromeMirrorWidget* gone =
            ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
        if( gone && gone->native )
            DestroyWindow((HWND)gone->native);
        chrome_gdi_drop_extras(s, cmd->widget);
    }
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE )
    {
        for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        {
            struct ToriRSChromeMirrorWidget* gone =
                ToriRSChromeMirror_Widget(&s->mirror, i);
            if( gone && gone->panel == cmd->panel && gone->native )
                DestroyWindow((HWND)gone->native);
            if( gone && gone->panel == cmd->panel )
                chrome_gdi_drop_extras(s, i);
        }
    }

    shape = ToriRSChromeMirror_Apply(&s->mirror, cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_SYNC_END:
        /* One relayout per frame at most, and only when something moved. The
         * flag is what keeps a frame that only changed a label from
         * repositioning every control in the window. */
        if( s->mirror.intent_overflow )
        {
            fprintf(stderr, "chrome: gdi intent queue overflowed; input was dropped\n");
            s->mirror.intent_overflow = 0;
        }
        return;

    case TORIRS_CHROME_CMD_PANEL_OPEN:
        /* PANEL_OPEN is the one command every presentation gets. A paged
         * plugin window has no TABSTRIP, so deriving the close target from the
         * strip leaves both X buttons addressing panel -1. */
        s->window_panel = cmd->panel;
        s->scroll_y = 0;
        s->scrollbar_dragging = 0;
        if( cmd->text[0] )
            SetWindowTextA(s->hwnd, cmd->text);
        chrome_gdi_layout(s);
        return;

    case TORIRS_CHROME_CMD_PANEL_TITLE:
        SetWindowTextA(s->hwnd, cmd->text);
        InvalidateRect(s->hwnd, NULL, TRUE);
        return;

    case TORIRS_CHROME_CMD_PANEL_CLOSE:
        /* The windows were destroyed above, before the mirror forgot them. */
        if( s->window_panel == cmd->panel )
            s->window_panel = -1;
        s->scroll_y = 0;
        s->scrollbar_dragging = 0;
        chrome_gdi_layout(s);
        return;

    case TORIRS_CHROME_CMD_CHECK_STYLE:
        /* Chrome-wide, so it is handled HERE, in the pass that runs before a
         * widget handle is resolved -- the command names no widget, and the
         * switch below it never sees a command that names none.
         *
         * Every owner-drawn boolean in the window repaints, and the checkbox
         * rows re-lay-out with it: the mark's box is a different width, so the
         * caption beside it starts somewhere else. chrome_gdi_layout batches
         * the moves and invalidates the finished parent once. */
        if( s->check_style != cmd->value )
        {
            s->check_style = cmd->value;
            chrome_gdi_layout(s);
        }
        return;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        chrome_gdi_add(s, cmd);
        chrome_gdi_layout(s);
        return;

    case TORIRS_CHROME_CMD_WIDGET_REMOVE:
        /* Destroyed above, for the same reason. The row it vacated has to be
         * closed up or every control below it stays a row too low. */
        chrome_gdi_layout(s);
        return;

    default:
        break;
    }

    w = ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
    if( !w || !w->native )
    {
        /* A strip's options are its tab titles, and it has no control. */
        if( cmd->widget == s->tab_strip_widget )
        {
            if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTIONS )
            {
                s->tab_count = cmd->value < 16 ? cmd->value : 16;
                memset(s->tabs, 0, sizeof(s->tabs));
            }
            else if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTION && cmd->value < 16 )
            {
                snprintf(
                    s->tabs[cmd->value], sizeof(s->tabs[0]), "%s", cmd->text);
                chrome_gdi_rebuild_tabs(s);
                chrome_gdi_layout(s);
            }
        }
        if( shape )
            chrome_gdi_layout(s);
        return;
    }

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_WIDGET_TEXT:
        /* Never while it has focus: the model is echoing a value the user is
         * still editing, and writing it back would move the caret and undo
         * whatever they typed since the last commit. */
        if( (w->kind != TORIRS_CHROME_W_TEXTINPUT &&
             w->kind != TORIRS_CHROME_W_TEXTAREA &&
             w->kind != TORIRS_CHROME_W_COLORPICK) ||
            GetFocus() != (HWND)w->native )
        {
            if( w->kind == TORIRS_CHROME_W_TEXTAREA )
            {
                char text[TORIRS_CHROME_TEXT_MAX * 2];
                chrome_gdi_to_crlf(text, (int)sizeof(text), cmd->text);
                SetWindowTextA((HWND)w->native, text);
            }
            else
                SetWindowTextA((HWND)w->native, cmd->text);
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_COLOR:
        s->color[cmd->widget] = cmd->color;
        InvalidateRect((HWND)w->native, NULL, TRUE);
        if( s->label[cmd->widget] )
            InvalidateRect(s->label[cmd->widget], NULL, TRUE);
        break;

    case TORIRS_CHROME_CMD_WIDGET_LABEL:
        /* A checkbox carries its own caption; a roster row's name is the
         * STATIC beside it. Both are the label, and only one of them was
         * being updated -- a plugin renamed by a reload kept its old name. */
        if( w->kind == TORIRS_CHROME_W_CHECKBOX )
            SetWindowTextA((HWND)w->native, cmd->label);
        else if( s->label[cmd->widget] )
            SetWindowTextA(s->label[cmd->widget], cmd->label);
        InvalidateRect((HWND)w->native, NULL, TRUE);
        break;

    case TORIRS_CHROME_CMD_WIDGET_CHECKED:
        /*
         * The MODEL is what decides, and this is where it says so.
         *
         * The control has no check state of its own any more (BS_OWNERDRAW),
         * so the bit lives here and the repaint is what shows it. This also
         * covers a toggle the model REFUSED: the click flipped the shadow
         * optimistically, and a WIDGET_CHECKED that disagrees puts it back.
         */
        s->checked[cmd->widget] = cmd->value ? 1 : 0;
        InvalidateRect((HWND)w->native, NULL, TRUE);
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTIONS:
        SendMessageA((HWND)w->native, CB_RESETCONTENT, 0, 0);
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTION:
    case TORIRS_CHROME_CMD_WIDGET_SELECTED:
        if( w->kind == TORIRS_CHROME_W_COLORPICK )
        {
            /* The selection IS the colour (packed HSL16), so the sample's
             * brush is rebuilt around it. Rebuilt rather than recoloured
             * because a brush has no colour to set -- it is the object. */
            HBRUSH replacement = CreateSolidBrush(
                chrome_gdi_colorref(ToriRSChrome_Hsl16ToRgb(cmd->value)));
            if( s->swatch_brush[cmd->widget] )
                DeleteObject(s->swatch_brush[cmd->widget]);
            s->swatch_brush[cmd->widget] = replacement;
            if( s->swatch[cmd->widget] )
                InvalidateRect(s->swatch[cmd->widget], NULL, TRUE);
            break;
        }
        /* TEXTAREA also uses SELECTED, for its first visible model line. A
         * native multiline EDIT scrolls itself; sending a COMBOBOX message to
         * it is not an ignore -- that numeric message means something else to
         * the EDIT class. */
        if( w->kind == TORIRS_CHROME_W_DROPDOWN )
        {
            if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTION )
                SendMessageA((HWND)w->native, CB_ADDSTRING, 0, (LPARAM)cmd->text);
            else
                SendMessageA((HWND)w->native, CB_SETCURSEL, (WPARAM)cmd->value, 0);
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_FOCUS:
        /* An EDIT owns its own focus, and this window's is set by the user
         * clicking in it -- the model's copy is downstream of that. Acted on
         * only in the direction that cannot loop: taking the focus when the
         * model says a row has it and Windows disagrees. */
        if( cmd->value && GetFocus() != (HWND)w->native )
            SetFocus((HWND)w->native);
        break;

    default:
        break;
    }

    if( shape )
        chrome_gdi_layout(s);
}

static int
chrome_gdi_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeGdi* s = user;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;
    /* The messages themselves are pumped by the game's own PeekMessage loop in
     * platform_win32gdi.c -- one queue per thread, and both windows are on
     * this one. Nothing extra runs here; the wndproc has already filled the
     * mirror's queue by the time anything asks. */
    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

struct ToriRSChromeExec
ToriRSChromeExec_Gdi(void* platform)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    memset(&g_chrome_gdi, 0, sizeof(g_chrome_gdi));
    /* The shell hands every executor the same platform handle; this one asks
     * it for the game's HWND, which is all an owned tool window needs. Asking
     * here rather than making the shell branch per executor is what keeps the
     * chooser's call site identical on every lane. */
    g_chrome_gdi.owner = platform ? (HWND)PlatformSDL2_NativeWindowHandle(platform) : NULL;

    exec.user = &g_chrome_gdi;
    exec.begin = chrome_gdi_begin;
    exec.apply = chrome_gdi_apply;
    exec.end = chrome_gdi_end;
    exec.poll = chrome_gdi_poll;
    return exec;
}
