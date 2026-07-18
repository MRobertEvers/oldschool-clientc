#ifndef SRC_PLATFORM_PLATFORM_SDL2_H
#define SRC_PLATFORM_PLATFORM_SDL2_H

#include "input/torirs_input.h"

#include <stdbool.h>

struct PlatformSDL2;

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

/** Poll SDL events into input (mouse coords already mapped to logical space). */
void
PlatformSDL2_PollInput(
    struct PlatformSDL2* platform,
    struct LibToriRS_Input* input);

void
PlatformSDL2_Present(struct PlatformSDL2* platform);

#endif
