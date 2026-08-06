#include "platform/platform_sdl2.h"

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
PlatformSDL2_Win32TestPaintCount(struct PlatformSDL2* platform);

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
    uint32_t const sentinel = UINT32_C(0x00234567);
    struct PlatformSDL2* platform;
    HWND hwnd;
    struct TestSurface target;
    RECT client;
    int* source;
    LRESULT erased;
    uint32_t paints_before;

    SetEnvironmentVariableA("TORIRS_WIN32_HIDDEN", "1");
    platform = PlatformSDL2_New();
    if( !platform || !PlatformSDL2_Init(platform, logical_w, logical_h, "torirs-gdi-test") )
        fail("platform initialization failed");
    hwnd = (HWND)PlatformSDL2_NativeWindowHandle(platform);
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

    source = PlatformSDL2_Pixels(platform);
    for( int y = 0; y < logical_h; y++ )
        for( int x = 0; x < logical_w; x++ )
            source[y * logical_w + x] = (int)pattern_at(x, y);
    PlatformSDL2_Present(platform);

    surface_fill(&target, sentinel);
    SendMessageA(hwnd, WM_PRINTCLIENT, (WPARAM)target.dc, PRF_CLIENT);
    for( int y = 0; y < logical_h; y++ )
        for( int x = 0; x < logical_w; x++ )
            if( (target.pixels[y * logical_w + x] & UINT32_C(0x00ffffff)) != pattern_at(x, y) )
                fail("1:1 repair paint did not reproduce the retained DIB");

    paints_before = PlatformSDL2_Win32TestPaintCount(platform);
    InvalidateRect(hwnd, NULL, TRUE);
    SendMessageA(hwnd, WM_PAINT, 0, 0);
    if( PlatformSDL2_Win32TestPaintCount(platform) <= paints_before )
        fail("invalidated client did not pass through WM_PAINT");

    /* A 2x-wide client letterboxes the unscaled image between two black bars.
     * This proves bars are isolated from the image instead of a full black
     * clear becoming visible before the image blit. */
    PlatformSDL2_SetWindowSize(platform, logical_w * 2, logical_h);
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
    if( !PlatformSDL2_Resize(platform, logical_w + 1, logical_h + 1) ||
        PlatformSDL2_Width(platform) != logical_w + 1 ||
        PlatformSDL2_Height(platform) != logical_h + 1 ||
        !PlatformSDL2_Pixels(platform) )
        fail("first retained-DIB resize failed");
    if( !PlatformSDL2_Resize(platform, logical_w, logical_h) ||
        PlatformSDL2_Width(platform) != logical_w ||
        PlatformSDL2_Height(platform) != logical_h ||
        !PlatformSDL2_Pixels(platform) )
        fail("second retained-DIB resize failed");

    surface_free(&target);
    PlatformSDL2_Free(platform);
    SetEnvironmentVariableA("TORIRS_WIN32_HIDDEN", NULL);
    puts("win32_gdi_test: ok (retained paint, no erase, isolated letterbox bars)");
    return 0;
}
