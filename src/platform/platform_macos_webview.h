#ifndef SRC_PLATFORM_PLATFORM_MACOS_WEBVIEW_H
#define SRC_PLATFORM_PLATFORM_MACOS_WEBVIEW_H

struct PlatformWindow;

/* Private lifecycle hooks used by platform_sdl2.c.  The public semantic
 * transport is the PlatformWindow_PluginBrowser* family in platform_window.h;
 * only the Cocoa-backed SDL window needs to know how that transport is torn
 * down and kept aligned with the trailing-edge allocation. */
void PlatformMacPluginBrowser_SyncFrame(struct PlatformWindow* platform);
void PlatformMacPluginBrowser_Destroy(struct PlatformWindow* platform);
/* Re-order the browser's child window onto the SDL window's CURRENT native
 * window. A renderer switch may make SDL replace that NSWindow under the same
 * SDL_Window, and a child stays on the parent it was added to. */
void PlatformMacPluginBrowser_Reattach(struct PlatformWindow* platform);

#endif
