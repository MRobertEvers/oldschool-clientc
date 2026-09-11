/*
 * Deterministic visual/runtime harness for the production Win32 plugin chrome.
 *
 * This is intentionally not a look-alike test renderer.  It creates the real
 * PlatformWindow HWND, publishes the real persistent rail ABI, starts the real
 * browser executor on its attached child, sends the normal semantic command
 * stream, and publishes one identity-fenced CUSTOM bitmap. PrintWindow then
 * asks that exact HWND tree to paint itself into a BMP for review on Windows.
 */

#include "platform/platform_win32_chrome.h"
#include "platform/platform_window.h"
#include "ui/torirs_chrome_exec.h"

#include "cmd/cmdbus.h"

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PRF_CLIENT
#  define PRF_CLIENT 0x00000004L
#endif
#ifndef PRF_CHILDREN
#  define PRF_CHILDREN 0x00000010L
#endif
#ifndef PW_CLIENTONLY
#  define PW_CLIENTONLY 0x00000001
#endif

static void
fail(char const* message)
{
    fprintf(stderr, "win32_plugin_chrome_capture: %s\n", message);
    exit(1);
}

static void
copy_text(char* out, size_t cap, char const* text)
{
    size_t n = text ? strlen(text) : 0;

    if( n >= cap )
        n = cap - 1;
    if( n )
        memcpy(out, text, n);
    out[n] = '\0';
}

static void
apply_cmd(
    struct ToriRSChromeExec const* exec,
    int kind,
    int panel,
    int widget,
    int value,
    char const* label,
    char const* text)
{
    struct ToriRSChromeCmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = kind;
    cmd.panel = panel;
    cmd.widget = widget;
    cmd.tab = -1;
    cmd.value = value;
    copy_text(cmd.label, sizeof(cmd.label), label);
    copy_text(cmd.text, sizeof(cmd.text), text);
    exec->apply(exec->user, &cmd);
}

static void
apply_add(
    struct ToriRSChromeExec const* exec,
    int panel,
    int widget,
    int kind,
    int height,
    char const* label,
    char const* text)
{
    struct ToriRSChromeCmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = TORIRS_CHROME_CMD_WIDGET_ADD;
    cmd.panel = panel;
    cmd.widget = widget;
    cmd.tab = -1;
    cmd.value = kind;
    cmd.h = height;
    cmd.serial = (uint32_t)(1001 + widget);
    copy_text(cmd.label, sizeof(cmd.label), label);
    copy_text(cmd.text, sizeof(cmd.text), text);
    exec->apply(exec->user, &cmd);
}

static void
make_icon(
    struct ToriRSChromeRailIcon* icon,
    int plugin,
    uint32_t revision,
    uint32_t center,
    int diagonal)
{
    memset(icon, 0, sizeof(*icon));
    icon->plugin_index = plugin;
    icon->revision = revision;
    icon->width = 16;
    icon->height = 16;
    for( int y = 1; y < 15; y++ )
        for( int x = 1; x < 15; x++ )
        {
            int const edge = x == 1 || x == 14 || y == 1 || y == 14;
            int const mark = diagonal ? (x == y || x + y == 15)
                                      : (x >= 5 && x <= 10 && y >= 4 && y <= 11);
            uint32_t color = edge ? UINT32_C(0xFF0E0E0C)
                                  : UINT32_C(0xFF372E22);
            if( mark )
                color = center;
            icon->argb[y * 16 + x] = color;
        }
}

