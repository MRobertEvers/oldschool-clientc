/*
 * platform_win32gdi.c -- Win32 + GDI backend for the src/main.c App front-end.
 *
 * src/main.c programs to the PlatformWindow interface (platform_window.h) and, when
 * built without a GPU renderer, uses the software path:
 *
 *     App_Render(app, PlatformWindow_Pixels(sdl), W, H);   // CPU raster into pixels
 *     PlatformWindow_Present(sdl);                          // show the pixels
 *
 * That is exactly what GDI does well: hand out a top-down 32bpp DIB section as
 * the pixel buffer, then BitBlt/StretchBlt it to the window. This file is a
 * drop-in implementation of the SAME PlatformWindow_* symbols backed by Win32 GDI
 * instead of SDL. It also owns the HWND used by both Windows lanes' fixed-
 * function D3D9 renderer; --soft3d keeps using the DIB presentation path below.
 *
 * The pixel format matches SDL's ARGB8888 (what App_Render writes): on a
 * little-endian box a top-down BI_RGB 32bpp DIB is byte-order BGRA / word-order
 * ARGB, identical to SDL_PIXELFORMAT_ARGB8888. So the buffer is presented
 * verbatim, no per-pixel conversion.
 *
 * Input is translated from Win32 messages into the same CmdBus_Push* commands
 * the SDL backend emits (the retained-mode event system), so the client sees an
 * identical input stream regardless of platform.
 *
 * Built in place of src/platform/platform_sdl2.c: the win32 and win64 lanes in
 * src/platform/platform.mk point PLATFORM_WINDOW_SRC here. Use build_winxp.ps1
 * for the i686 XP artifact or build_windows.ps1 for modern x86_64 Windows.
 */

#include "platform/platform_window.h"
#include "platform/platform_app_icon.h"
#include "platform/platform_win32_chrome.h"
#include "platform/platform_win32_browser_backend.h"
#include "platform/platform_win32_timing.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "input/torirs_touch.h"
#include "perf/torirs_perf.h"
#include "ui/torirs_chrome_metrics.h"
#include <windows.h>
#include <objbase.h>

/*
 * WM_TOUCH, without giving up the XP lane.
 *
 * Touch arrived in Windows 7. This backend still has to LOAD on XP, so none of
 * it can be linked against: RegisterTouchWindow and friends live in user32 on
 * a machine that has them and nowhere at all on one that does not, and an
 * import for a missing symbol fails the whole process at load time rather than
 * at the call. So they are looked up by name once, and a machine without them
 * simply never registers the window and never sees the message.
 *
 * The declarations are local for the same reason the lookup is: the XP-era
 * headers this lane compiles against have no winuser.h touch section, so
 * including one is not an option and the shapes are restated from the API
 * documentation instead.
 */
#ifndef WM_TOUCH
#define WM_TOUCH 0x0240
#endif
#ifndef TOUCHEVENTF_MOVE
#define TOUCHEVENTF_MOVE 0x0001
#define TOUCHEVENTF_DOWN 0x0002
#define TOUCHEVENTF_UP 0x0004
#endif
#ifndef TWF_WANTPALM
#define TWF_WANTPALM 0x00000002
#endif

typedef struct
{
    LONG x; /* in hundredths of a pixel, screen space */
    LONG y;
    HANDLE source;
    DWORD id;
    DWORD flags;
    DWORD mask;
    DWORD time;
    ULONG_PTR extra;
    DWORD contact_w;
    DWORD contact_h;
} TORIRS_TOUCHINPUT;

typedef BOOL(WINAPI* TORIRS_RegisterTouchWindow)(HWND, ULONG);
typedef BOOL(WINAPI* TORIRS_GetTouchInputInfo)(HANDLE, UINT, TORIRS_TOUCHINPUT*, int);
typedef BOOL(WINAPI* TORIRS_CloseTouchInputHandle)(HANDLE);

static TORIRS_RegisterTouchWindow g_register_touch_window;
static TORIRS_GetTouchInputInfo g_get_touch_input_info;
static TORIRS_CloseTouchInputHandle g_close_touch_input_handle;

