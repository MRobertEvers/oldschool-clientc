/*
 * platform_win32gdi.c -- Win32 + GDI backend for the src/main.c App front-end.
 *
 * src/main.c programs to the PlatformSDL2 interface (platform_sdl2.h) and, when
 * built without a GPU renderer, uses the software path:
 *
 *     App_Render(app, PlatformSDL2_Pixels(sdl), W, H);   // CPU raster into pixels
 *     PlatformSDL2_Present(sdl);                          // show the pixels
 *
 * That is exactly what GDI does well: hand out a top-down 32bpp DIB section as
 * the pixel buffer, then BitBlt/StretchBlt it to the window. This file is a
 * drop-in implementation of the SAME PlatformSDL2_* symbols backed by Win32 GDI
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

#include "platform/platform_sdl2.h"
#include "platform/platform_app_icon.h"
#include "platform/platform_win32_timing.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "input/torirs_touch.h"
#include "perf/torirs_perf.h"

#include <windows.h>

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
#include <stdlib.h>
#include <string.h>

#ifndef GET_X_LPARAM
#  define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#  define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif
#ifndef WM_PRINTCLIENT
#  define WM_PRINTCLIENT 0x0318
#endif

static const char RPD_WNDCLASS[] = "TorirsWin32GdiWindow";

struct PlatformSDL2
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
     * @see PlatformSDL2_SetPresentDamage. */
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

/* ---- pixel buffer ------------------------------------------------------- */

static int
gdi_make_dib(struct PlatformSDL2* p, int width, int height)
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
map_mouse(struct PlatformSDL2* p, int win_x, int win_y, int* out_x, int* out_y)
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
    win_w = client.right - client.left;
    win_h = client.bottom - client.top;
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
gdi_paint_latest(struct PlatformSDL2* p, HDC dc)
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
    win_w = client.right - client.left;
    win_h = client.bottom - client.top;
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
    struct PlatformSDL2* p =
        (struct PlatformSDL2*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
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

                    /* Screen space, in HUNDREDTHS of a pixel, so it is divided
                     * down before being made client-relative. */
                    pt.x = inputs[i].x / 100;
                    pt.y = inputs[i].y / 100;
                    ScreenToClient(hwnd, &pt);
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
            if( p->gdi_frame_valid )
                InvalidateRect(hwnd, NULL, FALSE);
            if( p->canvas_follows_window )
            {
                p->pending_resize_w = LOWORD(lparam);
                p->pending_resize_h = HIWORD(lparam);
            }
        }
        break;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

/* ---- lifecycle ---------------------------------------------------------- */

struct PlatformSDL2*
PlatformSDL2_New(void)
{
    struct PlatformSDL2* p = (struct PlatformSDL2*)malloc(sizeof(*p));
    assert(p);
    memset(p, 0, sizeof(*p));
    p->pending_resize_w = -1;
    p->pending_resize_h = -1;
    p->interface_scale_mode = 2;
    return p;
}

void
PlatformSDL2_SetInterfaceScaleMode(struct PlatformSDL2* p, int mode)
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
    if( !RegisterClassA(&wc) )
        return 0;
    registered = 1;
    return 1;
}

bool
PlatformSDL2_Init(struct PlatformSDL2* p, int width, int height, char const* title)
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
    style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;

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
PlatformSDL2_InitForOpenGL3(struct PlatformSDL2* p, int width, int height, char const* title)
{
    (void)p;
    (void)width;
    (void)height;
    (void)title;
    return false;
}

ToriRS_GLWindow*
PlatformSDL2_Window(struct PlatformSDL2* p)
{
    (void)p;
    /* No GL renderer on this lane -- it selects fixed-function D3D9, which
     * consumes the HWND below rather than a GL window. */
    return NULL;
}

void*
PlatformSDL2_NativeWindowHandle(struct PlatformSDL2* p)
{
    return p ? (void*)p->hwnd : NULL;
}

