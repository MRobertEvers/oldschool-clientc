#ifndef SRC_PLATFORM_PLATFORM_WIN32_BROWSER_BACKEND_H
#define SRC_PLATFORM_PLATFORM_WIN32_BROWSER_BACKEND_H

/* Private ABI shared by platform_win32gdi.c and exactly one Windows browser
 * backend. The semantic executor sees only platform_window.h. */

#include <windows.h>

struct PlatformWin32Browser;

struct PlatformWin32Browser*
PlatformWin32Browser_New(HWND parent, WCHAR const* bundle_root);
void PlatformWin32Browser_Free(struct PlatformWin32Browser* browser);
void PlatformWin32Browser_Resize(
    struct PlatformWin32Browser* browser, int width, int height);
void PlatformWin32Browser_Send(
    struct PlatformWin32Browser* browser, char const* json);
int PlatformWin32Browser_Poll(
    struct PlatformWin32Browser* browser, char* out_json, int capacity);
int PlatformWin32Browser_Ready(struct PlatformWin32Browser const* browser);
int PlatformWin32Browser_Failed(struct PlatformWin32Browser const* browser);
int PlatformWin32Browser_PreTranslateMessage(
    struct PlatformWin32Browser* browser, MSG* message);
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
int PlatformWin32Browser_CapturePng(
    struct PlatformWin32Browser* browser, char const* path);
int PlatformWin32Browser_CaptureStatus(
    struct PlatformWin32Browser const* browser);
#endif
void PlatformWin32Browser_AllowLocalRoot(
    struct PlatformWin32Browser* browser, WCHAR const* file_url_root);

#endif
