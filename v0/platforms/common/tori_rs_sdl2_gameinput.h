#ifndef TORI_RS_SDL2_GAMEINPUT_H
#define TORI_RS_SDL2_GAMEINPUT_H

#include "osrs/ginput.h"

#include <SDL.h>

enum GameInputKeyCode
ToriRSLibPlatform_SDL2_GameInput_SDLKeyCodeToKeyCode(SDL_Keycode key_code);

void
ToriRSLibPlatform_SDL2_GameInput_PollEvents(
    struct GInput* input,
    double time_s);

#endif