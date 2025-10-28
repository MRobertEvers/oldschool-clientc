#include "platform_impl_emscripten_sdl2.h"

#include <SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

struct Platform*
PlatformImpl_Emscripten_SDL2_New(void)
{
    struct Platform* platform = (struct Platform*)malloc(sizeof(struct Platform));
    memset(platform, 0, sizeof(struct Platform));

    return platform;
}

void
PlatformImpl_Emscripten_SDL2_Free(struct Platform* platform)
{
    free(platform);
}

bool
PlatformImpl_Emscripten_SDL2_InitForSoft3D(
    struct Platform* platform,
    int canvas_width,
    int canvas_height,
    int max_canvas_width,
    int max_canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "3D Raster - WebGL1",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
PlatformImpl_Emscripten_SDL2_Shutdown(struct Platform* platform)
{
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
}

void
PlatformImpl_Emscripten_SDL2_PollEvents(struct Platform* platform, struct GameInput* input)
{
    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;

    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            input->should_quit = true;
            break;

        case SDL_KEYDOWN:
        {
            SDL_Keycode key = event.key.keysym.sym;
            switch( key )
            {
            case SDLK_w:
                input->is_key_w_down = true;
                break;
            case SDLK_a:
                input->is_key_a_down = true;
                break;
            case SDLK_s:
                input->is_key_s_down = true;
                break;
            case SDLK_d:
                input->is_key_d_down = true;
                break;
            case SDLK_q:
                input->is_key_q_down = true;
                break;
            case SDLK_e:
                input->is_key_e_down = true;
                break;
            case SDLK_SPACE:
                input->is_key_space_down = true;
                break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT:
                input->is_key_shift_down = true;
                break;
            case SDLK_UP:
                input->is_key_up_down = true;
                break;
            case SDLK_DOWN:
                input->is_key_down_down = true;
                break;
            case SDLK_LEFT:
                input->is_key_left_down = true;
                break;
            case SDLK_RIGHT:
                input->is_key_right_down = true;
                break;
            case SDLK_ESCAPE:
                input->should_quit = true;
                break;
            default:
                break;
            }
            break;
        }

        case SDL_KEYUP:
        {
            SDL_Keycode key = event.key.keysym.sym;
            switch( key )
            {
            case SDLK_w:
                input->is_key_w_down = false;
                break;
            case SDLK_a:
                input->is_key_a_down = false;
                break;
            case SDLK_s:
                input->is_key_s_down = false;
                break;
            case SDLK_d:
                input->is_key_d_down = false;
                break;
            case SDLK_q:
                input->is_key_q_down = false;
                break;
            case SDLK_e:
                input->is_key_e_down = false;
                break;
            case SDLK_SPACE:
                input->is_key_space_down = false;
                break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT:
                input->is_key_shift_down = false;
                break;
            case SDLK_UP:
                input->is_key_up_down = false;
                break;
            case SDLK_DOWN:
                input->is_key_down_down = false;
                break;
            case SDLK_LEFT:
                input->is_key_left_down = false;
                break;
            case SDLK_RIGHT:
                input->is_key_right_down = false;
                break;
            default:
                break;
            }
            break;
        }

        case SDL_MOUSEMOTION:
            input->mouse_x = event.motion.x;
            input->mouse_y = event.motion.y;
            input->mouse_dx = event.motion.xrel;
            input->mouse_dy = event.motion.yrel;
            break;

        case SDL_MOUSEBUTTONDOWN:
            if( event.button.button == SDL_BUTTON_LEFT )
            {
                input->is_mouse_left_down = true;
            }
            else if( event.button.button == SDL_BUTTON_RIGHT )
            {
                input->is_mouse_right_down = true;
            }
            else if( event.button.button == SDL_BUTTON_MIDDLE )
            {
                input->is_mouse_middle_down = true;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if( event.button.button == SDL_BUTTON_LEFT )
            {
                input->is_mouse_left_down = false;
            }
            else if( event.button.button == SDL_BUTTON_RIGHT )
            {
                input->is_mouse_right_down = false;
            }
            else if( event.button.button == SDL_BUTTON_MIDDLE )
            {
                input->is_mouse_middle_down = false;
            }
            break;

        case SDL_MOUSEWHEEL:
            input->mouse_wheel_delta = event.wheel.y;
            break;

        default:
            break;
        }
    }
}

