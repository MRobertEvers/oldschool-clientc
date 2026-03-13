#include "platform_impl2_emscripten_sdl2.h"

// #include "osrs/rscache/cache_dat.h"
// #include "tori_rs.h"

#include <SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include ImGui SDL backend to process events (C++)
#include "imgui_impl_sdl2.h"

struct Platform2_Emscripten_SDL2*
Platform2_Emscripten_SDL2_New(void)
{
    struct Platform2_Emscripten_SDL2* platform =
        (struct Platform2_Emscripten_SDL2*)malloc(sizeof(struct Platform2_Emscripten_SDL2));
    memset(platform, 0, sizeof(struct Platform2_Emscripten_SDL2));

    platform->input = (struct GInput*)malloc(sizeof(struct GInput));
    memset(platform->input, 0, sizeof(struct GInput));

    // platform->game = LibToriRS_GameNew(NULL, 513, 335);
    // platform->render_command_buffer = LibToriRS_RenderCommandBufferNew(1024);

    return platform;
}

void
Platform2_Emscripten_SDL2_Free(struct Platform2_Emscripten_SDL2* platform)
{
    if( platform->input )
        free(platform->input);
    free(platform);
}

bool
Platform2_Emscripten_SDL2_InitForSoft3D(
    struct Platform2_Emscripten_SDL2* platform,
    int canvas_width,
    int canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "3D Raster - Soft3D",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_SHOWN |
            SDL_WINDOW_RESIZABLE); // Use SHOWN instead of OPENGL for Emscripten - context created
                                   // separately, RESIZABLE allows full-width canvas
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
Platform2_Emscripten_SDL2_Shutdown(struct Platform2_Emscripten_SDL2* platform)
{
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
}

void
Platform2_Emscripten_SDL2_PollEvents(struct Platform2_Emscripten_SDL2* platform)
{
    struct GInput* input = platform->input;
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Forward events to ImGui for processing (mouse, keyboard, etc.)
        // ImGui_ImplSDL2_ProcessEvent(&event);

        switch( event.type )
        {
        case SDL_QUIT:
            input->quit = 1;
            break;

        case SDL_KEYDOWN:
        {
            SDL_Keycode key = event.key.keysym.sym;
            switch( key )
            {
            case SDLK_w:
                input->w_pressed = 1;
                break;
            case SDLK_a:
                input->a_pressed = 1;
                break;
            case SDLK_s:
                input->s_pressed = 1;
                break;
            case SDLK_d:
                input->d_pressed = 1;
                break;
            case SDLK_q:
                input->q_pressed = 1;
                break;
            case SDLK_e:
                input->e_pressed = 1;
                break;
            case SDLK_SPACE:
                input->space_pressed = 1;
                break;
            case SDLK_UP:
                input->up_pressed = 1;
                break;
            case SDLK_DOWN:
                input->down_pressed = 1;
                break;
            case SDLK_LEFT:
                input->left_pressed = 1;
                break;
            case SDLK_RIGHT:
                input->right_pressed = 1;
                break;
            case SDLK_f:
                input->f_pressed = 1;
                break;
            case SDLK_r:
                input->r_pressed = 1;
                break;
            case SDLK_m:
                input->m_pressed = 1;
                break;
            case SDLK_n:
                input->n_pressed = 1;
                break;
            case SDLK_i:
                input->i_pressed = 1;
                break;
            case SDLK_k:
                input->k_pressed = 1;
                break;
            case SDLK_l:
                input->l_pressed = 1;
                break;
            case SDLK_j:
                input->j_pressed = 1;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 1;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 1;
                break;
            case SDLK_ESCAPE:
                input->quit = 1;
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
                input->w_pressed = 0;
                break;
            case SDLK_a:
                input->a_pressed = 0;
                break;
            case SDLK_s:
                input->s_pressed = 0;
                break;
            case SDLK_d:
                input->d_pressed = 0;
                break;
            case SDLK_q:
                input->q_pressed = 0;
                break;
            case SDLK_e:
                input->e_pressed = 0;
                break;
            case SDLK_SPACE:
                input->space_pressed = 0;
                break;
            case SDLK_UP:
                input->up_pressed = 0;
                break;
            case SDLK_DOWN:
                input->down_pressed = 0;
                break;
            case SDLK_LEFT:
                input->left_pressed = 0;
                break;
            case SDLK_RIGHT:
                input->right_pressed = 0;
                break;
            case SDLK_f:
                input->f_pressed = 0;
                break;
            case SDLK_r:
                input->r_pressed = 0;
                break;
            case SDLK_m:
                input->m_pressed = 0;
                break;
            case SDLK_n:
                input->n_pressed = 0;
                break;
            case SDLK_i:
                input->i_pressed = 0;
                break;
            case SDLK_k:
                input->k_pressed = 0;
                break;
            case SDLK_l:
                input->l_pressed = 0;
                break;
            case SDLK_j:
                input->j_pressed = 0;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 0;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 0;
                break;
            default:
                break;
            }
            break;
        }

        default:
            break;
        }
    }
}

static void
send_string_to_js(const char* str)
{
    // clang-format off
    EM_ASM(
        {
            if (typeof window.done === 'undefined') {
                window.done = false;
            }
            
            var str = UTF8ToString($0); // Convert C++ pointer to JS String
            window.LUA_SCRIPT_QUEUE = window.LUA_SCRIPT_QUEUE || [];
            if( window.LUA_SCRIPT_QUEUE.length === 0 && !window.done )
            {
                window.LUA_SCRIPT_QUEUE.push(str); // Add to queue
                window.done = true; // Prevent multiple pushes until loop processes it
            }  
        },
        str);

    // clang-format on
}

void
Platform2_Emscripten_SDL2_RunLuaScripts(struct Platform2_Emscripten_SDL2* platform)
{
    // In a real implementation, you'd have some way to get Lua scripts to run.
    // For this example, we'll just send a test script every frame.
    const char* test_script = "test.lua";
    send_string_to_js(test_script);
}