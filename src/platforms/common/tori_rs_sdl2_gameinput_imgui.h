#ifndef TORI_RS_SDL2_GAMEINPUT_IMGUI_H
#define TORI_RS_SDL2_GAMEINPUT_IMGUI_H

#include <SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
ToriRSLibPlatform_SDL2_GameInput_ImGui_ProcessEvent(const SDL_Event* event);

bool
ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureKeyboard(void);

bool
ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureMouse(void);

#ifdef __cplusplus
}
#endif

#endif
