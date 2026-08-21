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
#define CHROME_GDI_BOX CHROME_GDI_PX(TORIRS_CHROME_M_BOX)
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
#define CHROME_GDI_FIELD_PAD_X CHROME_GDI_PX(TORIRS_CHROME_M_FIELD_PAD_X)
#define CHROME_GDI_FIELD_INSET CHROME_GDI_PX(TORIRS_CHROME_M_FIELD_INSET)
#define CHROME_GDI_RULE CHROME_GDI_PX(1)

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

struct ChromeGdi
{
    HWND owner;
    HWND hwnd;
    HFONT font;
    int open;
    struct ToriRSChromeMirror mirror;
    /** The panel whose tabs the strip shows, and its titles. One window, so
     *  one strip; a list would be a list with one entry. */
    int tab_panel;
    int tab_strip_widget;
    char tabs[16][64];
    int tab_count;
    HWND tab_buttons[16];
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
 * dbg_push_frame, in GDI. The corners blit at their baked size and the edges
 * stretch along their runs; the baked centre is never drawn, because the
 * window's own tile is already under it.
 */
static void
chrome_gdi_frame(struct ChromeGdi* s, HDC dc, int w, int h)
{
    int const t = CHROME_GDI_FRAME;
    int const mid_w = w - 2 * t;
    int const mid_h = h - 2 * t;
    int const right = w - t;
    int const bottom = h - t;

    if( !ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) )
    {
        /* No frame baked: the black edge every interface in this game has. */
        chrome_gdi_outline(dc, 0, 0, w, h, TORIRS_CHROME_C_CHROME);
        return;
    }
    chrome_gdi_blit(s, dc, 0, 0, t, t, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT);
    chrome_gdi_blit(s, dc, right, 0, t, t, TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT);
    chrome_gdi_blit(s, dc, 0, bottom, t, t, TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT);
    chrome_gdi_blit(s, dc, right, bottom, t, t, TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT);
    if( mid_w > 0 )
    {
        chrome_gdi_blit(s, dc, t, 0, mid_w, t, TORIRS_CHROME_SKIN_FRAME_TOP);
        chrome_gdi_blit(s, dc, t, bottom, mid_w, t, TORIRS_CHROME_SKIN_FRAME_BOTTOM);
    }
    if( mid_h > 0 )
    {
        chrome_gdi_blit(s, dc, 0, t, t, mid_h, TORIRS_CHROME_SKIN_FRAME_LEFT);
        chrome_gdi_blit(s, dc, right, t, t, mid_h, TORIRS_CHROME_SKIN_FRAME_RIGHT);
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

/* ---- layout --------------------------------------------------------------- */

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
    int live = 0;

    if( !s->hwnd )
        return;
    GetClientRect(s->hwnd, &client);
    width = client.right - client.left;

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
                CHROME_GDI_FRAME + CHROME_GDI_PAD,
                CHROME_GDI_TAB_W,
                CHROME_GDI_TAB_H,
                SWP_NOZORDER | SWP_SHOWWINDOW);

    y = CHROME_GDI_PAD + (s->tab_count > 1 ? CHROME_GDI_TAB_H + CHROME_GDI_ROW_GAP : 0);
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
        int const row_h = CHROME_GDI_ROW_H;
        int x = CHROME_GDI_FRAME + CHROME_GDI_PAD;
        int row_w = width - 2 * CHROME_GDI_PAD;

        if( !w || !w->native )
            continue;
        control = (HWND)w->native;

