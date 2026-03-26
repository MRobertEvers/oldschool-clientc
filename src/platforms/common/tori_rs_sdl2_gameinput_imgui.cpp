#include "tori_rs_sdl2_gameinput_imgui.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"

void
ToriRSLibPlatform_SDL2_GameInput_ImGui_ProcessEvent(const SDL_Event* event)
{
    if( ImGui::GetCurrentContext() == nullptr )
        return;
    ImGui_ImplSDL2_ProcessEvent(event);
}

bool
ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureKeyboard(void)
{
    if( ImGui::GetCurrentContext() == nullptr )
        return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool
ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureMouse(void)
{
    if( ImGui::GetCurrentContext() == nullptr )
        return false;
    return ImGui::GetIO().WantCaptureMouse;
}
