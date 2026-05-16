#ifndef PLATFORM_SDL2_H
#define PLATFORM_SDL2_H

#include "../commands/libtorirs_command_queue.h"

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2;

struct LibToriPlatformSDL2*
LibToriPlatformSDL2_New(void);

void
LibToriPlatformSDL2_Free(struct LibToriPlatformSDL2* platform);

bool
LibToriPlatformSDL2_InitForSoft3D(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height);

void
LibToriPlatformSDL2_PollEvents(
    struct LibToriPlatformSDL2* platform,
    struct LibToriRS_CommandQueue* command_queue);

enum LibToriRS_KeyCode
LibToriPlatformSDL2_SDLKeyCodeToKeyCode(SDL_Keycode key_code);

enum LibToriRS_MouseButton
LibToriPlatformSDL2_SDLMouseButtonToMouseButton(int mouse_button);

#endif