        if( !ToriRSChromeMirror_Shown(&s->mirror, i) )
        {
            /* Hidden, not destroyed: the control keeps its text and its
             * selection, so switching back to a tab restores what was on it
             * rather than a rebuilt blank. The row's OTHER controls have to go
             * with it -- a roster row whose switch hid and whose name did not
             * is a caption floating over the row below. */
            dwp = DeferWindowPos(dwp, control, NULL, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            if( s->label[i] )
                dwp = DeferWindowPos(dwp, s->label[i], NULL, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            if( s->action[i] )
                dwp = DeferWindowPos(dwp, s->action[i], NULL, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            if( s->swatch[i] )
                dwp = DeferWindowPos(dwp, s->swatch[i], NULL, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            continue;
        }

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
    RedrawWindow(s->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
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
    int const slot = on ? TORIRS_CHROME_SKIN_CHECK_ON : TORIRS_CHROME_SKIN_CHECK_OFF;
    int const h = box.bottom - box.top;
    int const side = h < CHROME_GDI_BOX ? h : CHROME_GDI_BOX;
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
        if( s->label[i] == control )
            return TORIRS_CHROME_C_LABEL;
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
            mark.right = mark.left + CHROME_GDI_BOX;
            chrome_gdi_draw_mark(s, di->hDC, mark, s->checked[handle]);
            box.left += CHROME_GDI_BOX + CHROME_GDI_CHECK_GAP;
            chrome_gdi_text(di->hDC, box, caption, TORIRS_CHROME_C_TEXT, DT_LEFT);
            break;
        }

        case TORIRS_CHROME_W_LISTROW:
            /* Only the SWITCH: a roster row's name is a STATIC of its own and
             * its settings well is the parallel control, so this box is just
             * the toggle's hit area. */
            chrome_gdi_draw_mark(s, di->hDC, box, s->checked[handle]);
            break;

        case TORIRS_CHROME_W_BUTTON:
        case TORIRS_CHROME_W_MENUITEM:
            chrome_gdi_draw_button(
                s, di->hDC, box, caption, pressed, TORIRS_CHROME_C_TEXT);
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
            case TORIRS_CHROME_W_COLORPICK:
                /* EN_KILLFOCUS, not EN_CHANGE: an intent per keystroke would
                 * send the model a value for every half-typed state, and the
                 * chrome's own input commits the same way. A colour row's EDIT
                 * commits by the same route -- the model is what turns the hex
                 * into a palette entry, so there is nothing extra to do here. */
                if( notify == EN_KILLFOCUS )
                {
                    char buf[TORIRS_CHROME_TEXT_MAX];
                    GetWindowTextA(control, buf, (int)sizeof(buf));
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
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;

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
        chrome_gdi_frame(s, dc, client.right, client.bottom);

        ordered = ToriRSChromeMirror_Order(&s->mirror, order, TORIRS_CHROME_MAX_WIDGETS);
        for( int oi = 0; oi < ordered; oi++ )
        {
            int const i = order[oi];
            struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
            RECT box;

            if( !w || !w->native || !ToriRSChromeMirror_Shown(&s->mirror, i) )
                continue;
            if( w->kind != TORIRS_CHROME_W_TEXTINPUT && w->kind != TORIRS_CHROME_W_COLORPICK )
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
        intent.panel = s->tab_panel;
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
        WS_EX_TOOLWINDOW,
        CHROME_GDI_WNDCLASS,
        "Plugins",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
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
        s->font = CreateFontIndirectA(&ncm.lfMessageFont);

    chrome_gdi_make_tile_brush(s);

    ToriRSChromeMirror_Init(&s->mirror);
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
    s->font = NULL;
    s->tile_brush = NULL;
    s->tile_bitmap = NULL;
    s->scratch_bitmap = NULL;
    s->scratch_dc = NULL;
    s->scratch_pixels = NULL;
    s->scratch_w = 0;
    s->scratch_h = 0;
    s->open = 0;
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
    /* The ADD is the one command carrying a widget's SHAPE, and `w` is a
     * LISTROW's settings affordance -- the same field the CS2 executor reads
     * it out of. A row that gained or lost one is re-added, not updated. */
    s->row_action[cmd->widget] = cmd->w ? 1 : 0;

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
        control = chrome_gdi_child(s, "STATIC", SS_ETCHEDHORZ, NULL, -1);
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

    case TORIRS_CHROME_CMD_PANEL_TITLE:
        SetWindowTextA(s->hwnd, cmd->text);
        return;

    case TORIRS_CHROME_CMD_PANEL_CLOSE:
        /* The windows were destroyed above, before the mirror forgot them. */
        chrome_gdi_layout(s);
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
             w->kind != TORIRS_CHROME_W_COLORPICK) ||
            GetFocus() != (HWND)w->native )
            SetWindowTextA((HWND)w->native, cmd->text);
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
        if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTION )
            SendMessageA((HWND)w->native, CB_ADDSTRING, 0, (LPARAM)cmd->text);
        else
            SendMessageA((HWND)w->native, CB_SETCURSEL, (WPARAM)cmd->value, 0);
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