static void
publish_rail(struct ToriRSChromeExec const* exec)
{
    struct ToriRSChromeRailSnapshot snapshot;
    struct ToriRSChromeRailIcon icon;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.registry_revision = 9;
    snapshot.selection_generation = 77;
    snapshot.page_generation = 401;
    snapshot.active_plugin = 3;
    snapshot.last_selected_plugin = 3;
    snapshot.selected_entry = 3;
    snapshot.expanded = 1;
    snapshot.entry_count = 3;

    snapshot.entries[0].kind = TORIRS_CHROME_RAIL_ENTRY_MANAGE;
    snapshot.entries[0].plugin_index = -2;
    copy_text(snapshot.entries[0].title, sizeof(snapshot.entries[0].title),
              "Manage Plugins");

    snapshot.entries[1].kind = TORIRS_CHROME_RAIL_ENTRY_PLUGIN;
    snapshot.entries[1].plugin_index = 3;
    snapshot.entries[1].preferred_width = 420;
    copy_text(snapshot.entries[1].title, sizeof(snapshot.entries[1].title),
              "Ground Items");

    snapshot.entries[2].kind = TORIRS_CHROME_RAIL_ENTRY_PLUGIN;
    snapshot.entries[2].plugin_index = 7;
    snapshot.entries[2].preferred_width = 390;
    snapshot.entries[2].attention = 1;
    copy_text(snapshot.entries[2].title, sizeof(snapshot.entries[2].title),
              "Loot Tracker");
    copy_text(snapshot.entries[2].badge, sizeof(snapshot.entries[2].badge), "12");

    exec->rail_sync(exec->user, &snapshot);
    make_icon(&icon, 3, 5, UINT32_C(0xFFFF981F), 0);
    exec->rail_icon(exec->user, &icon);
    make_icon(&icon, 7, 3, UINT32_C(0xFF3FCF72), 1);
    exec->rail_icon(exec->user, &icon);
}

static void
publish_page(struct ToriRSChromeExec const* exec)
{
    apply_cmd(exec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1, 0, NULL, NULL);
    apply_cmd(exec, TORIRS_CHROME_CMD_CHECK_STYLE, -1, -1, 0, NULL, NULL);
    apply_cmd(exec, TORIRS_CHROME_CMD_PANEL_OPEN, 0, -1,
              TORIRS_CHROME_PANEL_WINDOW, NULL, "Ground Items");

    apply_add(exec, 0, 0, TORIRS_CHROME_W_LABEL, 0, NULL,
              "Modern OSRS chrome / one embedded browser");
    apply_add(exec, 0, 1, TORIRS_CHROME_W_CHECKBOX, 0,
              "Highlight valuable drops", NULL);
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_CHECKED, 0, 1, 1, NULL, NULL);

    apply_add(exec, 0, 2, TORIRS_CHROME_W_DROPDOWN, 0,
              "Minimum value", NULL);
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_OPTIONS, 0, 2, 3, NULL, NULL);
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_OPTION, 0, 2, 0, NULL, "1,000 gp");
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_OPTION, 0, 2, 1, NULL, "10,000 gp");
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_OPTION, 0, 2, 2, NULL, "100,000 gp");
    apply_cmd(exec, TORIRS_CHROME_CMD_WIDGET_SELECTED, 0, 2, 1, NULL, NULL);

    apply_add(exec, 0, 3, TORIRS_CHROME_W_CUSTOM, 64,
              "Plugin-owned custom region", NULL);
    apply_add(exec, 0, 4, TORIRS_CHROME_W_TEXTINPUT, 0,
              "Tag", "Rune platebody");
    apply_add(exec, 0, 5, TORIRS_CHROME_W_BUTTON, 0, NULL, "Save settings");
    apply_cmd(exec, TORIRS_CHROME_CMD_SYNC_END, -1, -1, 0, NULL, NULL);
}

static uint32_t*
make_custom_pixels(int width, int height, int late)
{
    uint32_t* pixels =
        (uint32_t*)malloc((size_t)width * (size_t)height * sizeof(*pixels));

    if( !pixels )
        fail("could not allocate custom frame");
    for( int y = 0; y < height; y++ )
        for( int x = 0; x < width; x++ )
        {
            uint32_t color = late ? UINT32_C(0xFFFF00FF)
                                  : UINT32_C(0xFF241F19);
            if( !late )
            {
                if( x % 32 == 0 || y % 24 == 0 )
                    color = UINT32_C(0xFF40372B);
                if( y >= 24 && y < 48 && x >= 18 && x < width - 18 )
                    color = x < width * 3 / 5 ? UINT32_C(0xFF3E8B55)
                                              : UINT32_C(0xFF191612);
                if( y >= 70 && y < 94 && x >= 18 && x < width - 18 )
                    color = x < width * 4 / 5 ? UINT32_C(0xFFFF981F)
                                              : UINT32_C(0xFF191612);
                if( x >= width - 55 && x < width - 20 && y >= 18 && y < 53 )
                    color = UINT32_C(0xFF8B2F2F);
            }
            pixels[(size_t)y * (size_t)width + (size_t)x] = color;
        }
    return pixels;
}

