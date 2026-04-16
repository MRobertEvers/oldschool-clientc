#include "tori_rs_sdl2_gameinput_nuklear.h"

#include "torirs_nk_ui_bridge.h"

void
ToriRSLibPlatform_SDL2_GameInput_NK_PrePoll(void)
{
    torirs_nk_ui_pre_poll_events();
}

void
ToriRSLibPlatform_SDL2_GameInput_NK_ProcessEvent(const SDL_Event* event)
{
    if( !event )
        return;
    torirs_nk_ui_process_event((SDL_Event*)event);
}

void
ToriRSLibPlatform_SDL2_GameInput_NK_PostPoll(void)
{
    torirs_nk_ui_post_poll_events();
}

bool
ToriRSLibPlatform_SDL2_GameInput_NK_WantCaptureKeyboard(void)
{
    return torirs_nk_ui_want_capture_keyboard_prev();
}

bool
ToriRSLibPlatform_SDL2_GameInput_NK_WantCaptureMouse(void)
{
    return torirs_nk_ui_want_capture_mouse_prev();
}
