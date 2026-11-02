/*
 * Trackpad pinch and precise scroll for the SDL client window.
 * @see platform_macos_gesture.h
 */

#import <AppKit/AppKit.h>

#include "platform_macos_gesture.h"
#include "platform_window.h"

#include <assert.h>

static id g_monitor = nil;
static struct PlatformWindow* g_platform = NULL;
static double g_pinch_scale = 1.0;
static int g_pinch_pending = 0;
static double g_scroll_dx = 0.0;
static double g_scroll_dy = 0.0;
static int g_scroll_count = 0;
static int g_scroll_flipped = 0;

static int
mac_gesture_is_ours(NSEvent* event)
{
    void* const native = g_platform ? PlatformWindow_NativeWindowHandle(g_platform) : NULL;
    return native && (__bridge void*)[event window] == native;
}

void
PlatformMacGesture_Install(struct PlatformWindow* platform)
{
    assert(platform);
    g_platform = platform;
    if( g_monitor )
        return;
    g_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:(NSEventMaskMagnify | NSEventMaskScrollWheel)
                                     handler:^NSEvent*(NSEvent* event) {
                                       if( !mac_gesture_is_ours(event) )
                                           return event;
                                       if( [event type] == NSEventTypeMagnify )
                                       {
                                           /* AppKit: the content scales by
                                            * 1 + magnification, per event. */
                                           double const m = [event magnification];
                                           if( m > -0.95 )
                                           {
                                               g_pinch_scale *= 1.0 + m;
                                               g_pinch_pending = 1;
                                           }
                                       }
                                       else if(
                                           [event type] == NSEventTypeScrollWheel &&
                                           [event hasPreciseScrollingDeltas] )
                                       {
                                           /* Points, natural scrolling already
                                            * applied; SDL's sign: x negated so
                                            * +x is "scroll right". */
                                           g_scroll_dx += -[event scrollingDeltaX];
                                           g_scroll_dy += [event scrollingDeltaY];
                                           g_scroll_flipped =
                                               [event isDirectionInvertedFromDevice] ? 1 : 0;
                                           g_scroll_count++;
                                       }
                                       return event;
                                     }];
}

void
PlatformMacGesture_Uninstall(void)
{
    if( g_monitor )
        [NSEvent removeMonitor:g_monitor];
    g_monitor = nil;
    g_platform = NULL;
    g_pinch_scale = 1.0;
    g_pinch_pending = 0;
    g_scroll_dx = g_scroll_dy = 0.0;
    g_scroll_count = 0;
}

int
PlatformMacGesture_TakePinch(double* out_scale)
{
    assert(out_scale);
    if( !g_pinch_pending )
        return 0;
    *out_scale = g_pinch_scale;
    g_pinch_scale = 1.0;
    g_pinch_pending = 0;
    return 1;
}

int
PlatformMacGesture_TakeScroll(double* out_dx, double* out_dy, int* out_flipped)
{
    int const count = g_scroll_count;

    assert(out_dx);
    assert(out_dy);
    assert(out_flipped);
    *out_dx = g_scroll_dx;
    *out_dy = g_scroll_dy;
    *out_flipped = g_scroll_flipped;
    g_scroll_dx = g_scroll_dy = 0.0;
    g_scroll_count = 0;
    return count;
}

int
PlatformMacGesture_PreciseScrollPending(void)
{
    return g_scroll_count > 0;
}