static void
publish_custom(struct ToriRSChromeExec const* exec)
{
    int const width = 320;
    int const height = 128;
    struct ToriRSChromeCustomFrame frame;
    uint32_t* pixels = make_custom_pixels(width, height, 0);
    uint32_t* late = make_custom_pixels(width, height, 1);

    memset(&frame, 0, sizeof(frame));
    frame.panel = 0;
    frame.widget = 3;
    frame.selection_generation = 401;
    frame.widget_serial = 1004;
    frame.scale_milli = 2000;
    frame.width = width;
    frame.height = height;
    frame.stride = width;
    frame.argb = pixels;
    exec->custom_present(exec->user, &frame);

    /* A posted frame carrying another identity must not replace the retained
     * one; the common executor rejects it before publishing another URL. */
    frame.widget_serial = 9004;
    frame.argb = late;
    exec->custom_present(exec->user, &frame);

    free(late);
    free(pixels);
}

static void
pump_until_ready(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    DWORD timeout_ms)
{
    DWORD const started = GetTickCount();

    while( !PlatformWindow_PluginBrowserReady(platform) )
    {
        if( PlatformWindow_PluginBrowserFailed(platform) )
            fail("embedded browser initialization failed");
        PlatformWindow_PollCommands(platform, bus);
        if( GetTickCount() - started >= timeout_ms )
            fail("timed out waiting for embedded browser");
        Sleep(10);
    }
    /* ExecuteScript and DOM layout are asynchronous even after navigation.
     * Keep pumping a bounded number of real UI turns before PrintWindow. */
    for( int i = 0; i < 30; i++ )
    {
        PlatformWindow_PollCommands(platform, bus);
        Sleep(10);
    }
}

static int
capture_browser_png(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    char const* path)
{
    DWORD const started = GetTickCount();
    int status;

    if( !PlatformWindow_Win32BrowserCapturePng(platform, path) )
        return 0; /* XP/MSHTML is captured with its ordinary child HWND below. */
    do
    {
        PlatformWindow_PollCommands(platform, bus);
        status = PlatformWindow_Win32BrowserCaptureStatus(platform);
        if( status > 0 ) return 1;
        if( status < 0 ) fail("WebView2 preview capture failed");
        if( GetTickCount() - started >= 15000 )
            fail("timed out waiting for WebView2 preview capture");
        Sleep(10);
    } while( 1 );
}

