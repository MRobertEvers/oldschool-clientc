#include "platform/platform_win32_browser_backend.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Deterministic browser stand-in for the Win32 window-layout test.
 *
 * The production platform object still creates, grows, shrinks and destroys
 * its real browser-container HWND.  This tiny backend supplies only the
 * nested rendering control so the test does not depend on an installed
 * MSHTML/WebView2 runtime (or network/user-profile state).
 */
struct PlatformWin32Browser
{
    HWND child;
    int ready;
};

static int
staged_file(WCHAR const* root, WCHAR const* relative)
{
    WCHAR path[MAX_PATH];
    DWORD attributes;

    if( _snwprintf(path, MAX_PATH, L"%s\\%s", root, relative) <= 0 )
        return 0;
    attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

struct PlatformWin32Browser*
PlatformWin32Browser_New(HWND parent, WCHAR const* bundle_root)
{
    static WCHAR const* const required[] = {
        L"modern.html", L"modern.css", L"codec-es3.js", L"runtime.js",
        L"legacy-ie8.html", L"legacy-ie8.css", L"runtime-ie8.js",
        L"skin\\PanelBody.png", L"skin\\PluginIcon.png",
        L"skin\\ButtonLeft.png", L"skin\\ButtonMid.png",
        L"skin\\ButtonRight.png", L"skin\\CheckOn.png",
        L"skin\\CheckOff.png", L"skin\\CheckBoxOn.png",
        L"skin\\CheckBoxOff.png", L"skin\\DropdownBody.png",
        L"skin\\ScrollUp.png", L"skin\\ScrollDown.png",
        L"skin\\ScrollTrack.png", L"skin\\ScrollGripTop.png",
        L"skin\\ScrollGripMid.png", L"skin\\ScrollGripBottom.png",
        L"skin\\CloseButton.png", L"skin\\CloseButtonOver.png",
        L"font\\ToriRSBody.eot", L"font\\ToriRSBody.woff",
        L"font\\ToriRSBody.ttf", L"font\\ToriRSMenu.eot",
        L"font\\ToriRSMenu.woff", L"font\\ToriRSMenu.ttf",
        L"font\\ToriRSSmall.eot", L"font\\ToriRSSmall.woff",
        L"font\\ToriRSSmall.ttf", L"font\\manifest.json"
    };
    struct PlatformWin32Browser* browser;

    if( !parent || !bundle_root || !bundle_root[0] )
        return NULL;
    for( int i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++ )
        if( !staged_file(bundle_root, required[i]) )
            return NULL;
    browser = (struct PlatformWin32Browser*)calloc(1, sizeof(*browser));
    if( !browser )
        return NULL;
    browser->child = CreateWindowExA(
        0, "STATIC", "plugin-browser-test", WS_CHILD | WS_VISIBLE,
        0, 0, 1, 1, parent, NULL, GetModuleHandleA(NULL), NULL);
    if( !browser->child )
    {
        free(browser);
        return NULL;
    }
    browser->ready = 1;
    return browser;
}

void
PlatformWin32Browser_Free(struct PlatformWin32Browser* browser)
{
    if( !browser )
        return;
    if( browser->child )
        DestroyWindow(browser->child);
    free(browser);
}

void
PlatformWin32Browser_Resize(
    struct PlatformWin32Browser* browser, int width, int height)
{
    if( browser && browser->child )
        SetWindowPos(
            browser->child, NULL, 0, 0, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);
}

int
PlatformWin32Browser_Send(
    struct PlatformWin32Browser* browser, char const* json)
{
    (void)browser;
    (void)json;
    return 1;
}

int
PlatformWin32Browser_TakeSendFailure(struct PlatformWin32Browser* browser)
{
    (void)browser;
    return 0;
}

int
PlatformWin32Browser_Poll(
    struct PlatformWin32Browser* browser, char* out_json, int capacity)
{
    (void)browser;
    if( out_json && capacity > 0 )
        out_json[0] = '\0';
    return 0;
}

int
PlatformWin32Browser_Ready(struct PlatformWin32Browser const* browser)
{
    return browser && browser->ready;
}

int
PlatformWin32Browser_Failed(struct PlatformWin32Browser const* browser)
{
    return !browser;
}

int
PlatformWin32Browser_PreTranslateMessage(
    struct PlatformWin32Browser* browser, MSG* message)
{
    (void)browser;
    (void)message;
    return 0;
}

#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
int PlatformWin32Browser_CapturePng(
    struct PlatformWin32Browser* browser, char const* path)
{ (void)browser; (void)path; return 0; }
int PlatformWin32Browser_CaptureStatus(
    struct PlatformWin32Browser const* browser)
{ (void)browser; return -1; }
#endif

void
PlatformWin32Browser_AllowLocalRoot(
    struct PlatformWin32Browser* browser, WCHAR const* file_url_root)
{
    (void)browser;
    (void)file_url_root;
}
