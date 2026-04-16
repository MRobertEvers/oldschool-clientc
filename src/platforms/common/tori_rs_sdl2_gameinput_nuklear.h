#ifndef TORI_RS_SDL2_GAMEINPUT_NUKLEAR_H
#define TORI_RS_SDL2_GAMEINPUT_NUKLEAR_H

#include <SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
ToriRSLibPlatform_SDL2_GameInput_NK_PrePoll(void);

void
ToriRSLibPlatform_SDL2_GameInput_NK_ProcessEvent(const SDL_Event* event);

void
ToriRSLibPlatform_SDL2_GameInput_NK_PostPoll(void);

bool
ToriRSLibPlatform_SDL2_GameInput_NK_WantCaptureKeyboard(void);

bool
ToriRSLibPlatform_SDL2_GameInput_NK_WantCaptureMouse(void);

#ifdef __cplusplus
}
#endif

#endif