static int
write_client_bmp(HWND hwnd, char const* path, int* out_w, int* out_h)
{
    RECT client;
    BITMAPINFO bitmap_info;
    BITMAPFILEHEADER file_header;
    BITMAPINFOHEADER dib_header;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
    void* bits = NULL;
    FILE* file;
    size_t byte_count;
    int ok = 1;
    int width;
    int height;

    if( !GetClientRect(hwnd, &client) )
        return 0;
    width = client.right - client.left;
    height = client.bottom - client.top;
    if( width <= 0 || height <= 0 )
        return 0;
    memset(&bitmap_info, 0, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    bitmap = CreateDIBSection(
        dc, &bitmap_info, DIB_RGB_COLORS, &bits, NULL, 0);
    if( !dc || !bitmap || !bits )
    {
        if( bitmap )
            DeleteObject(bitmap);
        if( dc )
            DeleteDC(dc);
        return 0;
    }
    previous = SelectObject(dc, bitmap);
    if( !previous || previous == HGDI_ERROR )
    {
        DeleteObject(bitmap);
        DeleteDC(dc);
        return 0;
    }
    memset(bits, 0xCC, (size_t)width * (size_t)height * sizeof(uint32_t));

    RedrawWindow(
        hwnd, NULL, NULL,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    /* PrintWindow captures the production main/game DIB deterministically.
     * WebView2's compositor region is then replaced with CapturePreview's
     * production pixels by the make target. */
    if( !PrintWindow(hwnd, dc, PW_CLIENTONLY) )
        SendMessageA(hwnd, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_CHILDREN);

    byte_count = (size_t)width * (size_t)height * sizeof(uint32_t);
    memset(&file_header, 0, sizeof(file_header));
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(dib_header);
    file_header.bfSize = (DWORD)(file_header.bfOffBits + byte_count);
    dib_header = bitmap_info.bmiHeader;
    file = fopen(path, "wb");
    if( !file )
        ok = 0;
    if( ok && fwrite(&file_header, sizeof(file_header), 1, file) != 1 )
        ok = 0;
    if( ok && fwrite(&dib_header, sizeof(dib_header), 1, file) != 1 )
        ok = 0;
    if( ok && fwrite(bits, byte_count, 1, file) != 1 )
        ok = 0;
    if( file && fclose(file) != 0 )
        ok = 0;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if( !ok )
        return 0;
    *out_w = width;
    *out_h = height;
    return 1;
}

int
main(int argc, char** argv)
{
    char const* output = argc > 1 ? argv[1] : "plugin_chrome_browser_capture.bmp";
    struct PlatformWindow* platform = PlatformWindow_New();
    struct ToriRSChromeExec exec;
    struct ToriRS_CmdBus bus;
    HWND hwnd;
    HWND page;
    int* game;
    int capture_w;
    int capture_h;
    int browser_captured;
    char browser_output[MAX_PATH];

    if( !platform ||
        !PlatformWindow_Init(platform, 512, 360, "ToriRS Plugin Chrome Capture") )
        fail("could not create the production main HWND");
    hwnd = (HWND)PlatformWindow_NativeWindowHandle(platform);
    SetWindowPos(hwnd, NULL, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(
        hwnd, HWND_TOPMOST, 40, 40, 0, 0,
        SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);

    game = PlatformWindow_Pixels(platform);
    for( int y = 0; y < PlatformWindow_Height(platform); y++ )
        for( int x = 0; x < PlatformWindow_Width(platform); x++ )
        {
            unsigned const r = 25 + (unsigned)(x * 30 / PlatformWindow_Width(platform));
            unsigned const g = 34 + (unsigned)(y * 45 / PlatformWindow_Height(platform));
            unsigned const b = 24;
            game[y * PlatformWindow_Width(platform) + x] =
                (int)((r << 16) | (g << 8) | b);
        }
    PlatformWindow_Present(platform);

    exec = ToriRSChromeExec_Browser(platform);
    if( !exec.rail_sync || !exec.rail_icon || !exec.rail_poll ||
        !exec.begin || !exec.apply || !exec.poll || !exec.custom_present )
        fail("browser executor is missing a production callback");
    publish_rail(&exec);
    if( !exec.begin(exec.user) )
        fail("production browser executor did not attach its page");
    publish_page(&exec);
    page = (HWND)PlatformWindow_Win32ChromeHandle(platform);
    if( !page || GetParent(page) != hwnd )
        fail("browser is not attached to the production main HWND");
    publish_custom(&exec);

    CmdBus_Init(&bus);
    pump_until_ready(platform, &bus, 15000);
    {
        int const length = snprintf(
            browser_output, sizeof(browser_output), "%s.browser.png", output);
        if( length <= 0 || length >= (int)sizeof(browser_output) )
            fail("browser preview path is too long");
    }
    browser_captured = capture_browser_png(platform, &bus, browser_output);
    UpdateWindow(hwnd);
    if( !write_client_bmp(hwnd, output, &capture_w, &capture_h) )
        fail("could not write the requested BMP capture");

    if( browser_captured )
        printf(
            "win32_plugin_chrome_capture: wrote %s (%dx%d) and %s "
            "(production WebView rail + one page)\n",
            output, capture_w, capture_h, browser_output);
    else
        printf(
            "win32_plugin_chrome_capture: wrote %s (%dx%d, production "
            "MSHTML rail + one page)\n",
            output, capture_w, capture_h);
    exec.end(exec.user);
    PlatformWindow_Free(platform);
    return 0;
}
