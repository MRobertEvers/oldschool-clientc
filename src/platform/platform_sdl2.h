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

uint64_t
PlatformSDL2_Ticks64(void);

void
PlatformSDL2_Delay(uint32_t ms);

#endif
