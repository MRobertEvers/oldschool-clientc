#ifndef SRC_PLATFORM_PLATFORM_SDL2_INPUT_H
#define SRC_PLATFORM_PLATFORM_SDL2_INPUT_H

/*
 * The SDL desktop's translation of its wheel-shaped events onto the command
 * bus, exposed so platform/test/sdl_gesture_test.c can hand it events SDL's
 * own queue would not carry intact (sdl2-compat's SDL_PushEvent drops the
 * precise fields). Called from PlatformWindow_PollCommands for the main
 * window's events; nothing else should need them.
 */

#include <SDL.h>

struct PlatformWindow;
struct ToriRS_CmdBus;

/**
 * SDL_MOUSEWHEEL: the integer notches (TORIRS_CMD_INPUT_MOUSE_WHEEL,
 * normalised against the platform's natural-scrolling flip, as always) and the
 * exact deltas beside them (TORIRS_CMD_INPUT_SCROLL: canvas pixels, the
 * platform's own direction kept, flagged trackpad when the deltas are
 * fractions -- or, on the web, by the page's wheel classification). On macOS a
 * trackpad's deltas come from AppKit instead (platform_macos_gesture.m).
 */
void PlatformSdl_PushWheel(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    SDL_MouseWheelEvent const* wheel);

/**
 * SDL_MULTIGESTURE: a TORIRS_CMD_INPUT_PINCH measured from the device's first
 * two fingers (spread now / spread at the previous gesture), from_wheel set
 * because the same fingers reach the touch layer's notches. Fewer than two
 * fingers: nothing, and the next gesture measures afresh.
 */
void PlatformSdl_PushMultiGesture(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    SDL_MultiGestureEvent const* gesture);

#endif