void
PlatformSDL2_Free(struct PlatformSDL2* p)
{
    if( !p )
        return;
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
PlatformSDL2_Pixels(struct PlatformSDL2* p)
{
    return p->pixels;
}

int
PlatformSDL2_Width(struct PlatformSDL2* p)
{
    return p->width;
}

int
PlatformSDL2_Height(struct PlatformSDL2* p)
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
PlatformSDL2_PixelDensity(struct PlatformSDL2* p)
{
    (void)p;
    return 1;
}

void
PlatformSDL2_SetWantHighDPI(bool want)
{
    /* Win32 GDI exposes one pixel coordinate space to this backend.  A future
     * per-monitor-DPI implementation belongs with PixelDensity above; until
     * then the cross-platform pre-init preference is intentionally a no-op. */
    (void)want;
}

bool
PlatformSDL2_QuitRequested(struct PlatformSDL2* p)
{
    return p->quit != 0;
}

void
PlatformSDL2_SetTitle(struct PlatformSDL2* p, char const* title)
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
PlatformSDL2_SetTextInput(struct PlatformSDL2* p, int on)
{
    (void)p;
    (void)on;
}

void
PlatformSDL2_SetTouchViewport(struct PlatformSDL2* p, int x, int y, int w, int h)
{
    assert(p);
    ToriRS_TouchSetViewport(&p->touch, x, y, w, h);
}

void
PlatformSDL2_SetCanvasFollowsWindow(
    struct PlatformSDL2* p, struct ToriRS_CmdBus* bus, bool follow, int min_w, int min_h)
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
    win_w = rc.right - rc.left;
    win_h = rc.bottom - rc.top;

    if( !follow )
    {
        if( was_following && win_w > min_w && win_h > min_h )
        {
            p->resizable_w = win_w;
            p->resizable_h = win_h;
        }
        if( min_w > 0 && min_h > 0 )
            PlatformSDL2_SetWindowSize(p, min_w, min_h);
        return;
    }

    if( !was_following && p->resizable_w > 0 && p->resizable_h > 0 )
    {
        PlatformSDL2_SetWindowSize(p, p->resizable_w, p->resizable_h);
        p->resizable_w = 0;
        p->resizable_h = 0;
        GetClientRect(p->hwnd, &rc);
        win_w = rc.right - rc.left;
        win_h = rc.bottom - rc.top;
    }
    if( bus && win_w > 0 && win_h > 0 )
        CmdBus_PushWindowResize(bus, win_w, win_h);
}

void
PlatformSDL2_SetWindowSize(struct PlatformSDL2* p, int width, int height)
{
    RECT r;
    if( !p->hwnd || width <= 0 || height <= 0 )
        return;
    /* Grow the outer window so the *client* area becomes width x height. */
    r.left = 0;
    r.top = 0;
    r.right = width;
    r.bottom = height;
    AdjustWindowRect(&r, (DWORD)GetWindowLongPtr(p->hwnd, GWL_STYLE), FALSE);
    SetWindowPos(
        p->hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool
PlatformSDL2_Resize(struct PlatformSDL2* p, int width, int height)
{
    assert(p);
    if( width <= 0 || height <= 0 )
        return false;
    if( width == p->width && height == p->height )
        return false;
    return gdi_make_dib(p, width, height) != 0;
}

void
PlatformSDL2_MapMouse(struct PlatformSDL2* p, int win_x, int win_y, int* out_x, int* out_y)
{
    map_mouse(p, win_x, win_y, out_x, out_y);
}

void
PlatformSDL2_PollCommands(struct PlatformSDL2* p, struct ToriRS_CmdBus* bus)
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
PlatformSDL2_SetPresentDamage(
    struct PlatformSDL2* p,
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
PlatformSDL2_SetPresentDamageRects(
    struct PlatformSDL2* p,
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

void
PlatformSDL2_Present(struct PlatformSDL2* p)
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
PlatformSDL2_PresentGL(struct PlatformSDL2* p)
{
    (void)p; /* no GL in this backend */
}

uint64_t
PlatformSDL2_Ticks64(void)
{
    return PlatformWin32Timing_NowMs();
}

uint64_t
PlatformSDL2_TicksUs(void)
{
    return PlatformWin32Timing_NowUs();
}

void
PlatformSDL2_SleepUntil(uint64_t deadline_ms)
{
    PlatformWin32Timing_SleepUntilMs(deadline_ms);
}

#if defined(TORIRS_WIN32_GDI_TEST_API)
uint32_t
PlatformSDL2_Win32TestPaintCount(struct PlatformSDL2* p)
{
    return p ? p->paint_count : 0;
}
#endif
