#include "platform/platform_window.h"
#include "platform/platform_win32_chrome.h"

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WM_PRINTCLIENT
#  define WM_PRINTCLIENT 0x0318
#endif
#ifndef PRF_CLIENT
#  define PRF_CLIENT 0x00000004L
#endif

extern uint32_t
PlatformWindow_Win32TestPaintCount(struct PlatformWindow* platform);

struct TestSurface
{
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    uint32_t* pixels;
    int width;
    int height;
};

static void
fail(char const* message)
{
    fprintf(stderr, "win32_gdi_test: %s\n", message);
    exit(1);
}

static struct TestSurface
surface_new(int width, int height)
{
    struct TestSurface surface;
    BITMAPINFO info;
    void* bits = NULL;

    memset(&surface, 0, sizeof(surface));
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    surface.dc = CreateCompatibleDC(NULL);
    surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if( !surface.dc || !surface.bitmap || !bits )
        fail("could not create target DIB");
    surface.old_bitmap = (HBITMAP)SelectObject(surface.dc, surface.bitmap);
    surface.pixels = (uint32_t*)bits;
    surface.width = width;
    surface.height = height;
    return surface;
}

static void
surface_free(struct TestSurface* surface)
{
    if( surface->dc && surface->old_bitmap )
        SelectObject(surface->dc, surface->old_bitmap);
    if( surface->bitmap )
        DeleteObject(surface->bitmap);
    if( surface->dc )
        DeleteDC(surface->dc);
    memset(surface, 0, sizeof(*surface));
}

static void
surface_fill(struct TestSurface* surface, uint32_t color)
{
    size_t count = (size_t)surface->width * (size_t)surface->height;
    for( size_t i = 0; i < count; i++ )
        surface->pixels[i] = color;
}

static uint32_t
pattern_at(int x, int y)
{
    return UINT32_C(0x00010000) | (uint32_t)(x + 1) << 8 | (uint32_t)(y + 1);
}

static void
assert_surface_color(struct TestSurface const* surface, uint32_t expected, char const* message)
{
    size_t count = (size_t)surface->width * (size_t)surface->height;
    for( size_t i = 0; i < count; i++ )
        if( (surface->pixels[i] & UINT32_C(0x00ffffff)) != expected )
            fail(message);
}

