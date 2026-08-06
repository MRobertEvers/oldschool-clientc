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

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"

#include <windows.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef GET_X_LPARAM
#  define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#  define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static const char RPD_WNDCLASS[] = "TorirsWin32GdiWindow";

struct PlatformSDL2
{
    HWND    hwnd;
    HDC     mem_dc;          /* memory DC holding the DIB                     */
    HBITMAP dib;             /* top-down 32bpp DIB section                    */
    HBITMAP old_bmp;         /* default bitmap displaced from mem_dc          */
    int*    pixels;          /* DIB bits; App_Render writes here (ARGB8888)   */
    int     width;
    int     height;

    int     quit;
    int     esc_quits;       /* TORIRS_ESC_QUIT: ESC closes the window        */
    int     canvas_follows_window;
    int     resizable_w;
    int     resizable_h;

    /* Set only for the duration of PollCommands so the WndProc can translate
     * into the caller's bus; the coalesced resize is applied after the pump. */
    struct ToriRS_CmdBus* poll_bus;
    int     pending_resize_w;
    int     pending_resize_h;
    int     pending_repeat;
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
        if( !p->dib )
            p->old_bmp = prev; /* first bind: remember the DC's default bitmap */
        else
            DeleteObject(prev); /* resize: prev is our previous DIB */
    }
    p->dib = dib;
    p->pixels = (int*)bits;
    p->width = width;
    p->height = height;
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

    case WM_SIZE:
        if( p && p->canvas_follows_window && wparam != SIZE_MINIMIZED )
        {
            p->pending_resize_w = LOWORD(lparam);
            p->pending_resize_h = HIWORD(lparam);
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
    if( !p )
        return NULL;
    memset(p, 0, sizeof(*p));
    p->pending_resize_w = -1;
    p->pending_resize_h = -1;
    return p;
}

static int
register_class(void)
{
    static int registered = 0;
    WNDCLASSA wc;
    if( registered )
        return 1;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
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

    assert(p);
    assert(width > 0 && height > 0);

    p->esc_quits = getenv("TORIRS_ESC_QUIT") != NULL;

    if( !register_class() )
        return false;

    r.left = 0;
    r.top = 0;
    r.right = width;
    r.bottom = height;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    p->hwnd = CreateWindowExA(
        0, RPD_WNDCLASS, title ? title : "torirs",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);
    if( !p->hwnd )
        return false;
    SetWindowLongPtr(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);

    /* A DC compatible with the screen; the DIB lives selected into it. */
    screen = GetDC(p->hwnd);
    p->mem_dc = CreateCompatibleDC(screen);
    ReleaseDC(p->hwnd, screen);
    if( !p->mem_dc )
    {
        DestroyWindow(p->hwnd);
        p->hwnd = NULL;
        return false;
    }
    if( !gdi_make_dib(p, width, height) )
    {
        DeleteDC(p->mem_dc);
        p->mem_dc = NULL;
        DestroyWindow(p->hwnd);
        p->hwnd = NULL;
        return false;
    }

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

struct SDL_Window*
PlatformSDL2_Window(struct PlatformSDL2* p)
{
    (void)p;
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
    if( p->hwnd )
        DestroyWindow(p->hwnd);
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
PlatformSDL2_Present(struct PlatformSDL2* p)
{
    HDC dc;
    RECT client;
    RECT box;
    int win_w;
    int win_h;

    assert(p);
    assert(p->pixels);
    if( !p->hwnd )
        return;

    GetClientRect(p->hwnd, &client);
    win_w = client.right - client.left;
    win_h = client.bottom - client.top;
    letterbox_dst(p->width, p->height, win_w, win_h, &box);

    dc = GetDC(p->hwnd);
    if( !dc )
        return;

    /* Black bars only when the image does not fill the window. */
    if( box.left != 0 || box.top != 0 || box.right != win_w || box.bottom != win_h )
    {
        RECT full;
        full.left = 0;
        full.top = 0;
        full.right = win_w;
        full.bottom = win_h;
        FillRect(dc, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    if( box.right == p->width && box.bottom == p->height )
    {
        BitBlt(dc, box.left, box.top, p->width, p->height, p->mem_dc, 0, 0, SRCCOPY);
    }
    else
    {
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(
            dc, box.left, box.top, box.right, box.bottom,
            p->mem_dc, 0, 0, p->width, p->height, SRCCOPY);
    }
    ReleaseDC(p->hwnd, dc);
}

void
PlatformSDL2_PresentGL(struct PlatformSDL2* p)
{
    (void)p; /* no GL in this backend */
}

uint64_t
PlatformSDL2_Ticks64(void)
{
    /* GetTickCount is XP-safe (GetTickCount64 is Vista+). It wraps after ~49
     * days of uptime, which no benchmark run will span. */
    return (uint64_t)GetTickCount();
}

void
PlatformSDL2_Delay(uint32_t ms)
{
    Sleep(ms);
}