/** @return true when this machine has the touch API at all. */
static int
win32_touch_load(void)
{
    static int tried;
    HMODULE user32;

    if( tried )
        return g_register_touch_window != NULL;
    tried = 1;
    user32 = GetModuleHandleA("user32.dll");
    if( !user32 )
        return 0;
    g_register_touch_window =
        (TORIRS_RegisterTouchWindow)(void*)GetProcAddress(user32, "RegisterTouchWindow");
    g_get_touch_input_info =
        (TORIRS_GetTouchInputInfo)(void*)GetProcAddress(user32, "GetTouchInputInfo");
    g_close_touch_input_handle =
        (TORIRS_CloseTouchInputHandle)(void*)GetProcAddress(user32, "CloseTouchInputHandle");
    if( !g_get_touch_input_info || !g_close_touch_input_handle )
        g_register_touch_window = NULL;
    return g_register_touch_window != NULL;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef GET_X_LPARAM
#  define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#  define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif
#ifndef WM_PRINTCLIENT
#  define WM_PRINTCLIENT 0x0318
#endif

static const char RPD_WNDCLASS[] = "TorirsWin32GdiWindow";
static const char RPD_CHROME_RAIL_WNDCLASS[] = "TorirsWin32ChromeRail";

/* One logical, persistent modern-OSRS strip. Win32 uses device pixels here.
 * Both bundles lay the rail out at the shared authored width, so the window
 * reserves exactly that: the two spellings this carried before (48 on WebView2,
 * 46 on the ES3 page) each left a band of window background beside a page that
 * had drawn its own edge. */
#define WIN32_CHROME_RAIL_W TORIRS_CHROME_M_RAIL_W

/* Rail icons plus one selected page's custom regions fit comfortably here.
 * The ring also bounds historical page/serial keys over a long session. */
#define WIN32_BROWSER_BITMAP_CACHE_MAX 128
#define WIN32_BROWSER_BITMAP_PIXELS_MAX (4096u * 1024u)

struct Win32BrowserBitmapEntry
{
    char key[96];
    WCHAR path[MAX_PATH];
    uint32_t revision;
};

struct PlatformWindow
{
    HWND    hwnd;
    /* The window's own DC, fetched once. The class is CS_OWNDC, so this handle
     * is permanently associated with the window and survives resizes — but
     * GetDC/ReleaseDC around every present is still a pair of calls into
     * user32, and on a composited desktop they measured 24.6 us/frame, 15% of
     * the whole present stage against a blit that is 84% of it. */
    HDC     window_dc;
    HDC     mem_dc;          /* memory DC holding the DIB                     */
    HBITMAP dib;             /* top-down 32bpp DIB section                    */
    HBITMAP old_bmp;         /* default bitmap displaced from mem_dc          */
    int*    pixels;          /* DIB bits; App_Render writes here (ARGB8888)   */
    int     width;
    int     height;
    int     gdi_frame_valid; /* the DIB contains one complete Soft3D frame    */
    /* One-shot damage box for the next present; w == 0 means the whole DIB.
     * @see PlatformWindow_SetPresentDamage. */
    int     present_dmg_x;
    int     present_dmg_y;
    int     present_dmg_w;
    int     present_dmg_h;
    /* Optional finer breakdown of that box. BitBlt bills per pixel far more
     * than per call, so two blits covering 193,901 px beat one covering the
     * 240,195 px their bounding box spans. */
    int     present_dmg_rects[PLATFORM_PRESENT_DAMAGE_RECT_MAX][4];
    int     present_dmg_rect_count;
    int     timing_active;

    int     quit;
    int     esc_quits;       /* TORIRS_ESC_QUIT: ESC closes the window        */
    int     canvas_follows_window;
    int     resizable_w;
    int     resizable_h;
    int     interface_scale_mode;

    /*
     * The ONE plugin-chrome shell inside hwnd.
     *
     * rail_hwnd is one persistent browser-container child. Its local DOM owns
     * both the rail and the sole selected page; collapse only changes width.
     */
    HWND    chrome_rail_hwnd;
    struct PlatformWin32Browser* chrome_browser;
    WCHAR   chrome_browser_asset_dir[MAX_PATH];
    WCHAR   chrome_browser_asset_url[MAX_PATH * 3];
    struct Win32BrowserBitmapEntry
        chrome_browser_bitmaps[WIN32_BROWSER_BITMAP_CACHE_MAX];
    int     chrome_browser_bitmap_count;
    int     chrome_browser_bitmap_evict;
    int     chrome_width;
    int     chrome_height;
    int     chrome_requested_w;
    int     chrome_layout_w;
    int     chrome_collapsed_client_w;
    int     chrome_open;

    /* Set only for the duration of PollCommands so the WndProc can translate
     * into the caller's bus; the coalesced resize is applied after the pump. */
    struct ToriRS_CmdBus* poll_bus;
    int     pending_resize_w;
    int     pending_resize_h;
    int     pending_repeat;
    /* Fingers. @see ToriRS_Touch, which holds the gesture policy this backend
     * shares with the SDL one. */
    struct ToriRS_Touch touch;
#if defined(TORIRS_WIN32_GDI_TEST_API)
    uint32_t paint_count;
#endif
};

static LRESULT CALLBACK
chrome_rail_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static int
win32_main_client_size(struct PlatformWindow const* p, int* out_w, int* out_h)
{
    RECT client;

    if( !p || !p->hwnd || !GetClientRect(p->hwnd, &client) )
        return 0;
    if( out_w )
        *out_w = (int)(client.right - client.left);
    if( out_h )
        *out_h = (int)(client.bottom - client.top);
    return 1;
}

static void
win32_resize_client(struct PlatformWindow* p, int width, int height)
{
    RECT outer;
    DWORD style;

    if( !p || !p->hwnd || width <= 0 || height <= 0 )
        return;
    outer.left = 0;
    outer.top = 0;
    outer.right = width;
    outer.bottom = height;
    style = (DWORD)GetWindowLongPtr(p->hwnd, GWL_STYLE);
    AdjustWindowRect(&outer, style, FALSE);
    SetWindowPos(
        p->hwnd,
        NULL,
        0,
        0,
        outer.right - outer.left,
        outer.bottom - outer.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static int
win32_chrome_reserved_width(struct PlatformWindow const* p)
{
    int reserved = 0;

    if( !p )
        return 0;
    if( p->chrome_rail_hwnd )
        reserved += WIN32_CHROME_RAIL_W;
    if( p->chrome_open )
        reserved += p->chrome_layout_w;
    return reserved;
}

static int
win32_game_client_size(
    struct PlatformWindow const* p, int* out_w, int* out_h)
{
    int width;
    int height;

    if( !win32_main_client_size(p, &width, &height) )
        return 0;
    width -= win32_chrome_reserved_width(p);
    if( width < 0 )
        width = 0;
    if( out_w )
        *out_w = width;
    if( out_h )
        *out_h = height;
    return 1;
}

static void
win32_chrome_layout(struct PlatformWindow* p, int client_w, int client_h)
{
    int rail_w;
    int pane_w;
    int shell_w;
    int game_w;

    if( !p || client_w < 0 || client_h < 0 )
        return;
    rail_w = p->chrome_rail_hwnd ? WIN32_CHROME_RAIL_W : 0;
    pane_w = p->chrome_open ? p->chrome_requested_w : 0;
    if( pane_w > client_w - rail_w )
        pane_w = client_w - rail_w;
    if( pane_w < 0 )
        pane_w = 0;
    shell_w = rail_w + pane_w;
    game_w = client_w - shell_w;
    if( game_w < 0 )
        game_w = 0;
    p->chrome_layout_w = pane_w;
    SetWindowLongPtr(
        p->hwnd,
        TORIRS_WIN32_CHROME_EXTRA_RESERVED_OFFSET,
        (LONG_PTR)shell_w);

    /* Anatomy is game | one browser. The DOM inside that browser owns both
     * its persistent rail and its optional selected page. */
    if( p->chrome_rail_hwnd )
        SetWindowPos(
            p->chrome_rail_hwnd,
            NULL,
            game_w,
            0,
            shell_w,
            client_h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if( p->chrome_browser )
        PlatformWin32Browser_Resize(p->chrome_browser, shell_w, client_h);

}


/* This HWND is only the clip/layout parent for the embedded browser. It must
 * never become a second native presenter when WebView2/MSHTML is unavailable:
 * the browser bundle owns every visible rail/page pixel and every interaction. */
static LRESULT CALLBACK
chrome_rail_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct PlatformWindow* p =
        (struct PlatformWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch( msg )
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        RECT client;
        HDC const dc = BeginPaint(hwnd, &paint);
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        if( wparam )
        {
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(
                (HDC)wparam, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        return 0;
    case WM_SIZE:
        if( p && p->chrome_browser )
            PlatformWin32Browser_Resize(
                p->chrome_browser, LOWORD(lparam), HIWORD(lparam));
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static int
win32_chrome_ensure_rail(struct PlatformWindow* p)
{
    int width;
    int height;
    LONG_PTR style;

    if( !p || !p->hwnd )
        return 0;
    if( p->chrome_rail_hwnd )
        return 1;
    if( !win32_main_client_size(p, &width, &height) )
        return 0;
    p->chrome_rail_hwnd = CreateWindowExA(
        0,
        RPD_CHROME_RAIL_WNDCLASS,
        "Plugins",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        width,
        0,
        WIN32_CHROME_RAIL_W,
        height,
        p->hwnd,
        NULL,
        GetModuleHandleA(NULL),
        NULL);
    if( !p->chrome_rail_hwnd )
        return 0;
    SetWindowLongPtr(
        p->chrome_rail_hwnd, GWLP_USERDATA, (LONG_PTR)p);

    /* Keep-game-size, RuneLite-style, where USER32 permits it.  Maximized and
     * popup/fullscreen windows are fit in place and never forced off-screen. */
    style = GetWindowLongPtr(p->hwnd, GWL_STYLE);
    if( !(style & WS_POPUP) && !(style & WS_MAXIMIZE) )
        win32_resize_client(p, width + WIN32_CHROME_RAIL_W, height);
    win32_main_client_size(p, &width, &height);
    win32_chrome_layout(p, width, height);
    ShowWindow(p->chrome_rail_hwnd, SW_SHOWNOACTIVATE);
    return 1;
}

/* ---- pixel buffer ------------------------------------------------------- */

static int
gdi_make_dib(struct PlatformWindow* p, int width, int height)
{
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP dib;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; /* top-down: row 0 is the top scanline */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(p->mem_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if( !dib || !bits )
    {
        if( dib )
            DeleteObject(dib);
        return 0;
    }

    /* Swap the new DIB in, retire the old one. */
    {
        HGDIOBJ prev = SelectObject(p->mem_dc, dib);
        if( !prev || prev == HGDI_ERROR || (p->dib && prev != (HGDIOBJ)p->dib) )
        {
            /* Leave the platform's published buffer unchanged if the memory
             * DC rejected the replacement or contained an unexpected object. */
            if( prev && prev != HGDI_ERROR )
                SelectObject(p->mem_dc, prev);
            DeleteObject(dib);
            return 0;
        }
        if( !p->dib )
            p->old_bmp = (HBITMAP)prev; /* remember the DC's default bitmap */
        else
            DeleteObject(p->dib); /* resize: the old DIB is now unselected */
    }
    p->dib = dib;
    p->pixels = (int*)bits;
    p->width = width;
    p->height = height;
    p->gdi_frame_valid = 0;
    memset(p->pixels, 0, (size_t)width * (size_t)height * sizeof(int));
    return 1;
}

/* ---- letterbox math (mirrors the SDL backend) --------------------------- */

static void
letterbox_dst(int logical_w, int logical_h, int win_w, int win_h, RECT* dst)
{
    float src_aspect;
    float win_aspect;

    dst->left = 0;
    dst->top = 0;
    dst->right = logical_w;
    dst->bottom = logical_h;
    if( win_w <= 0 || win_h <= 0 )
        return;

    src_aspect = (float)logical_w / (float)logical_h;
    win_aspect = (float)win_w / (float)win_h;
    if( src_aspect > win_aspect )
    {
        int w = win_w;
        int h = (int)(win_w / src_aspect);
        dst->left = 0;
        dst->top = (win_h - h) / 2;
        dst->right = w;
        dst->bottom = h;
    }
    else
    {
        int h = win_h;
        int w = (int)(win_h * src_aspect);
        dst->left = (win_w - w) / 2;
        dst->top = 0;
        dst->right = w;
        dst->bottom = h;
    }
}

static void
map_mouse(struct PlatformWindow* p, int win_x, int win_y, int* out_x, int* out_y)
{
    RECT client;
    RECT box;
    int win_w;
    int win_h;
    int x;
    int y;

    if( !p->hwnd )
    {
        *out_x = win_x;
        *out_y = win_y;
        return;
    }
    GetClientRect(p->hwnd, &client);
    if( !win32_game_client_size(p, &win_w, &win_h) )
    {
        win_w = client.right - client.left;
        win_h = client.bottom - client.top;
    }
    letterbox_dst(p->width, p->height, win_w, win_h, &box);
    if( box.right <= 0 || box.bottom <= 0 )
    {
        *out_x = 0;
        *out_y = 0;
        return;
    }
    x = (win_x - box.left) * p->width / box.right;
    y = (win_y - box.top) * p->height / box.bottom;
    if( x < 0 )
        x = 0;
    else if( x >= p->width )
        x = p->width - 1;
    if( y < 0 )
        y = 0;
    else if( y >= p->height )
        y = p->height - 1;
    *out_x = x;
    *out_y = y;
}

/* Paint the last complete software frame. RECT.right/bottom in `box` are the
 * destination width/height (letterbox_dst predates this helper), not absolute
 * coordinates. Keeping every visible operation here makes normal presents,
 * repair paints, and PrintWindow captures agree. */
static void
gdi_fill_black(HDC dc, int left, int top, int right, int bottom)
{
    RECT rect;
    if( right <= left || bottom <= top )
        return;
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
}

/* ABLATION SUPPORT (measurement only) -- see the TORIRS_ABL_PRESENT_VP arm in
 * gdi_paint_latest. Read once; off is one predicted branch. */
static int
gdi_abl_present_vp(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_PRESENT_VP") ? 1 : 0;
    return armed;
}

static void
gdi_paint_latest(struct PlatformWindow* p, HDC dc)
{
    RECT client;
    RECT box;
    int win_w;
    int win_h;
    int image_right;
    int image_bottom;

    assert(p);
    if( !p->gdi_frame_valid || !p->mem_dc || !p->pixels )
        return;
    assert(dc);

    GetClientRect(p->hwnd, &client);
    if( !win32_game_client_size(p, &win_w, &win_h) )
    {
        win_w = client.right - client.left;
        win_h = client.bottom - client.top;
    }
    if( win_w <= 0 || win_h <= 0 )
        return;

    letterbox_dst(p->width, p->height, win_w, win_h, &box);
    image_right = box.left + box.right;
    image_bottom = box.top + box.bottom;

    /*
     * Consume the one-shot damage box, whoever is painting. Taking it here
     * rather than in Present is what makes a WM_PAINT repair safe: a repair
     * arrives with no damage set and therefore copies everything, and a repair
     * that lands between a SetPresentDamage and its Present takes the box,
     * paints that much, and leaves the following Present to do the rest.
     */
    {
        int dmg_x = p->present_dmg_x;
        int dmg_y = p->present_dmg_y;
        int dmg_w = p->present_dmg_w;
        int dmg_h = p->present_dmg_h;

        p->present_dmg_w = 0;
        p->present_dmg_h = 0;

        /* Only the unscaled path can honour it: under StretchBlt a destination
         * pixel samples a source area the box does not bound. */
        if( dmg_w > 0 && dmg_h > 0 && box.right == p->width &&
            box.bottom == p->height )
        {
            int nrects = p->present_dmg_rect_count;

            p->present_dmg_rect_count = 0;
            if( nrects > 0 )
            {
                for( int i = 0; i < nrects; i++ )
                    BitBlt(
                        dc,
                        box.left + p->present_dmg_rects[i][0],
                        box.top + p->present_dmg_rects[i][1],
                        p->present_dmg_rects[i][2],
                        p->present_dmg_rects[i][3],
                        p->mem_dc,
                        p->present_dmg_rects[i][0],
                        p->present_dmg_rects[i][1],
                        SRCCOPY);
            }
            else
            {
                BitBlt(
                    dc,
                    box.left + dmg_x,
                    box.top + dmg_y,
                    dmg_w,
                    dmg_h,
                    p->mem_dc,
                    dmg_x,
                    dmg_y,
                    SRCCOPY);
            }
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PRESENT_BLIT_1TO1, 1);
            /* The letterbox bars are unchanged for the same reason the chrome
             * is: nothing drew there. */
            return;
        }
    }

    /* Never clear the image rectangle. Clearing the full client and then
     * blitting exposed an observable black frame between the two GDI calls. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT_FILL)
    {
        gdi_fill_black(dc, 0, 0, win_w, box.top);
        gdi_fill_black(dc, 0, image_bottom, win_w, win_h);
        gdi_fill_black(dc, 0, box.top, box.left, image_bottom);
        gdi_fill_black(dc, image_right, box.top, win_w, image_bottom);
        TORIRS_PERF_COUNT(
            TORIRS_PERF_CTR_PRESENT_FILL_PIXELS,
            (int64_t)win_w * win_h - (int64_t)box.right * box.bottom);
    }

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT_BLIT)
    {
        if( box.right == p->width && box.bottom == p->height )
        {
            int blit_w = p->width;
            int blit_h = p->height;

            /* ABLATION (TORIRS_ABL_PRESENT_VP=1, measurement only): present
             * only a world-viewport-sized 512x334 region instead of the whole
             * 765x503 DIB.
             *
             * This bounds what damaged-rect presentation could recover from the
             * per-frame GDI BitBlt: in an idle in-world steady state the world
             * viewport is the region that genuinely changes, so a damage system
             * cannot blit less than this. The origin is deliberately (0,0) --
             * the kernel cost tracks the area copied, not where it lands, and
             * using the true viewport origin would only make the wrong image
             * look more plausible. */
            if( gdi_abl_present_vp() )
            {
                if( blit_w > 512 )
                    blit_w = 512;
                if( blit_h > 334 )
                    blit_h = 334;
            }
            BitBlt(dc, box.left, box.top, blit_w, blit_h, p->mem_dc, 0, 0, SRCCOPY);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PRESENT_BLIT_1TO1, 1);
        }
        else
        {
            if( p->interface_scale_mode == 0 )
                SetStretchBltMode(dc, COLORONCOLOR);
            else
            {
                /* GDI exposes one colour interpolation filter. HALFTONE is its
                 * highest-quality resampler and is the closest native match for
                 * both the Linear and Bicubic settings. */
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, box.left, box.top, NULL);
            }
            StretchBlt(
                dc, box.left, box.top, box.right, box.bottom,
                p->mem_dc, 0, 0, p->width, p->height, SRCCOPY);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PRESENT_BLIT_STRETCH, 1);
        }
        TORIRS_PERF_COUNT(
            TORIRS_PERF_CTR_PRESENT_BLIT_PIXELS, (int64_t)box.right * box.bottom);
    }
}

/* ---- Win32 VK -> torirs key code (mirrors sdl_keycode_to_torirs) -------- */

static enum LibToriRS_KeyCode
vk_to_torirs(int vk)
{
    if( vk >= 'A' && vk <= 'Z' )
        return (enum LibToriRS_KeyCode)(TORIRSK_A + (vk - 'A'));
    if( vk >= '0' && vk <= '9' )
        return (enum LibToriRS_KeyCode)(TORIRSK_0 + (vk - '0'));
    switch( vk )
    {
    case VK_ESCAPE:
        return TORIRSK_ESCAPE;
    case VK_RETURN:
        return TORIRSK_RETURN;
    case VK_BACK:
        return TORIRSK_BACKSPACE;
    case VK_INSERT:
        return TORIRSK_INSERT;
    case VK_DELETE:
        return TORIRSK_DELETE;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return TORIRSK_SHIFT;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return TORIRSK_CTRL;
    case VK_TAB:
        return TORIRSK_TAB;
    case VK_SPACE:
        return TORIRSK_SPACE;
    case VK_LEFT:
        return TORIRSK_LEFT;
    case VK_RIGHT:
        return TORIRSK_RIGHT;
    case VK_UP:
        return TORIRSK_UP;
    case VK_DOWN:
        return TORIRSK_DOWN;
    case VK_PRIOR:
        return TORIRSK_PAGE_UP;
    case VK_NEXT:
        return TORIRSK_PAGE_DOWN;
    case VK_OEM_COMMA:
        return TORIRSK_COMMA;
    default:
        return TORIRSK_UNKNOWN;
    }
}

static uint8_t
win_button_to_torirs(UINT msg)
{
    switch( msg )
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return (uint8_t)TORIRSM_LEFT;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return (uint8_t)TORIRSM_RIGHT;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return (uint8_t)TORIRSM_MIDDLE;
    default:
        return (uint8_t)TORIRSM_UNKNOWN;
    }
}

static int
ctrl_or_alt_down(void)
{
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000);
}