int
main(void)
{
    /* Stay above USER32's minimum tracked top-level window dimensions so the
     * requested client size is not silently widened during CreateWindow. */
    int const logical_w = 160;
    int const logical_h = 80;
#if defined(_WIN64)
    int const rail_w = 48;
#else
    int const rail_w = 46;
#endif
    uint32_t const sentinel = UINT32_C(0x00234567);
    struct PlatformWindow* platform;
    HWND hwnd;
    struct TestSurface target;
    RECT client;
    int* source;
    LRESULT erased;
    uint32_t paints_before;
    HWND chrome;
    HWND rail;
    HWND browser_child;
    int game_w;
    int game_h;
    uint32_t icon_pixels[4] = {
        UINT32_C(0xff000000), UINT32_C(0xffffffff),
        UINT32_C(0xffff981f), UINT32_C(0xff372e22)
    };
    char bitmap_url[128];

    SetEnvironmentVariableA("TORIRS_WIN32_HIDDEN", "1");
    platform = PlatformWindow_New();
    if( !platform || !PlatformWindow_Init(platform, logical_w, logical_h, "torirs-gdi-test") )
        fail("platform initialization failed");
    hwnd = (HWND)PlatformWindow_NativeWindowHandle(platform);
    if( !hwnd )
        fail("platform returned no HWND");
    if( GetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND) != 0 )
        fail("window class still owns an erase brush");
    if( GetClassLongPtrA(hwnd, GCL_STYLE) & (CS_HREDRAW | CS_VREDRAW) )
        fail("window class still requests whole-client redraws");

    target = surface_new(logical_w, logical_h);

    /* Before Soft3D presents, paint/print messages must leave the target alone.
     * The same HWND belongs to D3D9, whose surface an empty GDI DIB must never
     * cover. */
    surface_fill(&target, sentinel);
    SendMessageA(hwnd, WM_PRINTCLIENT, (WPARAM)target.dc, PRF_CLIENT);
    assert_surface_color(&target, sentinel, "pre-Soft3D print modified the target");

    surface_fill(&target, sentinel);
    erased = SendMessageA(hwnd, WM_ERASEBKGND, (WPARAM)target.dc, 0);
    if( erased != 1 )
        fail("WM_ERASEBKGND was not suppressed");
    assert_surface_color(&target, sentinel, "background erase touched client pixels");

    source = PlatformWindow_Pixels(platform);
    for( int y = 0; y < logical_h; y++ )
        for( int x = 0; x < logical_w; x++ )
            source[y * logical_w + x] = (int)pattern_at(x, y);
    PlatformWindow_Present(platform);

    surface_fill(&target, sentinel);
    SendMessageA(hwnd, WM_PRINTCLIENT, (WPARAM)target.dc, PRF_CLIENT);
    for( int y = 0; y < logical_h; y++ )
        for( int x = 0; x < logical_w; x++ )
            if( (target.pixels[y * logical_w + x] & UINT32_C(0x00ffffff)) != pattern_at(x, y) )
                fail("1:1 repair paint did not reproduce the retained DIB");

    paints_before = PlatformWindow_Win32TestPaintCount(platform);
    InvalidateRect(hwnd, NULL, TRUE);
    SendMessageA(hwnd, WM_PAINT, 0, 0);
    if( PlatformWindow_Win32TestPaintCount(platform) <= paints_before )
        fail("invalidated client did not pass through WM_PAINT");

    /* A 2x-wide client letterboxes the unscaled image between two black bars.
     * This proves bars are isolated from the image instead of a full black
     * clear becoming visible before the image blit. */
    PlatformWindow_SetWindowSize(platform, logical_w * 2, logical_h);
    GetClientRect(hwnd, &client);
    if( client.right != logical_w * 2 || client.bottom != logical_h )
        fail("SetWindowSize did not produce the requested client size");
    surface_free(&target);
    target = surface_new(client.right, client.bottom);
    surface_fill(&target, sentinel);
    SendMessageA(hwnd, WM_PRINTCLIENT, (WPARAM)target.dc, PRF_CLIENT);
    for( int y = 0; y < logical_h; y++ )
    {
        for( int x = 0; x < logical_w * 2; x++ )
        {
            uint32_t actual = target.pixels[y * logical_w * 2 + x] & UINT32_C(0x00ffffff);
            if( x < logical_w / 2 || x >= logical_w + logical_w / 2 )
            {
                if( actual != 0 )
                    fail("letterbox bar was not black");
            }
            else if( actual != pattern_at(x - logical_w / 2, y) )
            {
                fail("letterboxed image was cleared or shifted");
            }
        }
    }

    /* Exercise transactional bitmap replacement and the cleanup path more
     * than once; a failed SelectObject must not publish dangling DIB state. */
    if( !PlatformWindow_Resize(platform, logical_w + 1, logical_h + 1) ||
        PlatformWindow_Width(platform) != logical_w + 1 ||
        PlatformWindow_Height(platform) != logical_h + 1 ||
        !PlatformWindow_Pixels(platform) )
        fail("first retained-DIB resize failed");
    if( !PlatformWindow_Resize(platform, logical_w, logical_h) ||
        PlatformWindow_Width(platform) != logical_w ||
        PlatformWindow_Height(platform) != logical_h ||
        !PlatformWindow_Pixels(platform) )
        fail("second retained-DIB resize failed");

    /* The plugin shell is part of this same top-level HWND.  Restore its
     * original size first: the letterbox case above deliberately doubled it. */
    PlatformWindow_SetWindowSize(platform, logical_w, logical_h);
    GetClientRect(hwnd, &client);
    if( client.right != logical_w || client.bottom != logical_h )
        fail("could not restore main client before chrome test");
    if( !PlatformWindow_ChromeRailOpen(platform, rail_w, "Plugins") )
        fail("could not create persistent browser rail");
    chrome = (HWND)PlatformWindow_Win32ChromeHandle(platform);
    rail = (HWND)PlatformWindow_Win32TestRailHandle(platform);
    if( !chrome || chrome != rail || chrome == hwnd )
        fail("plugin chrome did not expose its one persistent browser container");
    if( GetParent(chrome) != hwnd ||
        !(GetWindowLongPtrA(chrome, GWL_STYLE) & WS_CHILD) ||
        (GetWindowLongPtrA(chrome, GWL_STYLE) & WS_POPUP) )
        fail("plugin browser container escaped the main HWND");
    browser_child = GetWindow(chrome, GW_CHILD);
    if( !browser_child || GetParent(browser_child) != chrome )
        fail("browser backend did not attach inside the persistent container");
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w || client.bottom != logical_h ||
        !PlatformWindow_Win32GameClientSize(hwnd, &game_w, &game_h) ||
        game_w != logical_w || game_h != logical_h ||
        PlatformWindow_ChromeRailWidth(platform) != rail_w ||
        PlatformWindow_ChromePageWidth(platform) != 0 )
        fail("collapsed browser rail changed the game allocation");
    if( !PlatformWindow_PluginBrowserReady(platform) ||
        PlatformWindow_PluginBrowserFailed(platform) )
        fail("deterministic browser backend did not become ready");
    GetClientRect(chrome, &client);
    if( client.right != rail_w || client.bottom != logical_h )
        fail("collapsed browser was not exactly rail width");
    if( !PlatformWindow_PluginBrowserBitmapUrl(
            platform, "rail-4", 9, icon_pixels, 2, 2,
            bitmap_url, (int)sizeof(bitmap_url)) ||
        strcmp(bitmap_url, "bitmap/rail-4-r9.bmp") != 0 )
        fail("browser bitmap cache did not publish a relative revision URL");
    if( PlatformWindow_PluginBrowserBitmapUrl(
            platform, "../escape", 9, icon_pixels, 2, 2,
            bitmap_url, (int)sizeof(bitmap_url)) )
        fail("browser bitmap cache accepted a path traversal key");

    if( !PlatformWindow_ChromeOpen(platform, 60, logical_h, "Plugins") )
        fail("could not open attached plugin chrome");
    chrome = (HWND)PlatformWindow_Win32ChromeHandle(platform);
    rail = (HWND)PlatformWindow_Win32TestRailHandle(platform);
    if( !chrome || chrome != rail || GetParent(chrome) != hwnd )
        fail("expand replaced the persistent browser container");
    if( GetWindow(chrome, GW_CHILD) != browser_child )
        fail("expand replaced the one shared browser control");
    if( !PlatformWindow_Win32GameClientSize(hwnd, &game_w, &game_h) ||
        game_w != logical_w || game_h != logical_h )
        fail("attached-grow changed the game presentation size");
    if( PlatformWindow_ChromeWidth(platform) != 60 ||
        PlatformWindow_ChromeHeight(platform) != logical_h ||
        PlatformWindow_ChromeRailWidth(platform) != rail_w ||
        PlatformWindow_ChromePageWidth(platform) != 60 )
        fail("attached pane reported the wrong drawable size");
    if( PlatformWindow_ChromePixels(platform) != NULL )
        fail("browser-backed chrome unexpectedly exposed a surface buffer");
    GetClientRect(chrome, &client);
    if( client.right != rail_w + 60 || client.bottom != logical_h )
        fail("expanded browser did not contain rail plus selected page");
    PlatformWindow_ChromePresent(platform);

    PlatformWindow_ChromeClose(platform);
    if( PlatformWindow_ChromeIsOpen(platform) ||
        PlatformWindow_Win32ChromeHandle(platform) != chrome ||
        PlatformWindow_Win32TestRailHandle(platform) != rail )
        fail("collapse did not preserve the shared browser control");
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w || client.bottom != logical_h ||
        !PlatformWindow_Win32GameClientSize(hwnd, &game_w, &game_h) ||
        game_w != logical_w || game_h != logical_h ||
        PlatformWindow_ChromePageWidth(platform) != 0 )
        fail("collapse failed to preserve the rail and game width");

    /* A second cycle must land on the exact same expanded and collapsed
     * widths. This catches the classic frame-vs-client one-pixel ratchet. */
    if( !PlatformWindow_ChromeOpen(platform, 60, logical_h, "Plugins") )
        fail("could not reopen attached plugin chrome");
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w + 60 )
        fail("reopen accumulated width drift");
    if( !PlatformWindow_ChromeOpen(platform, 60, logical_h, "Plugins") )
        fail("idempotent open was refused");
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w + 60 )
        fail("idempotent open ratcheted the window wider");

    /* A user/window-manager shrink while expanded must not make the next
     * collapsed game smaller than the size from which this cycle opened. */
    PlatformWindow_SetWindowSize(platform, logical_w + rail_w + 30, logical_h);
    PlatformWindow_ChromeClose(platform);
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w )
        fail("collapse after an external resize lost the saved game width");
    if( !PlatformWindow_ChromeOpen(platform, 60, logical_h, "Plugins") )
        fail("could not expand after the clamped collapse");
    PlatformWindow_ChromeClose(platform);
    GetClientRect(hwnd, &client);
    if( client.right != logical_w + rail_w )
        fail("third collapse accumulated width drift");

    surface_free(&target);
    PlatformWindow_Free(platform);
    SetEnvironmentVariableA("TORIRS_WIN32_HIDDEN", NULL);
    puts("win32_gdi_test: ok (retained game paint, one attached browser, stable collapse)");
    return 0;
}
