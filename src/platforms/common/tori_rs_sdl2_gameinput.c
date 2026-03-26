#include "tori_rs_sdl2_gameinput.h"
#include "tori_rs_sdl2_gameinput_imgui.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum GameInputKeyCode
ToriRSLibPlatform_SDL2_GameInput_SDLKeyCodeToKeyCode(SDL_Keycode key_code)
{
    switch( key_code )
    {
    case SDLK_a:
        return TORIRSK_A;
    case SDLK_b:
        return TORIRSK_B;
    case SDLK_c:
        return TORIRSK_C;
    case SDLK_d:
        return TORIRSK_D;
    case SDLK_e:
        return TORIRSK_E;
    case SDLK_f:
        return TORIRSK_F;
    case SDLK_g:
        return TORIRSK_G;
    case SDLK_h:
        return TORIRSK_H;
    case SDLK_i:
        return TORIRSK_I;
    case SDLK_j:
        return TORIRSK_J;
    case SDLK_k:
        return TORIRSK_K;
    case SDLK_l:
        return TORIRSK_L;
    case SDLK_m:
        return TORIRSK_M;
    case SDLK_n:
        return TORIRSK_N;
    case SDLK_o:
        return TORIRSK_O;
    case SDLK_p:
        return TORIRSK_P;
    case SDLK_q:
        return TORIRSK_Q;
    case SDLK_r:
        return TORIRSK_R;
    case SDLK_s:
        return TORIRSK_S;
    case SDLK_t:
        return TORIRSK_T;
    case SDLK_u:
        return TORIRSK_U;
    case SDLK_v:
        return TORIRSK_V;
    case SDLK_w:
        return TORIRSK_W;
    case SDLK_x:
        return TORIRSK_X;
    case SDLK_y:
        return TORIRSK_Y;
    case SDLK_z:
        return TORIRSK_Z;
    case SDLK_0:
        return TORIRSK_0;
    case SDLK_1:
        return TORIRSK_1;
    case SDLK_2:
        return TORIRSK_2;
    case SDLK_3:
        return TORIRSK_3;
    case SDLK_4:
        return TORIRSK_4;
    case SDLK_5:
        return TORIRSK_5;
    case SDLK_6:
        return TORIRSK_6;
    case SDLK_7:
        return TORIRSK_7;
    case SDLK_8:
        return TORIRSK_8;
    case SDLK_9:
        return TORIRSK_9;
    case SDLK_ESCAPE:
        return TORIRSK_ESCAPE;
    case SDLK_RETURN:
        return TORIRSK_RETURN;
    case SDLK_BACKSPACE:
        return TORIRSK_BACKSPACE;
    case SDLK_INSERT:
        return TORIRSK_INSERT;
    case SDLK_DELETE:
        return TORIRSK_DELETE;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return TORIRSK_CTRL;
    case SDLK_COMMA:
        return TORIRSK_COMMA;
    case SDLK_TAB:
        return TORIRSK_TAB;
    case SDLK_SPACE:
        return TORIRSK_SPACE;
    case SDLK_LEFT:
        return TORIRSK_LEFT;
    case SDLK_RIGHT:
        return TORIRSK_RIGHT;
    case SDLK_UP:
        return TORIRSK_UP;
    case SDLK_DOWN:
        return TORIRSK_DOWN;
    case SDLK_PAGEUP:
        return TORIRSK_PAGE_UP;
    case SDLK_PAGEDOWN:
        return TORIRSK_PAGE_DOWN;
    default:
        return TORIRSK_UNKNOWN;
    }
}

static void
push_keydown_event(
    struct GInput* input,
    SDL_Keycode key)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_KEY_DOWN;
    input->in_events[input->in_event_count].key_down.key =
        ToriRSLibPlatform_SDL2_GameInput_SDLKeyCodeToKeyCode(key);
    input->in_event_count++;
}

static void
push_keyup_event(
    struct GInput* input,
    SDL_Keycode key)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_KEY_UP;
    input->in_events[input->in_event_count].key_up.key =
        ToriRSLibPlatform_SDL2_GameInput_SDLKeyCodeToKeyCode(key);
    input->in_event_count++;
}

static void
push_mouse_move_event(
    struct GInput* input,
    SDL_MouseMotionEvent* event)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_MOUSE_MOVE;
    struct GameInputEvent_MouseMove* mouse_move =
        &input->in_events[input->in_event_count].mouse_move;
    mouse_move->x = event->x;
    mouse_move->y = event->y;
    mouse_move->xrel = event->xrel;
    mouse_move->yrel = event->yrel;
    input->in_event_count++;
}

static void
push_mouse_down_event(
    struct GInput* input,
    SDL_MouseButtonEvent* event)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_MOUSE_DOWN;
    struct GameInputEvent_MouseDown* mouse_down =
        &input->in_events[input->in_event_count].mouse_down;
    mouse_down->mouse_x = event->x;
    mouse_down->mouse_y = event->y;
    mouse_down->button = event->button;
    input->in_event_count++;
}

static void
push_mouse_up_event(
    struct GInput* input,
    SDL_MouseButtonEvent* event)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_MOUSE_UP;
    struct GameInputEvent_MouseUp* mouse_up = &input->in_events[input->in_event_count].mouse_up;
    mouse_up->mouse_x = event->x;
    mouse_up->mouse_y = event->y;
    mouse_up->button = event->button;
    input->in_event_count++;
}

static void
push_mouse_wheel_event(
    struct GInput* input,
    SDL_MouseWheelEvent* event)
{
    assert(input->in_event_count < GAME_INPUT_MAX_EVENTS);
    input->in_events[input->in_event_count].type = TORIRSEV_MOUSE_WHEEL;
    struct GameInputEvent_MouseWheel* mouse_wheel =
        &input->in_events[input->in_event_count].mouse_wheel;
    mouse_wheel->mouse_x = event->mouseX;
    mouse_wheel->mouse_y = event->mouseY;
    mouse_wheel->wheel = event->y;
    input->in_event_count++;
}

void
ToriRSLibPlatform_SDL2_GameInput_PollEvents(
    struct GInput* input,
    double time)
{
    double time_delta_seconds = time - input->time;
    input->time_delta_accumulator_seconds += time_delta_seconds;
    input->time = time;
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        ToriRSLibPlatform_SDL2_GameInput_ImGui_ProcessEvent(&event);
        bool imgui_wants_keyboard =
            ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureKeyboard();
        bool imgui_wants_mouse =
            ToriRSLibPlatform_SDL2_GameInput_ImGui_WantCaptureMouse();

        switch( event.type )
        {
        case SDL_QUIT:
            input->quit = 1;
            break;
        case SDL_KEYDOWN:
            if( !imgui_wants_keyboard )
                push_keydown_event(input, event.key.keysym.sym);
            break;
        case SDL_KEYUP:
            if( !imgui_wants_keyboard )
                push_keyup_event(input, event.key.keysym.sym);
            break;
        case SDL_MOUSEMOTION:
            if( !imgui_wants_mouse )
                push_mouse_move_event(input, &event.motion);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if( !imgui_wants_mouse )
                push_mouse_down_event(input, &event.button);
            break;
        case SDL_MOUSEBUTTONUP:
            if( !imgui_wants_mouse )
                push_mouse_up_event(input, &event.button);
            break;
        case SDL_MOUSEWHEEL:
            if( !imgui_wants_mouse )
                push_mouse_wheel_event(input, &event.wheel);
            break;
        }
    }
}