/* ---- window procedure: translate messages into the poll bus ------------- */

static LRESULT CALLBACK
wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct PlatformWindow* p =
        (struct PlatformWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    struct ToriRS_CmdBus* bus = p ? p->poll_bus : NULL;
    int lx;
    int ly;

    switch( msg )
    {
    case WM_ERASEBKGND:
        /* The complete DIB below owns every client pixel. Default erasure used
         * to paint it black before the next out-of-band GetDC blit. */
        return 1;

    case WM_PAINT:
        if( p )
        {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            gdi_paint_latest(p, dc);
            EndPaint(hwnd, &paint);
#if defined(TORIRS_WIN32_GDI_TEST_API)
            p->paint_count++;
#endif
            return 0;
        }
        break;

    case WM_PRINTCLIENT:
        /* PrintWindow and the headless regression test need the same complete
         * client image that WM_PAINT repairs after uncover/invalidation. */
        if( p && wparam )
            gdi_paint_latest(p, (HDC)wparam);
        return 0;

    case WM_CLOSE:
    case WM_DESTROY:
        if( p )
            p->quit = 1;
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        int vk = (int)wparam;
        int repeat = (lparam & 0x40000000) ? 1 : 0; /* prev key-state bit */
        enum LibToriRS_KeyCode key = vk_to_torirs(vk);
        int osrs;
        if( !p )
            break;
        if( p->esc_quits && key == TORIRSK_ESCAPE )
            p->quit = 1;
        if( bus && !repeat )
            CmdBus_PushKey(bus, TORIRS_CMD_INPUT_KEY_DOWN, (uint8_t)key);
        osrs = LibToriRS_OsrsKeyFromVk(vk);
        if( bus && osrs >= 0 )
        {
            CmdBus_PushOsrsKey(bus, osrs, 1, (uint8_t)!repeat);
            CmdBus_PushKeyEvent(bus, osrs, 0, (uint8_t)repeat);
            p->pending_repeat = repeat;
        }
        break;
    }

    case WM_CHAR:
    {
        int cp = (int)wparam; /* UTF-16 code unit */
        if( !bus || ctrl_or_alt_down() )
            break;
        if( cp >= 32 && cp <= 255 )
            CmdBus_PushKeyEvent(bus, -1, cp, (uint8_t)(p ? p->pending_repeat : 0));
        break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int vk = (int)wparam;
        int osrs = LibToriRS_OsrsKeyFromVk(vk);
        if( bus )
        {
            CmdBus_PushKey(bus, TORIRS_CMD_INPUT_KEY_UP, (uint8_t)vk_to_torirs(vk));
            if( osrs >= 0 )
                CmdBus_PushOsrsKey(bus, osrs, 0, 0);
        }
        break;
    }

    case WM_KILLFOCUS:
        /* The OS stops delivering key-ups without focus, so anything held now
         * would latch forever -- clear it, matching the SDL FOCUS_LOST path. */
        if( bus )
            CmdBus_Push(bus, TORIRS_CMD_INPUT_CLEAR_KEYS, NULL, 0);
        break;

    case WM_MOUSEMOVE:
        if( bus )
        {
            map_mouse(p, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &lx, &ly);
            CmdBus_PushMouseMove(bus, (int16_t)lx, (int16_t)ly);
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if( bus )
        {
            SetCapture(hwnd);
            map_mouse(p, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &lx, &ly);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_DOWN, win_button_to_torirs(msg),
                (int16_t)lx, (int16_t)ly);
        }
        break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if( bus )
        {
            ReleaseCapture();
            map_mouse(p, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &lx, &ly);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_UP, win_button_to_torirs(msg),
                (int16_t)lx, (int16_t)ly);
        }
        break;

    case WM_MOUSEWHEEL:
        if( bus )
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            CmdBus_PushMouseWheel(bus, (int16_t)delta);
        }
        break;

    case WM_TOUCH:
        if( bus && g_get_touch_input_info )
        {
            /*
             * Windows hands over a BATCH -- every contact that changed in this
             * frame of the digitiser's report, not one message per finger --
             * and the handle behind it has to be closed whatever happens next,
             * or the driver runs out of them and touch stops working for the
             * whole session.
             */
            UINT const count = LOWORD(wparam);
            TORIRS_TOUCHINPUT inputs[TORIRS_TOUCH_MAX];
            UINT const wanted = count > TORIRS_TOUCH_MAX ? TORIRS_TOUCH_MAX : count;

            if( wanted &&
                g_get_touch_input_info(
                    (HANDLE)lparam, wanted, inputs, (int)sizeof(TORIRS_TOUCHINPUT)) )
            {
                for( UINT i = 0; i < wanted; i++ )
                {
                    POINT pt;
                    enum ToriRS_TouchPhase phase = TORIRS_TOUCH_MOVED;
                    int game_w = 0;
                    int game_h = 0;

                    /* Screen space, in HUNDREDTHS of a pixel, so it is divided
                     * down before being made client-relative. */
                    pt.x = inputs[i].x / 100;
                    pt.y = inputs[i].y / 100;
                    ScreenToClient(hwnd, &pt);
                    /* Child chrome owns every contact in its reserved region.
                     * WM_TOUCH can still be delivered to the registered parent
                     * on older digitiser stacks, so enforce the same boundary
                     * here that child HWND hit-testing gives mouse input. */
                    if( win32_game_client_size(p, &game_w, &game_h) &&
                        (pt.x < 0 || pt.y < 0 || pt.x >= game_w || pt.y >= game_h) )
                        continue;
                    map_mouse(p, pt.x, pt.y, &lx, &ly);

                    if( inputs[i].flags & TOUCHEVENTF_DOWN )
                        phase = TORIRS_TOUCH_BEGAN;
                    else if( inputs[i].flags & TOUCHEVENTF_UP )
                        phase = TORIRS_TOUCH_ENDED;
                    ToriRS_TouchEvent(
                        &p->touch, bus, phase, (int64_t)inputs[i].id, lx, ly,
                        (uint64_t)GetTickCount());
                }
            }
            g_close_touch_input_handle((HANDLE)lparam);
            return 0;
        }
        break;

    case WM_SIZE:
        if( p && wparam != SIZE_MINIMIZED )
        {
            int const client_w = LOWORD(lparam);
            int const client_h = HIWORD(lparam);
            int game_w;

            win32_chrome_layout(p, client_w, client_h);
            if( p->gdi_frame_valid )
                InvalidateRect(hwnd, NULL, FALSE);
            if( p->canvas_follows_window )
            {
                game_w = client_w - win32_chrome_reserved_width(p);
                if( game_w < 0 )
                    game_w = 0;
                p->pending_resize_w = game_w;
                p->pending_resize_h = client_h;
            }
        }
        break;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

