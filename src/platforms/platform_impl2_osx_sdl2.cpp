#include "platform_impl2_osx_sdl2.h"

extern "C" {
#include "osrs/gio.h"
#include "osrs/gio_assets.h"
#include "osrs/gio_cache.h"
#include "osrs/grender.h"
}

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Platform2_OSX_SDL2*
Platform2_OSX_SDL2_New(void)
{
    struct Platform2_OSX_SDL2* platform =
        (struct Platform2_OSX_SDL2*)malloc(sizeof(struct Platform2_OSX_SDL2));
    memset(platform, 0, sizeof(struct Platform2_OSX_SDL2));
    return platform;
}

void
Platform2_OSX_SDL2_Free(struct Platform2_OSX_SDL2* platform)
{
    free(platform);
}

bool
Platform2_OSX_SDL2_InitForSoft3D(
    struct Platform2_OSX_SDL2* platform, int screen_width, int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->last_frame_time_ticks = SDL_GetTicks64();
    return true;
}

void
Platform2_OSX_SDL2_PollEvents(struct Platform2_OSX_SDL2* platform, struct GInput* input)
{
    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;

    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Process ImGui events first
        ImGui_ImplSDL2_ProcessEvent(&event);

        if( event.type == SDL_QUIT )
        {
            input->quit = true;
        }
        else if( event.type == SDL_WINDOWEVENT )
        {
            if( event.window.event == SDL_WINDOWEVENT_RESIZED )
            {
                SDL_GetWindowSize(
                    platform->window, &platform->window_width, &platform->window_height);
                // Since we don't have SDL renderer, drawable size = window size
                platform->drawable_width = platform->window_width;
                platform->drawable_height = platform->window_height;

                // Update ImGui display size for proper scaling
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize =
                    ImVec2((float)platform->drawable_width, (float)platform->drawable_height);
            }
        }
        else if( event.type == SDL_MOUSEMOTION )
        {
            // transform_mouse_coordinates(
            //     event.motion.x, event.motion.y, &input->mouse_x, &input->mouse_y, platform);
        }
        else if( event.type == SDL_MOUSEBUTTONDOWN )
        {
            // int transformed_x, transformed_y;
            // transform_mouse_coordinates(
            //     event.button.x, event.button.y, &transformed_x, &transformed_y, &platform);
            // input->mouse_click_x = transformed_x;
            // input->mouse_click_y = transformed_y;
            // input->mouse_click_cycle = 0;
        }
        else if( event.type == SDL_KEYDOWN )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_ESCAPE:
                input->quit = true;
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
            case SDLK_s:
                input->s_pressed = 1;
                break;
            case SDLK_w:
                input->w_pressed = 1;
                break;
            case SDLK_d:
                input->d_pressed = 1;
                break;
            case SDLK_a:
                input->a_pressed = 1;
                break;
            case SDLK_r:
                input->r_pressed = 1;
                break;
            case SDLK_f:
                input->f_pressed = 1;
                break;
            case SDLK_SPACE:
                input->space_pressed = 1;
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
            case SDLK_q:
                input->q_pressed = 1;
                break;
            case SDLK_e:
                input->e_pressed = 1;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 1;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 1;
                break;
            }
        }
        else if( event.type == SDL_KEYUP )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_s:
                input->s_pressed = 0;
                break;
            case SDLK_w:
                input->w_pressed = 0;
                break;
            case SDLK_d:
                input->d_pressed = 0;
                break;
            case SDLK_a:
                input->a_pressed = 0;
                break;
            case SDLK_r:
                input->r_pressed = 0;
                break;
            case SDLK_f:
                input->f_pressed = 0;
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
            case SDLK_SPACE:
                input->space_pressed = 0;
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
            case SDLK_PERIOD:
                input->period_pressed = 0;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 0;
                break;
            case SDLK_q:
                input->q_pressed = 0;
                break;
            case SDLK_e:
                input->e_pressed = 0;
                break;
            }
        }
    }
}

// TODO: Clean this up?

void
Platform2_OSX_SDL2_PollIO(struct Platform2_OSX_SDL2* platform, struct GIOQueue* queue)
{
    struct GIOMessage message = { 0 };
    while( gioqb_read_next(queue, &message) )
    {
        switch( message.kind )
        {
        case GIO_REQ_INIT:
        {
            switch( message.status )
            {
            case GIO_STATUS_PENDING:
                platform->cache = gioqb_cache_new();
                gioqb_mark_done(
                    queue,
                    message.message_id,
                    message.command,
                    message.param_b,
                    message.param_a,
                    NULL,
                    0);
                break;
            case GIO_STATUS_DONE:
                break;
            case GIO_STATUS_INFLIGHT:
                break;
            case GIO_STATUS_FINALIZED:
                gioqb_remove(queue, &message);
                break;
            case GIO_STATUS_ERROR:
                gioqb_remove(queue, &message);
                break;
            }
        }
        break;
        case GIO_REQ_ASSET:
        {
            switch( message.status )
            {
            case GIO_STATUS_PENDING:
            {
                if( message.command == ASSET_MODELS )
                {
                    struct CacheModel* model =
                        gioqb_cache_model_new_load(platform->cache, message.param_b);
                    gioqb_mark_done(
                        queue,
                        message.message_id,
                        message.command,
                        message.param_b,
                        message.param_a,
                        model,
                        sizeof(struct CacheModel));
                }
                else
                {
                    printf("Unknown asset command: %d\n", message.command);
                    assert(false && "Unknown asset command");
                }

                break;
            }
            break;
            case GIO_STATUS_DONE:
                break;
            case GIO_STATUS_INFLIGHT:
                break;
            case GIO_STATUS_FINALIZED:
                gioqb_remove(queue, &message);
                break;
            case GIO_STATUS_ERROR:
                gioqb_remove(queue, &message);
                break;
            }
        }

        break;
        }
    }
}