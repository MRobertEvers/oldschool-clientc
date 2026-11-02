#ifndef SRC_PLATFORM_PLATFORM_MACOS_GESTURE_H
#define SRC_PLATFORM_PLATFORM_MACOS_GESTURE_H

/*
 * The trackpad gestures SDL2 (sdl2-compat over SDL3) does not hand an SDL2
 * client: a pinch arrives at AppKit as NSEventTypeMagnify and SDL2 never
 * reports it (SDL3's own SDL_EVENT_PINCH_* is not forwarded through the
 * compat layer), and SDL's wheel event does not say whether it came from a
 * trackpad. One local NSEvent monitor watches both, for the client's own
 * window only, and leaves every event to go on to SDL unchanged.
 *
 * Private to platform_sdl2.c.
 */

struct PlatformWindow;

/** Watch `platform`'s window (its CURRENT NSWindow, asked per event, since a
 *  renderer switch may replace it). Idempotent. */
void PlatformMacGesture_Install(struct PlatformWindow* platform);
void PlatformMacGesture_Uninstall(void);

/** The pinch since the last take, as one scale (> 1 apart). @return 0 when
 *  there was none. */
int PlatformMacGesture_TakePinch(double* out_scale);

/**
 * The trackpad's exact scroll since the last take, window POINTS, in SDL's
 * convention (+y scroll up, +x scroll right), AppKit's natural-scrolling
 * setting applied. `out_flipped`: that setting is on. @return the number of
 * precise (trackpad) scroll events folded in, 0 when there were none -- the
 * SDL wheel events of this batch then came from a mouse.
 */
int PlatformMacGesture_TakeScroll(double* out_dx, double* out_dy, int* out_flipped);

/** A trackpad scroll has been seen since the last take. */
int PlatformMacGesture_PreciseScrollPending(void);

#endif