/* ---- lifecycle ---------------------------------------------------------- */

struct PlatformWindow*
PlatformWindow_New(void)
{
    struct PlatformWindow* p = (struct PlatformWindow*)malloc(sizeof(*p));
    assert(p);
    memset(p, 0, sizeof(*p));
    p->pending_resize_w = -1;
    p->pending_resize_h = -1;
    p->interface_scale_mode = 2;
    return p;
}

void
PlatformWindow_SetInterfaceScaleMode(struct PlatformWindow* p, int mode)
{
    assert(p);
    if( mode < 0 )
        mode = 0;
    if( mode > 2 )
        mode = 2;
    if( p->interface_scale_mode == mode )
        return;
    p->interface_scale_mode = mode;
    if( p->hwnd && p->gdi_frame_valid )
        InvalidateRect(p->hwnd, NULL, FALSE);
}

/* The window icon, from the RGBA that tools/make_app_icons.py embedded.
 *
 * Built at runtime rather than linked as an .rc resource: this lane has no
 * resource-compile step, and adding one would mean a second description of
 * the same artwork living in the build system. The bits come from the same
 * generated array the SDL backend uses.
 *
 * A 32-bit DIB section carries its own alpha, so the AND mask exists only
 * because ICONINFO requires one; it is left all-zero (fully opaque) and the
 * alpha channel does the shaping.
 */
