#ifndef SRC_PLATFORM_PLATFORM_WIN32_CHROME_H
#define SRC_PLATFORM_PLATFORM_WIN32_CHROME_H

/*
 * Win32-only seams between the raw USER32 host and the GDI chrome executor.
 *
 * The portable surface API in platform_window.h deliberately exposes pixels,
 * not HWNDs.  The GDI executor is the one exception that needs the pane HWND:
 * it puts native EDIT/COMBOBOX children behind the ToriRSChrome skin.  Keeping
 * these entry points here avoids making every non-Windows backend invent a
 * meaningless native-child accessor.
 */

#include <stdbool.h>
#include <stdint.h>

struct PlatformWindow;
struct ToriRSChromeRailIcon;
struct ToriRSChromeRailIntent;
struct ToriRSChromeRailSnapshot;

/* Two class-extra LONG_PTRs publish the game inset without coupling D3D9 to
 * PlatformWindow's private layout structure.  The magic keeps an arbitrary
 * HWND used by a renderer probe from treating its own class-extra word as an
 * inset. */
#define TORIRS_WIN32_CHROME_EXTRA_MAGIC_OFFSET 0
#define TORIRS_WIN32_CHROME_EXTRA_RESERVED_OFFSET ((int)sizeof(intptr_t))
#define TORIRS_WIN32_CHROME_EXTRA_MAGIC ((intptr_t)UINT32_C(0x54524348))

/** The one attached plugin-page child, or NULL while the page is collapsed. */
void*
PlatformWindow_Win32ChromeHandle(struct PlatformWindow* platform);

/**
 * Size available to the game, excluding the persistent rail and open page.
 * D3D9 uses this rather than the top-level HWND's full client size so its one
 * renderer never draws underneath the attached chrome child.
 */
bool
PlatformWindow_Win32GameClientSize(
    void* native_window, int* out_width, int* out_height);

/**
 * Persistent rail presentation.  These remain valid while the selected page
 * executor is stopped: the rail is application chrome owned by the main HWND,
 * not a child of the currently mounted plugin page.
 */
void PlatformWindow_Win32ChromeRailSync(
    struct PlatformWindow* platform,
    struct ToriRSChromeRailSnapshot const* snapshot);
void PlatformWindow_Win32ChromeRailIcon(
    struct PlatformWindow* platform,
    struct ToriRSChromeRailIcon const* icon);
int PlatformWindow_Win32ChromeRailPoll(
    struct PlatformWindow* platform,
    struct ToriRSChromeRailIntent* out,
    int max);

#if defined(TORIRS_WIN32_GDI_TEST_API)
void*
PlatformWindow_Win32TestRailHandle(struct PlatformWindow* platform);
#endif

#endif
