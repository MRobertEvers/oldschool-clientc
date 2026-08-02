#ifndef SRC_PLATFORM_PLATFORM_SDL2_H
#define SRC_PLATFORM_PLATFORM_SDL2_H

#include <stdbool.h>
#include <stdint.h>

struct PlatformSDL2;
struct ToriRS_CmdBus;

struct PlatformSDL2*
PlatformSDL2_New(void);

bool
PlatformSDL2_Init(
    struct PlatformSDL2* platform,
    int width,
    int height,
    char const* title);

/** Create an SDL OpenGL window (no CPU pixel buffer / SDL_Renderer).
 *  macOS: GL 3.2 core + forward-compatible; elsewhere: GL 3.3 core. */
bool
PlatformSDL2_InitForOpenGL3(
    struct PlatformSDL2* platform,
    int width,
    int height,
    char const* title);

struct SDL_Window*
PlatformSDL2_Window(struct PlatformSDL2* platform);

void
PlatformSDL2_Free(struct PlatformSDL2* platform);

int*
PlatformSDL2_Pixels(struct PlatformSDL2* platform);

int
PlatformSDL2_Width(struct PlatformSDL2* platform);

int
PlatformSDL2_Height(struct PlatformSDL2* platform);

bool
PlatformSDL2_QuitRequested(struct PlatformSDL2* platform);

void
PlatformSDL2_SetTitle(
    struct PlatformSDL2* platform,
    char const* title);

/**
 * Resizable mode: make the logical framebuffer track the window instead of
 * letterboxing a fixed one into it. While set, every window size change emits
 * TORIRS_CMD_WINDOW_RESIZE so the client relayouts; while clear, the window
 * only scales what is already drawn. Turning it ON pushes one resize command
 * for the current window size, so the caller does not have to wait for the user
 * to drag something.
 *
 * `min_w`/`min_h` are the client's canvas floor. They become the window's
 * minimum size in BOTH modes, because a window smaller than the floor is the
 * one case the client cannot answer with layout — the canvas clamps and the
 * present scales it down. Below the floor, "resize" is not available at any
 * layer, so the window is not allowed there.
 *
 * Turning it OFF also snaps the window back to exactly `min_w x min_h`: fixed
 * mode *is* that frame, and leaving a larger window behind would present it
 * upscaled, which is the behaviour the mode switch exists to leave. The size it
 * snapped away from is remembered and restored when it is turned back ON, so
 * resizable -> fixed -> resizable returns the user to the window they had
 * instead of leaving them at the floor.
 *
 * This does NOT resize the backbuffer. The client clamps the canvas to a floor
 * it owns, so the canvas — not the window — is what the backbuffer must match;
 * the caller reconciles them with PlatformSDL2_Resize after draining the bus.
 */
void
PlatformSDL2_SetCanvasFollowsWindow(
    struct PlatformSDL2* platform,
    struct ToriRS_CmdBus* bus,
    bool follow,
    int min_w,
    int min_h);

/**
 * Resize the OS window, as if the user had dragged its corner.
 *
 * The distinction from PlatformSDL2_Resize matters: this touches the window and
 * nothing else, so what happens next is decided by the follow gate exactly as it
 * would be for a real drag. It is the only way to exercise that gate headlessly
 * — pushing TORIRS_CMD_WINDOW_RESIZE straight onto the bus skips it.
 */
void
PlatformSDL2_SetWindowSize(
    struct PlatformSDL2* platform,
    int width,
    int height);

/**
 * Resize the logical framebuffer (pixels + streaming texture) in place. No-op
 * when the size is unchanged or the platform is in GL mode (GL draws straight
 * to the window and owns no CPU buffer). Returns true when the size changed.
 */
bool
PlatformSDL2_Resize(
    struct PlatformSDL2* platform,
    int width,
    int height);

/** Map window-pixel mouse coords into the letterboxed logical framebuffer. */
void
PlatformSDL2_MapMouse(
    struct PlatformSDL2* platform,
    int win_x,
    int win_y,
    int* out_x,
    int* out_y);

/**
 * Poll SDL events into the command bus as TORIRS_CMD_INPUT_* commands (mouse
 * coords already mapped to logical space, so recordings are window-size
 * independent). Window-level events (quit, esc-quit) stay platform state.
 */
void
PlatformSDL2_PollCommands(
    struct PlatformSDL2* platform,
    struct ToriRS_CmdBus* bus);

void
PlatformSDL2_Present(struct PlatformSDL2* platform);

/** Swap the GL backbuffer. Only valid after InitForOpenGL3. */
void
PlatformSDL2_PresentGL(struct PlatformSDL2* platform);

uint64_t
PlatformSDL2_Ticks64(void);

void
PlatformSDL2_Delay(uint32_t ms);

#endif