static HICON
win32_create_app_icon(void)
{
    BITMAPV5HEADER header;
    ICONINFO info;
    HICON icon;
    HBITMAP color;
    HBITMAP mask;
    void* bits = NULL;
    HDC screen;
    int i;
    int const width = platform_app_icon_width;
    int const height = platform_app_icon_height;
    int const pixels = width * height;

    memset(&header, 0, sizeof(header));
    header.bV5Size = sizeof(header);
    header.bV5Width = width;
    /* Negative height: a top-down DIB, matching the generator's row order. */
    header.bV5Height = -height;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000u;
    header.bV5GreenMask = 0x0000FF00u;
    header.bV5BlueMask = 0x000000FFu;
    header.bV5AlphaMask = 0xFF000000u;

    screen = GetDC(NULL);
    color = CreateDIBSection(
        screen, (BITMAPINFO*)&header, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    assert(color);
    assert(bits);

    /* The generated array is R,G,B,A in memory order; a 32-bit DIB word is
     * 0xAARRGGBB, so the red and blue bytes swap on the way in. */
    for( i = 0; i < pixels; i++ )
    {
        unsigned char const* source = &platform_app_icon_rgba[i * 4];
        ((unsigned int*)bits)[i] = ((unsigned int)source[3] << 24) |
                                   ((unsigned int)source[0] << 16) |
                                   ((unsigned int)source[1] << 8) |
                                   ((unsigned int)source[2]);
    }

    mask = CreateBitmap(width, height, 1, 1, NULL);
    assert(mask);

    memset(&info, 0, sizeof(info));
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
    assert(icon);

    /* CreateIconIndirect copies both bitmaps. */
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static int
register_class(void)
{
    static int registered = 0;
    WNDCLASSA wc;
    if( registered )
        return 1;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.cbWndExtra = (int)(2 * sizeof(LONG_PTR));
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* hIconSm is left NULL: Windows derives the small icon by scaling
     * hIcon, and the generated art has no separate small-size variant
     * for it to prefer. */
    wc.hIcon = win32_create_app_icon();
    /* The retained DIB repaints invalid regions; a class brush would erase the
     * frame before WM_PAINT and recreate the black flash this backend avoids. */
    wc.hbrBackground = NULL;
    wc.lpszClassName = RPD_WNDCLASS;
    if( !RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
        return 0;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = chrome_rail_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = RPD_CHROME_RAIL_WNDCLASS;
    if( !RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
        return 0;
    registered = 1;
    return 1;
}

bool
PlatformWindow_Init(struct PlatformWindow* p, int width, int height, char const* title)
{
    RECT r;
    HDC screen;
    DWORD style;
    int fullscreen;

    assert(p);
    assert(width > 0 && height > 0);

    p->esc_quits = getenv("TORIRS_ESC_QUIT") != NULL;

    if( !register_class() )
        return false;

    /* TORIRS_WIN32_FULLSCREEN=1: a borderless window at the desktop origin,
     * so the client area IS the screen.
     *
     * The ordinary window is WS_OVERLAPPEDWINDOW, and asking that for a
     * client area the size of the screen produces an outer window LARGER than
     * the screen -- the caption sits at y=0 and the bottom rows of the canvas
     * fall off the desktop. The client would then be rendering pixels the
     * desktop never shows, which is exactly the wrong thing to hand a renderer
     * benchmark: soft3d's blit shrinks with the visible area while D3D9's
     * backbuffer does not, so the two lanes would stop drawing the same frame.
     * WS_POPUP has no non-client area at all, so the client area asked for is
     * the client area obtained.
     *
     * This decides the window's STYLE, not its size -- the caller still owns
     * that, and fills the screen by passing the screen's own resolution. */
    fullscreen = getenv("TORIRS_WIN32_FULLSCREEN") ? 1 : 0;
    style = (fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN;

    r.left = 0;
    r.top = 0;
    r.right = width;
    r.bottom = height;
    AdjustWindowRect(&r, style, FALSE);

    p->hwnd = CreateWindowExA(
        0, RPD_WNDCLASS, title ? title : "ToriRS",
        style,
        fullscreen ? 0 : CW_USEDEFAULT, fullscreen ? 0 : CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);
    if( !p->hwnd )
        return false;
    SetWindowLongPtr(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    SetWindowLongPtr(
        p->hwnd,
        TORIRS_WIN32_CHROME_EXTRA_MAGIC_OFFSET,
        (LONG_PTR)TORIRS_WIN32_CHROME_EXTRA_MAGIC);

    /* Asked for, and a refusal is an answer: a desktop without a digitiser
     * says no here and simply never sends WM_TOUCH. TWF_WANTPALM keeps the
     * contacts the OS would otherwise filter as an accidental palm, because
     * this frame is played with the hand ON the screen. */
    ToriRS_TouchReset(&p->touch);
    if( win32_touch_load() )
        g_register_touch_window(p->hwnd, TWF_WANTPALM);

    /* Held for the window's life rather than fetched per present; see the field
     * comment. Also the DC the memory DC is made compatible with. */
    p->window_dc = GetDC(p->hwnd);
    if( !p->window_dc )
    {
        DestroyWindow(p->hwnd);
        p->hwnd = NULL;
        return false;
    }
    screen = p->window_dc;
    p->mem_dc = CreateCompatibleDC(screen);
    if( !p->mem_dc )
    {
        /* DestroyWindow frees the CS_OWNDC handle with the window. */
        DestroyWindow(p->hwnd);
        p->hwnd = NULL;
        p->window_dc = NULL;
        return false;
    }
    if( !gdi_make_dib(p, width, height) )
    {
        DeleteDC(p->mem_dc);
        p->mem_dc = NULL;
        DestroyWindow(p->hwnd);
        p->hwnd = NULL;
        p->window_dc = NULL;
        return false;
    }

    p->timing_active = PlatformWin32Timing_Init() ? 1 : 0;

    ShowWindow(p->hwnd, getenv("TORIRS_WIN32_HIDDEN") ? SW_HIDE : SW_SHOW);
    UpdateWindow(p->hwnd);
    return true;
}

/* GL is not available in this backend; main.c only calls these under
 * TORIRS_HAVE_GL3, which the win32gdi target does not define. Safe stubs keep
 * the symbols resolvable regardless. */
bool
PlatformWindow_InitForOpenGL3(struct PlatformWindow* p, int width, int height, char const* title)
{
    (void)p;
    (void)width;
    (void)height;
    (void)title;
    return false;
}

ToriRS_GLWindow*
PlatformWindow_GLWindow(struct PlatformWindow* p)
{
    (void)p;
    /* No GL renderer on this lane -- it selects fixed-function D3D9, which
     * consumes the HWND below rather than a GL window. */
    return NULL;
}

void*
PlatformWindow_NativeWindowHandle(struct PlatformWindow* p)
{
    return p ? (void*)p->hwnd : NULL;
}

bool
PlatformWindow_ChromeOpen(
    struct PlatformWindow* p, int width, int height, char const* title)
{
    int client_w;
    int client_h;
    int prior_width;
    LONG_PTR style;

    (void)title; /* The attached page shares its top-level window's title. */
    assert(p);
    if( !p->hwnd || width <= 0 || !PlatformWindow_PluginBrowserEnsure(p) )
        return false;
    if( !win32_main_client_size(p, &client_w, &client_h) )
        return false;

    if( p->chrome_open )
    {
        prior_width = p->chrome_requested_w;
        p->chrome_requested_w = width;
        style = GetWindowLongPtr(p->hwnd, GWL_STYLE);
        if( width != prior_width && !(style & WS_POPUP) && !(style & WS_MAXIMIZE) )
            win32_resize_client(
                p, client_w + width - prior_width,
                height > client_h ? height : client_h);
        win32_main_client_size(p, &client_w, &client_h);
        win32_chrome_layout(p, client_w, client_h);
        p->chrome_width = p->chrome_layout_w;
        p->chrome_height = client_h;
        return p->chrome_layout_w > 0 && client_h > 0;
    }

    p->chrome_requested_w = width;
    p->chrome_collapsed_client_w = client_w;
    p->chrome_open = 1;

    /* The browser/rail was grown separately and remains part of the collapsed
     * width. Expanding adds only the page portion to that same child. */
    style = GetWindowLongPtr(p->hwnd, GWL_STYLE);
    if( !(style & WS_POPUP) && !(style & WS_MAXIMIZE) )
        win32_resize_client(
            p,
            p->chrome_collapsed_client_w + width,
            height > client_h ? height : client_h);
    win32_main_client_size(p, &client_w, &client_h);
    win32_chrome_layout(p, client_w, client_h);
    if( p->chrome_layout_w <= 0 || client_h <= 0 )
    {
        PlatformWindow_ChromeClose(p);
        return false;
    }

    p->chrome_width = p->chrome_layout_w;
    p->chrome_height = client_h;
    InvalidateRect(p->chrome_rail_hwnd, NULL, FALSE);
    return true;
}

bool
PlatformWindow_ChromeRailOpen(
    struct PlatformWindow* p, int width, char const* title)
{
    (void)width; /* Windows browser bundle owns its 48px (legacy 46px) rail. */
    (void)title;
    assert(p);
    return PlatformWindow_PluginBrowserEnsure(p);
}

bool
PlatformWindow_ChromeSetPageWidth(struct PlatformWindow* p, int page_width)
{
    assert(p);
    if( !p->chrome_open || page_width <= 0 )
        return false;
    return PlatformWindow_ChromeOpen(
        p, page_width, p->chrome_height > 0 ? p->chrome_height : 480, "Plugins");
}

void
PlatformWindow_ChromeClose(struct PlatformWindow* p)
{
    int client_w = 0;
    int client_h = 0;
    int restore_w;
    int pane_w;
    LONG_PTR style;

    assert(p);
    if( !p->chrome_open )
        return;
    win32_main_client_size(p, &client_w, &client_h);
    pane_w = p->chrome_layout_w;
    restore_w = client_w - pane_w;
    if( restore_w < p->chrome_collapsed_client_w )
        restore_w = p->chrome_collapsed_client_w;

    /* Publish collapsed before resizing. The browser object and its DOM rail
     * survive; only its attached allocation contracts. */
    p->chrome_open = 0;
    p->chrome_layout_w = 0;
    p->chrome_requested_w = 0;
    p->chrome_width = 0;
    p->chrome_height = 0;
    SetFocus(p->hwnd);

    style = GetWindowLongPtr(p->hwnd, GWL_STYLE);
    if( !(style & WS_POPUP) && !(style & WS_MAXIMIZE) && restore_w > 0 && client_h > 0 )
        win32_resize_client(p, restore_w, client_h);
    win32_main_client_size(p, &client_w, &client_h);
    win32_chrome_layout(p, client_w, client_h);
    if( p->chrome_rail_hwnd )
        InvalidateRect(p->chrome_rail_hwnd, NULL, FALSE);
}

bool
PlatformWindow_ChromeIsOpen(struct PlatformWindow const* p)
{
    assert(p);
    return p->chrome_open != 0;
}

int*
PlatformWindow_ChromePixels(struct PlatformWindow* p)
{
    assert(p);
    return NULL; /* Windows plugin chrome is a browser control, not a surface. */
}

int
PlatformWindow_ChromeWidth(struct PlatformWindow const* p)
{
    assert(p);
    return p->chrome_open ? p->chrome_width : 0;
}

int
PlatformWindow_ChromeHeight(struct PlatformWindow const* p)
{
    assert(p);
    return p->chrome_open ? p->chrome_height : 0;
}

int
PlatformWindow_ChromeRailWidth(struct PlatformWindow const* p)
{
    return p && p->chrome_rail_hwnd ? WIN32_CHROME_RAIL_W : 0;
}

int
PlatformWindow_ChromePageWidth(struct PlatformWindow const* p)
{
    return p && p->chrome_open ? p->chrome_layout_w : 0;
}

bool
PlatformWindow_ChromeResize(struct PlatformWindow* p, int width, int height)
{
    assert(p);
    (void)width;
    (void)height;
    return false;
}

void
PlatformWindow_ChromePresent(struct PlatformWindow* p)
{
    assert(p);
}

bool
PlatformWindow_ChromeTakeDirty(struct PlatformWindow* p)
{
    /* This backend blits the retained pane straight into its child HWND; there
     * is no GPU texture upload to schedule. */
    assert(p);
    return false;
}

bool
PlatformWindow_ChromeIsDirty(struct PlatformWindow const* p)
{
    (void)p;
    return false;
}

bool
PlatformWindow_ChromeTakeInput(
    struct PlatformWindow* p, struct PlatformWindow_AuxInput* out)
{
    assert(p);
    assert(out);
    /* Browser input stays inside its native child and exits only as a copied,
     * generation-fenced protocol intent. */
    return false;
}

bool
PlatformWindow_ChromeTakeRailInput(
    struct PlatformWindow* p, struct PlatformWindow_AuxInput* out)
{
    (void)p;
    (void)out;
    /* Browser DOM events cross through the protocol queue, never this raw
     * surface-only input seam. */
    return false;
}

void*
PlatformWindow_Win32ChromeHandle(struct PlatformWindow* p)
{
    return p ? (void*)p->chrome_rail_hwnd : NULL;
}

#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
bool
PlatformWindow_Win32BrowserCapturePng(
    struct PlatformWindow* p, char const* path)
{
    return p && p->chrome_browser && path && path[0] &&
           PlatformWin32Browser_CapturePng(p->chrome_browser, path) != 0;
}

int
PlatformWindow_Win32BrowserCaptureStatus(struct PlatformWindow const* p)
{
    return p && p->chrome_browser
               ? PlatformWin32Browser_CaptureStatus(p->chrome_browser)
               : -1;
}
#endif

static int
win32_browser_file_url(WCHAR const* path, WCHAR* out, int capacity)
{
    static WCHAR const prefix[] = L"file:///";
    int at = 0;

    if( !path || !path[0] || !out || capacity < 16 )
        return 0;
    for( int i = 0; prefix[i] && at < capacity - 1; i++ )
        out[at++] = prefix[i];
    for( int i = 0; path[i] && at < capacity - 4; i++ )
    {
        WCHAR const c = path[i] == L'\\' ? L'/' : path[i];
        if( c == L' ' )
        {
            out[at++] = L'%';
            out[at++] = L'2';
            out[at++] = L'0';
        }
        else
            out[at++] = c;
    }
    out[at] = 0;
    return 1;
}

static int
win32_browser_asset_dir(struct PlatformWindow* p)
{
    WCHAR temp[MAX_PATH];
    WCHAR unique[MAX_PATH];
    WCHAR guid_text[40];
    WCHAR guid_name[40];
    GUID guid;
    int length;
    int made = 0;

    if( p->chrome_browser_asset_dir[0] )
        return 1;
    length = (int)GetTempPathW(MAX_PATH, temp);
    if( length <= 0 || length >= MAX_PATH )
        return 0;
    /* CreateDirectory is the atomic claim. A GUID avoids GetTempFileName's
     * 16-bit namespace and its delete-then-create race; the directory inherits
     * the current user's private temp-directory ACL on XP and modern Windows. */
    for( int attempt = 0; attempt < 8; attempt++ )
    {
        if( FAILED(CoCreateGuid(&guid)) ||
            StringFromGUID2(&guid, guid_text, 40) <= 0 )
            return 0;
        {
            int at = 0;
            for( int i = 0; guid_text[i] && at < 39; i++ )
                if( guid_text[i] != L'{' && guid_text[i] != L'}' )
                    guid_name[at++] = guid_text[i];
            guid_name[at] = 0;
        }
        if( _snwprintf(
                unique, MAX_PATH, L"%sToriRS-PluginChrome-%lu-%s",
                temp, (unsigned long)GetCurrentProcessId(), guid_name) <= 0 )
            return 0;
        if( CreateDirectoryW(unique, NULL) )
        {
            made = 1;
            break;
        }
        if( GetLastError() != ERROR_ALREADY_EXISTS )
            return 0;
    }
    if( !made )
        return 0;
    length = (int)wcslen(unique);
    if( length <= 0 || length >= MAX_PATH )
    {
        p->chrome_browser_asset_dir[0] = 0;
        return 0;
    }
    lstrcpynW(p->chrome_browser_asset_dir, unique, MAX_PATH);
    if( !win32_browser_file_url(
            p->chrome_browser_asset_dir,
            p->chrome_browser_asset_url,
            (int)(sizeof(p->chrome_browser_asset_url) /
                  sizeof(p->chrome_browser_asset_url[0]))) )
        return 0;
    length = (int)wcslen(p->chrome_browser_asset_url);
    if( length + 1 >= (int)(sizeof(p->chrome_browser_asset_url) /
                            sizeof(p->chrome_browser_asset_url[0])) )
        return 0;
    if( length && p->chrome_browser_asset_url[length - 1] != L'/' )
    {
        p->chrome_browser_asset_url[length++] = L'/';
        p->chrome_browser_asset_url[length] = 0;
    }
    return 1;
}

static int
win32_browser_find_source(WCHAR* out, int capacity)
{
    WCHAR env[MAX_PATH];
    WCHAR executable[MAX_PATH];
    WCHAR full[MAX_PATH];
    WCHAR probe[MAX_PATH];
    WCHAR* slash;
    static WCHAR const* candidates[] = {
        L"plugin_chrome", L"src\\plugin_chrome", L"..\\src\\plugin_chrome"
    };
    DWORD n = GetEnvironmentVariableW(
        L"TORIRS_PLUGIN_CHROME_DIR", env, MAX_PATH);

    if( n > 0 && n < MAX_PATH )
    {
        GetFullPathNameW(env, MAX_PATH, full, NULL);
        _snwprintf(probe, MAX_PATH, L"%s\\modern.html", full);
        if( GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES )
        {
            lstrcpynW(out, full, capacity);
            return 1;
        }
    }

    /* Standalone distributions keep the immutable canonical bundle beside
     * torirs.exe. Resolve it from the module path, never the caller's current
     * directory, so shortcuts and remote launchers cannot change what loads. */
    n = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if( n > 0 && n < MAX_PATH )
    {
        slash = wcsrchr(executable, L'\\');
        if( slash )
        {
            *slash = 0;
            if( _snwprintf(
                    full, MAX_PATH, L"%s\\plugin_chrome", executable) > 0 )
            {
                _snwprintf(probe, MAX_PATH, L"%s\\modern.html", full);
                if( GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES )
                {
                    lstrcpynW(out, full, capacity);
                    return 1;
                }
            }
        }
    }
    for( int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++ )
    {
        if( !GetFullPathNameW(candidates[i], MAX_PATH, full, NULL) )
            continue;
        _snwprintf(probe, MAX_PATH, L"%s\\modern.html", full);
        if( GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES )
            continue;
        lstrcpynW(out, full, capacity);
        return 1;
    }
    return 0;
}

static int
win32_browser_copy(
    WCHAR const* source_root, WCHAR const* target_root, WCHAR const* name)
{
    WCHAR source[MAX_PATH];
    WCHAR target[MAX_PATH];

    if( _snwprintf(source, MAX_PATH, L"%s\\%s", source_root, name) <= 0 ||
        _snwprintf(target, MAX_PATH, L"%s\\%s", target_root, name) <= 0 )
        return 0;
    return CopyFileW(source, target, FALSE) != 0;
}

static void
win32_browser_stage_skin(
    WCHAR const* bundle_source, WCHAR const* target_root)
{
    static WCHAR const* const names[] = {
        L"PanelBody.png", L"PluginIcon.png", L"ButtonLeft.png",
        L"ButtonMid.png", L"ButtonRight.png", L"CheckOn.png",
        L"CheckOff.png", L"CheckBoxOn.png", L"CheckBoxOff.png",
        L"DropdownBody.png", L"ScrollUp.png", L"ScrollDown.png",
        L"ScrollTrack.png", L"ScrollGripTop.png", L"ScrollGripMid.png",
        L"ScrollGripBottom.png", L"CloseButton.png", L"CloseButtonOver.png",
        L"FrameTopLeft.png",
        L"FrameTop.png", L"FrameTopRight.png", L"FrameLeft.png",
        L"FrameRight.png", L"FrameBottomLeft.png", L"FrameBottom.png",
        L"FrameBottomRight.png"
    };
    WCHAR source[MAX_PATH];
    WCHAR target[MAX_PATH];
    WCHAR probe[MAX_PATH];
    WCHAR skin_source[MAX_PATH];
    WCHAR skin_target[MAX_PATH];
    static WCHAR const* const candidates[] = {
        L"res\\plugin_chrome\\skin", L"..\\res\\plugin_chrome\\skin"
    };

    _snwprintf(probe, MAX_PATH, L"%s\\skin\\PanelBody.png", bundle_source);
    if( GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES )
        _snwprintf(skin_source, MAX_PATH, L"%s\\skin", bundle_source);
    else
    {
        skin_source[0] = 0;
        for( int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++ )
        {
            if( !GetFullPathNameW(candidates[i], MAX_PATH, probe, NULL) )
                continue;
            _snwprintf(source, MAX_PATH, L"%s\\PanelBody.png", probe);
            if( GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES )
            {
                lstrcpynW(skin_source, probe, MAX_PATH);
                break;
            }
        }
    }
    if( !skin_source[0] )
        return; /* flat palette remains complete without optional baked art */
    _snwprintf(skin_target, MAX_PATH, L"%s\\skin", target_root);
    CreateDirectoryW(skin_target, NULL);
    for( int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++ )
    {
        _snwprintf(source, MAX_PATH, L"%s\\%s", skin_source, names[i]);
        _snwprintf(target, MAX_PATH, L"%s\\%s", skin_target, names[i]);
        CopyFileW(source, target, FALSE);
    }
}

static int
win32_browser_stage_fonts(
    WCHAR const* bundle_source, WCHAR const* target_root)
{
    static WCHAR const* const names[] = {
        L"ToriRSBody.eot", L"ToriRSBody.woff", L"ToriRSBody.ttf",
        L"ToriRSMenu.eot", L"ToriRSMenu.woff", L"ToriRSMenu.ttf",
        L"ToriRSSmall.eot", L"ToriRSSmall.woff", L"ToriRSSmall.ttf",
        L"manifest.json", L"README.md"
    };
    static WCHAR const* const candidates[] = {
        L"res\\plugin_chrome\\font", L"..\\res\\plugin_chrome\\font"
    };
    WCHAR source[MAX_PATH];
    WCHAR target[MAX_PATH];
    WCHAR probe[MAX_PATH];
    WCHAR font_source[MAX_PATH];
    WCHAR font_target[MAX_PATH];

    _snwprintf(probe, MAX_PATH, L"%s\\font\\ToriRSBody.eot", bundle_source);
    if( GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES )
        _snwprintf(font_source, MAX_PATH, L"%s\\font", bundle_source);
    else
    {
        font_source[0] = 0;
        for( int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++ )
        {
            if( !GetFullPathNameW(candidates[i], MAX_PATH, probe, NULL) )
                continue;
            _snwprintf(source, MAX_PATH, L"%s\\ToriRSBody.eot", probe);
            if( GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES )
            {
                lstrcpynW(font_source, probe, MAX_PATH);
                break;
            }
        }
    }
    if( !font_source[0] )
        return 0;
    _snwprintf(font_target, MAX_PATH, L"%s\\font", target_root);
    if( !CreateDirectoryW(font_target, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS )
        return 0;
    for( int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++ )
    {
        _snwprintf(source, MAX_PATH, L"%s\\%s", font_source, names[i]);
        _snwprintf(target, MAX_PATH, L"%s\\%s", font_target, names[i]);
        if( !CopyFileW(source, target, FALSE) )
            return 0;
    }
    return 1;
}

static int
win32_browser_stage_bundle(struct PlatformWindow* p)
{
    static WCHAR const* const files[] = {
        L"modern.html", L"modern.css", L"codec-es3.js", L"runtime.js",
        L"legacy-ie8.html", L"legacy-ie8.css", L"runtime-ie8.js"
    };
    WCHAR source[MAX_PATH];
    WCHAR bitmap[MAX_PATH];

    if( !win32_browser_asset_dir(p) ||
        !win32_browser_find_source(source, MAX_PATH) )
        return 0;
    for( int i = 0; i < (int)(sizeof(files) / sizeof(files[0])); i++ )
        if( !win32_browser_copy(source, p->chrome_browser_asset_dir, files[i]) )
            return 0;
    _snwprintf(bitmap, MAX_PATH, L"%s\\bitmap", p->chrome_browser_asset_dir);
    if( !CreateDirectoryW(bitmap, NULL) && GetLastError() != ERROR_ALREADY_EXISTS )
        return 0;
    win32_browser_stage_skin(source, p->chrome_browser_asset_dir);
    if( !win32_browser_stage_fonts(source, p->chrome_browser_asset_dir) )
        return 0;
    return 1;
}

bool
PlatformWindow_PluginBrowserEnsure(struct PlatformWindow* p)
{
    RECT client;

    if( !p || !p->hwnd || !win32_chrome_ensure_rail(p) )
        return false;
    if( p->chrome_browser )
        return !PlatformWin32Browser_Failed(p->chrome_browser);
    if( !win32_browser_stage_bundle(p) )
        return false;
    p->chrome_browser = PlatformWin32Browser_New(
        p->chrome_rail_hwnd, p->chrome_browser_asset_dir);
    if( !p->chrome_browser )
        return false;
    if( win32_browser_asset_dir(p) )
        PlatformWin32Browser_AllowLocalRoot(
            p->chrome_browser, p->chrome_browser_asset_url);
    if( GetClientRect(p->chrome_rail_hwnd, &client) )
        PlatformWin32Browser_Resize(
            p->chrome_browser, client.right - client.left, client.bottom - client.top);
    return !PlatformWin32Browser_Failed(p->chrome_browser);
}

bool
PlatformWindow_PluginBrowserReady(struct PlatformWindow const* p)
{
    return p && p->chrome_browser &&
           PlatformWin32Browser_Ready(p->chrome_browser) != 0;
}

bool
PlatformWindow_PluginBrowserFailed(struct PlatformWindow const* p)
{
    return !p || (p->chrome_browser &&
                  PlatformWin32Browser_Failed(p->chrome_browser));
}

bool
PlatformWindow_PluginBrowserSend(struct PlatformWindow* p, char const* json)
{
    if( !p || !json || !PlatformWindow_PluginBrowserEnsure(p) )
        return false;
    return PlatformWin32Browser_Send(p->chrome_browser, json) != 0;
}

bool
PlatformWindow_PluginBrowserTakeSendFailure(struct PlatformWindow* p)
{
    return p && p->chrome_browser &&
           PlatformWin32Browser_TakeSendFailure(p->chrome_browser) != 0;
}

int
PlatformWindow_PluginBrowserPoll(
    struct PlatformWindow* p, char* out_json, int capacity)
{
    if( !p || !p->chrome_browser || !out_json || capacity <= 0 )
        return 0;
    return PlatformWin32Browser_Poll(p->chrome_browser, out_json, capacity);
}

static void
win32_browser_remove_files(WCHAR const* directory)
{
    WCHAR pattern[MAX_PATH];
    WCHAR path[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;

    if( !directory || !directory[0] ||
        _snwprintf(pattern, MAX_PATH, L"%s\\*", directory) <= 0 )
        return;
    search = FindFirstFileW(pattern, &found);
    if( search == INVALID_HANDLE_VALUE )
        return;
    do
    {
        if( found.cFileName[0] == L'.' &&
            (!found.cFileName[1] ||
             (found.cFileName[1] == L'.' && !found.cFileName[2])) )
            continue;
        if( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            continue;
        if( _snwprintf(path, MAX_PATH, L"%s\\%s", directory, found.cFileName) > 0 )
        {
            if( found.dwFileAttributes & FILE_ATTRIBUTE_READONLY )
                SetFileAttributesW(
                    path, found.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            DeleteFileW(path);
        }
    } while( FindNextFileW(search, &found) );
    FindClose(search);
}

static void
win32_browser_remove_bundle(struct PlatformWindow* p)
{
    WCHAR child[MAX_PATH];

    if( !p || !p->chrome_browser_asset_dir[0] )
        return;
    _snwprintf(child, MAX_PATH, L"%s\\bitmap", p->chrome_browser_asset_dir);
    win32_browser_remove_files(child);
    RemoveDirectoryW(child);
    _snwprintf(child, MAX_PATH, L"%s\\skin", p->chrome_browser_asset_dir);
    win32_browser_remove_files(child);
    RemoveDirectoryW(child);
    _snwprintf(child, MAX_PATH, L"%s\\font", p->chrome_browser_asset_dir);
    win32_browser_remove_files(child);
    RemoveDirectoryW(child);
    win32_browser_remove_files(p->chrome_browser_asset_dir);
    RemoveDirectoryW(p->chrome_browser_asset_dir);
    p->chrome_browser_asset_dir[0] = 0;
    p->chrome_browser_asset_url[0] = 0;
}

bool
PlatformWindow_PluginBrowserBitmapUrl(
    struct PlatformWindow* p,
    char const* cache_key,
    uint32_t revision,
    uint32_t const* argb,
    int width,
    int height,
    char* out_url,
    int capacity)
{
    WCHAR path[MAX_PATH];
    WCHAR temporary[MAX_PATH];
    WCHAR previous[MAX_PATH];
    WCHAR name[128];
    BITMAPFILEHEADER file;
    BITMAPINFOHEADER info;
    HANDLE handle;
    uint32_t* row;
    DWORD wrote;
    int key_length;
    int cache_slot = -1;
    int cache_existing = 0;
    int cache_append = 0;
    int cache_evict = 0;
    int path_length;
    int url_length;
    char relative_url[128];

    if( !p || !cache_key || !cache_key[0] || !argb || !out_url || capacity <= 0 ||
        revision == 0 || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > WIN32_BROWSER_BITMAP_PIXELS_MAX )
        return false;
    key_length = (int)strlen(cache_key);
    if( key_length <= 0 || key_length >= 96 )
        return false;
    for( int i = 0; i < key_length; i++ )
        if( !((cache_key[i] >= 'a' && cache_key[i] <= 'z') ||
              (cache_key[i] >= 'A' && cache_key[i] <= 'Z') ||
              (cache_key[i] >= '0' && cache_key[i] <= '9') ||
              cache_key[i] == '-' || cache_key[i] == '_' || cache_key[i] == '.') )
            return false;
    if( !PlatformWindow_PluginBrowserEnsure(p) || !win32_browser_asset_dir(p) )
        return false;
    if( !MultiByteToWideChar(CP_ACP, 0, cache_key, -1, name, 128) )
        return false;
    url_length = snprintf(
        relative_url,
        sizeof(relative_url),
        "bitmap/%s-r%lu.bmp",
        cache_key,
        (unsigned long)revision);
    /* Output failure is decided before touching the retained cache or disk. */
    if( url_length <= 0 || url_length >= (int)sizeof(relative_url) ||
        url_length >= capacity )
        return false;
    path_length = _snwprintf(
        path, MAX_PATH, L"%s\\bitmap\\%s-r%lu.bmp",
        p->chrome_browser_asset_dir, name, (unsigned long)revision);
    if( path_length <= 0 || path_length >= MAX_PATH ||
        (path_length = _snwprintf(temporary, MAX_PATH, L"%s.tmp", path)) <= 0 ||
        path_length >= MAX_PATH )
        return false;

    previous[0] = 0;
    for( int i = 0; i < p->chrome_browser_bitmap_count; i++ )
        if( strcmp(p->chrome_browser_bitmaps[i].key, cache_key) == 0 )
        {
            cache_slot = i;
            cache_existing = 1;
            break;
        }
    if( cache_slot < 0 )
    {
        if( p->chrome_browser_bitmap_count < WIN32_BROWSER_BITMAP_CACHE_MAX )
        {
            cache_slot = p->chrome_browser_bitmap_count;
            cache_append = 1;
        }
        else
        {
            cache_slot = p->chrome_browser_bitmap_evict;
            cache_evict = 1;
        }
    }
    if( p->chrome_browser_bitmaps[cache_slot].path[0] )
        lstrcpynW(previous, p->chrome_browser_bitmaps[cache_slot].path, MAX_PATH);
    if( cache_existing && p->chrome_browser_bitmaps[cache_slot].revision )
    {
        int32_t const revision_delta =
            (int32_t)(revision - p->chrome_browser_bitmaps[cache_slot].revision);
        if( revision_delta < 0 )
            return false;
        if( revision_delta == 0 &&
            previous[0] && _wcsicmp(previous, path) == 0 &&
            GetFileAttributesW(previous) != INVALID_FILE_ATTRIBUTES )
        {
            memcpy(out_url, relative_url, (size_t)url_length + 1);
            return true;
        }
    }

    memset(&file, 0, sizeof(file));
    memset(&info, 0, sizeof(info));
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + (DWORD)((size_t)width * (size_t)height * 4u);
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = file.bfSize - file.bfOffBits;
    handle = CreateFileW(
        temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, NULL);
    if( handle == INVALID_HANDLE_VALUE )
        return false;
    if( !WriteFile(handle, &file, sizeof(file), &wrote, NULL) || wrote != sizeof(file) ||
        !WriteFile(handle, &info, sizeof(info), &wrote, NULL) || wrote != sizeof(info) )
    {
        CloseHandle(handle);
        DeleteFileW(temporary);
        return false;
    }
    row = (uint32_t*)malloc((size_t)width * sizeof(*row));
    if( !row )
    {
        CloseHandle(handle);
        DeleteFileW(temporary);
        return false;
    }
    for( int y = height - 1; y >= 0; y-- )
    {
        for( int x = 0; x < width; x++ )
        {
            uint32_t const source = argb[(size_t)y * (size_t)width + (size_t)x];
            unsigned const alpha = source >> 24;
            unsigned const red =
                (((source >> 16) & 255u) * alpha + 0x37u * (255u - alpha)) / 255u;
            unsigned const green =
                (((source >> 8) & 255u) * alpha + 0x2Eu * (255u - alpha)) / 255u;
            unsigned const blue =
                ((source & 255u) * alpha + 0x22u * (255u - alpha)) / 255u;
            row[x] = (red << 16) | (green << 8) | blue;
        }
        if( !WriteFile(
                handle, row, (DWORD)((size_t)width * sizeof(*row)), &wrote, NULL) ||
            wrote != (DWORD)((size_t)width * sizeof(*row)) )
        {
            free(row);
            CloseHandle(handle);
            DeleteFileW(temporary);
            return false;
        }
    }
    free(row);
    CloseHandle(handle);
    if( !MoveFileExW(
            temporary, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
    {
        DeleteFileW(temporary);
        return false;
    }
    if( previous[0] && _wcsicmp(previous, path) != 0 &&
        !DeleteFileW(previous) && GetLastError() != ERROR_FILE_NOT_FOUND &&
        GetLastError() != ERROR_PATH_NOT_FOUND )
    {
        /* Never forget an undeletable entry and thereby exceed the bound.
         * The new file has not been published to the DOM, so it is safe to
         * roll it back and leave the prior revision authoritative. */
        DeleteFileW(path);
        return false;
    }
    snprintf(
        p->chrome_browser_bitmaps[cache_slot].key,
        sizeof(p->chrome_browser_bitmaps[cache_slot].key),
        "%s",
        cache_key);
    lstrcpynW(p->chrome_browser_bitmaps[cache_slot].path, path, MAX_PATH);
    p->chrome_browser_bitmaps[cache_slot].revision = revision;
    if( cache_append )
        p->chrome_browser_bitmap_count++;
    if( cache_evict )
    {
        p->chrome_browser_bitmap_evict++;
        if( p->chrome_browser_bitmap_evict >= WIN32_BROWSER_BITMAP_CACHE_MAX )
            p->chrome_browser_bitmap_evict = 0;
    }
    memcpy(out_url, relative_url, (size_t)url_length + 1);
    return true;
}

bool
PlatformWindow_Win32GameClientSize(
    void* native_window, int* out_width, int* out_height)
{
    HWND hwnd = (HWND)native_window;
    struct PlatformWindow* p;

    if( !hwnd || !out_width || !out_height )
        return false;
    /* D3D9 can also be embedded in a probe's arbitrary HWND. Do not interpret
     * another class's application-defined GWLP_USERDATA as our structure. */
    if( (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC) != wnd_proc )
        return false;
    p = (struct PlatformWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if( !p || p->hwnd != hwnd )
        return false;
    return win32_game_client_size(p, out_width, out_height) != 0;
}


void
PlatformWindow_Win32ChromeRailSync(
    struct PlatformWindow* p,
    struct ToriRSChromeRailSnapshot const* snapshot)
{
    (void)p;
    (void)snapshot;
}

void
PlatformWindow_Win32ChromeRailIcon(
    struct PlatformWindow* p,
    struct ToriRSChromeRailIcon const* icon)
{
    (void)p;
    (void)icon;
}

int
PlatformWindow_Win32ChromeRailPoll(
    struct PlatformWindow* p,
    struct ToriRSChromeRailIntent* out,
    int max)
{
    (void)p;
    (void)out;
    (void)max;
    return 0;
}

#if defined(TORIRS_WIN32_GDI_TEST_API)
void*
PlatformWindow_Win32TestRailHandle(struct PlatformWindow* p)
{
    return p ? (void*)p->chrome_rail_hwnd : NULL;
}

int
PlatformWindow_Win32TestBitmapFileCount(struct PlatformWindow* p)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    int count = 0;

    if( !p || !p->chrome_browser_asset_dir[0] ||
        _snwprintf(
            pattern, MAX_PATH, L"%s\\bitmap\\*.bmp",
            p->chrome_browser_asset_dir) <= 0 )
        return -1;
    search = FindFirstFileW(pattern, &found);
    if( search == INVALID_HANDLE_VALUE )
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    do
    {
        if( !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
            count++;
    } while( FindNextFileW(search, &found) );
    FindClose(search);
    return count;
}

bool
PlatformWindow_Win32TestBitmapRevisionExists(
    struct PlatformWindow* p, char const* key, uint32_t revision)
{
    WCHAR wide_key[128];
    WCHAR path[MAX_PATH];

    if( !p || !p->chrome_browser_asset_dir[0] || !key || !key[0] ||
        !MultiByteToWideChar(CP_ACP, 0, key, -1, wide_key, 128) ||
        _snwprintf(
            path, MAX_PATH, L"%s\\bitmap\\%s-r%lu.bmp",
            p->chrome_browser_asset_dir, wide_key,
            (unsigned long)revision) <= 0 )
        return false;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

void*
PlatformWindow_Win32TestBitmapLock(
    struct PlatformWindow* p, char const* key, uint32_t revision)
{
    WCHAR wide_key[128];
    WCHAR path[MAX_PATH];
    HANDLE handle;

    if( !p || !p->chrome_browser_asset_dir[0] || !key || !key[0] ||
        !MultiByteToWideChar(CP_ACP, 0, key, -1, wide_key, 128) ||
        _snwprintf(
            path, MAX_PATH, L"%s\\bitmap\\%s-r%lu.bmp",
            p->chrome_browser_asset_dir, wide_key,
            (unsigned long)revision) <= 0 )
        return NULL;
    handle = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    return handle != INVALID_HANDLE_VALUE ? (void*)handle : NULL;
}
#endif

void
PlatformWindow_Free(struct PlatformWindow* p)
{
    if( !p )
        return;
    if( p->chrome_open )
        PlatformWindow_ChromeClose(p);
    if( p->chrome_browser )
        PlatformWin32Browser_Free(p->chrome_browser);
    p->chrome_browser = NULL;
    win32_browser_remove_bundle(p);
    if( p->chrome_rail_hwnd )
        DestroyWindow(p->chrome_rail_hwnd);
    p->chrome_rail_hwnd = NULL;
    if( p->mem_dc )
    {
        if( p->old_bmp )
            SelectObject(p->mem_dc, p->old_bmp);
        DeleteDC(p->mem_dc);
    }
    if( p->dib )
        DeleteObject(p->dib);
    /* Before DestroyWindow, which frees the CS_OWNDC handle itself. */
    if( p->window_dc && p->hwnd )
        ReleaseDC(p->hwnd, p->window_dc);
    p->window_dc = NULL;
    if( p->hwnd )
        DestroyWindow(p->hwnd);
    if( p->timing_active )
        PlatformWin32Timing_Shutdown();
    free(p);
}

int*
PlatformWindow_Pixels(struct PlatformWindow* p)
{
    return p->pixels;
}

int
PlatformWindow_Width(struct PlatformWindow* p)
{
    return p->width;
}

int
PlatformWindow_Height(struct PlatformWindow* p)
{
    return p->height;
}

/*
 * Drawable pixels per window point.
 *
 * 1, and honestly so: this backend creates its window and its DIB in the same
 * unit, so there is no second coordinate space here to differ from. The chrome
 * asks every backend this question and gets a truthful answer; a Windows
 * per-monitor-DPI story would be told here, by making the DIB follow the
 * monitor's scale, rather than by inventing a factor at this accessor.
 */
int
PlatformWindow_PixelDensity(struct PlatformWindow* p)
{
    (void)p;
    return 1;
}

void
PlatformWindow_SetWantHighDPI(bool want)
{
    /* Win32 GDI exposes one pixel coordinate space to this backend.  A future
     * per-monitor-DPI implementation belongs with PixelDensity above; until
     * then the cross-platform pre-init preference is intentionally a no-op. */
    (void)want;
}

bool
PlatformWindow_QuitRequested(struct PlatformWindow* p)
{
    return p->quit != 0;
}

void
PlatformWindow_SetTitle(struct PlatformWindow* p, char const* title)
{
    if( p->hwnd && title )
        SetWindowTextA(p->hwnd, title);
}

/*
 * No keyboard to raise.
 *
 * This lane is a Win32 desktop window and its keyboard is the physical one --
 * there is nothing to show and nothing to put away. The entry point exists so
 * that a plugin asking for a keyboard is answered the same way on every lane:
 * on the backends that HAVE a soft keyboard (SDL2 on Android and iOS, and
 * emscripten in a mobile browser) the SDL implementation raises it, and here it
 * is a no-op rather than a link error.
 */
void
PlatformWindow_SetTextInput(struct PlatformWindow* p, int on)
{
    (void)p;
    (void)on;
}

void
PlatformWindow_SetTouchViewport(struct PlatformWindow* p, int x, int y, int w, int h)
{
    assert(p);
    ToriRS_TouchSetViewport(&p->touch, x, y, w, h);
}

void
PlatformWindow_SetTouchOverlayTest(struct PlatformWindow* p, ToriRS_TouchOverlayFn fn, void* user)
{
    assert(p);
    ToriRS_TouchSetOverlayTest(&p->touch, fn, user);
}

void
PlatformWindow_SetCanvasFollowsWindow(
    struct PlatformWindow* p, struct ToriRS_CmdBus* bus, bool follow, int min_w, int min_h)
{
    RECT rc;
    int win_w = 0;
    int win_h = 0;
    int was_following;

    assert(p);
    was_following = p->canvas_follows_window;
    p->canvas_follows_window = follow ? 1 : 0;
    if( !p->hwnd )
        return;

    GetClientRect(p->hwnd, &rc);
    if( !win32_game_client_size(p, &win_w, &win_h) )
    {
        win_w = rc.right - rc.left;
        win_h = rc.bottom - rc.top;
    }

    if( !follow )
    {
        if( was_following && win_w > min_w && win_h > min_h )
        {
            p->resizable_w = win_w;
            p->resizable_h = win_h;
        }
        if( min_w > 0 && min_h > 0 )
            PlatformWindow_SetWindowSize(
                p, min_w + win32_chrome_reserved_width(p), min_h);
        return;
    }

    if( !was_following && p->resizable_w > 0 && p->resizable_h > 0 )
    {
        PlatformWindow_SetWindowSize(
            p,
            p->resizable_w + win32_chrome_reserved_width(p),
            p->resizable_h);
        p->resizable_w = 0;
        p->resizable_h = 0;
        GetClientRect(p->hwnd, &rc);
        if( !win32_game_client_size(p, &win_w, &win_h) )
        {
            win_w = rc.right - rc.left;
            win_h = rc.bottom - rc.top;
        }
    }
    if( bus && win_w > 0 && win_h > 0 )
        CmdBus_PushWindowResize(bus, win_w, win_h);
}

void
PlatformWindow_SetWindowSize(struct PlatformWindow* p, int width, int height)
{
    if( !p->hwnd || width <= 0 || height <= 0 )
        return;
    /* Grow the outer window so the *client* area becomes width x height. */
    win32_resize_client(p, width, height);
}

bool
PlatformWindow_Resize(struct PlatformWindow* p, int width, int height)
{
    assert(p);
    if( width <= 0 || height <= 0 )
        return false;
    if( width == p->width && height == p->height )
        return false;
    return gdi_make_dib(p, width, height) != 0;
}

void
PlatformWindow_MapMouse(struct PlatformWindow* p, int win_x, int win_y, int* out_x, int* out_y)
{
    map_mouse(p, win_x, win_y, out_x, out_y);
}

void
PlatformWindow_PollCommands(struct PlatformWindow* p, struct ToriRS_CmdBus* bus)
{
    MSG msg;
    assert(p);
    assert(bus);

    p->poll_bus = bus;
    p->pending_resize_w = -1;
    p->pending_resize_h = -1;

    /* A finger held perfectly still sends no further WM_TOUCH, so the long
     * press is given its chance to become a right click from out here. */
    ToriRS_TouchTick(&p->touch, bus, (uint64_t)GetTickCount());

    while( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
    {
        if( msg.message == WM_QUIT )
        {
            p->quit = 1;
            continue;
        }
        if( p->chrome_browser && p->chrome_rail_hwnd )
        {
            HWND const focus = GetFocus();
            if( (focus == p->chrome_rail_hwnd ||
                 (focus && IsChild(p->chrome_rail_hwnd, focus))) &&
                PlatformWin32Browser_PreTranslateMessage(
                    p->chrome_browser, &msg) )
                continue;
        }
        TranslateMessage(&msg); /* WM_KEYDOWN -> WM_CHAR for typed characters */
        DispatchMessage(&msg);  /* routed to wnd_proc, which pushes to the bus */
    }

    /* One coalesced resize per poll, after the input above -- a click seen at
     * the old size is applied at the old size, matching the SDL backend. */
    if( p->pending_resize_w > 0 && p->pending_resize_h > 0 )
        CmdBus_PushWindowResize(bus, p->pending_resize_w, p->pending_resize_h);
    p->poll_bus = NULL;
}

void
PlatformWindow_SetPresentDamage(
    struct PlatformWindow* p,
    int x,
    int y,
    int w,
    int h)
{
    assert(p);

    if( w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > p->width ||
        y + h > p->height )
    {
        /* Out of range is treated as "present everything" rather than
         * asserted: a caller's damage box is a claim about what it drew, and
         * the safe response to a claim this code cannot honour is to copy more
         * than asked, never less. */
        p->present_dmg_w = 0;
        p->present_dmg_h = 0;
        p->present_dmg_rect_count = 0;
        return;
    }
    p->present_dmg_x = x;
    p->present_dmg_y = y;
    p->present_dmg_w = w;
    p->present_dmg_h = h;
    p->present_dmg_rect_count = 0;
}

void
PlatformWindow_SetPresentDamageRects(
    struct PlatformWindow* p,
    int const (*rects)[4],
    int count)
{
    assert(p);
    assert(rects);
    assert(count > 0);

    /* Only refines a box that is already set; the box is what bounds the
     * region the renderer promised to have written. */
    if( p->present_dmg_w <= 0 || p->present_dmg_h <= 0 )
        return;
    if( count > PLATFORM_PRESENT_DAMAGE_RECT_MAX )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( rects[i][2] <= 0 || rects[i][3] <= 0 ||
            rects[i][0] < p->present_dmg_x || rects[i][1] < p->present_dmg_y ||
            rects[i][0] + rects[i][2] > p->present_dmg_x + p->present_dmg_w ||
            rects[i][1] + rects[i][3] > p->present_dmg_y + p->present_dmg_h )
            return; /* not inside the box -- present the box instead */
        p->present_dmg_rects[i][0] = rects[i][0];
        p->present_dmg_rects[i][1] = rects[i][1];
        p->present_dmg_rects[i][2] = rects[i][2];
        p->present_dmg_rects[i][3] = rects[i][3];
    }
    p->present_dmg_rect_count = count;
}

bool
PlatformWindow_CanPresent(struct PlatformWindow const* p)
{
    assert(p);
    return p->hwnd != NULL;
}

void
PlatformWindow_Present(struct PlatformWindow* p)
{
    assert(p);
    assert(p->pixels);
    if( !p->hwnd )
        return;
    assert(p->window_dc);

    /* App_Render has returned, so WM_PAINT may now reuse this DIB. This flag
     * stays clear for D3D9, preventing repair paints from covering its surface
     * with the software buffer that renderer never populated. */
    p->gdi_frame_valid = 1;

    gdi_paint_latest(p, p->window_dc);
}

void
PlatformWindow_PresentGL(struct PlatformWindow* p)
{
    (void)p; /* no GL in this backend */
}

uint64_t
PlatformWindow_Ticks64(void)
{
    return PlatformWin32Timing_NowMs();
}

uint64_t
PlatformWindow_TicksUs(void)
{
    return PlatformWin32Timing_NowUs();
}

void
PlatformWindow_SleepUntil(uint64_t deadline_ms)
{
    PlatformWin32Timing_SleepUntilMs(deadline_ms);
}

#if defined(TORIRS_WIN32_GDI_TEST_API)
uint32_t
PlatformWindow_Win32TestPaintCount(struct PlatformWindow* p)
{
    return p ? p->paint_count : 0;
}
#endif
