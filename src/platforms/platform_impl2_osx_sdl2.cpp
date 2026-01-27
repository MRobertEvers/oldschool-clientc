#include "platform_impl2_osx_sdl2.h"

extern "C" {
#include "osrs/filepack.h"
#include "osrs/gio.h"
#include "osrs/gio_assets.h"
#include "osrs/gio_cache.h"
#include "osrs/gio_cache_dat.h"
#include "tori_rs_render.h"
}

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_OSX_SDL2* platform)
{
    // Calculate the same scaling transformation as the rendering
    // Use the fixed game screen dimensions, not the window dimensions
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect = (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    // Transform window coordinates to game coordinates
    // Account for the offset and scaling of the game rendering area
    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        // Mouse is outside the game rendering area
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        // Transform from window coordinates to game coordinates
        float relative_x = (float)(window_mouse_x - dst_x) / dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / dst_h;

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

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
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height)
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

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    // Store game screen dimensions in drawable_width/drawable_height
    // (these represent the game's logical screen size, not the actual drawable size)
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;

    // Store fixed game screen dimensions for mouse coordinate transformation
    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;

    platform->last_frame_time_ticks = SDL_GetTicks64();

    platform->cache_dat = gioqb_cache_dat_new();
    return true;
}

void
Platform2_OSX_SDL2_PollEvents(
    struct Platform2_OSX_SDL2* platform,
    struct GInput* input)
{
    input->mouse_clicked = 0;
    input->mouse_clicked_x = -1;
    input->mouse_clicked_y = -1;

    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;

    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Process ImGui events first
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Check if ImGui wants to capture mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool imgui_wants_mouse = io.WantCaptureMouse;

        if( event.type == SDL_QUIT )
        {
            input->quit = true;
        }
        else if( event.type == SDL_WINDOWEVENT )
        {
            if( event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED )
            {
                SDL_GetWindowSize(
                    platform->window, &platform->window_width, &platform->window_height);
                // Since we don't have SDL renderer, drawable size = window size
                platform->drawable_width = platform->window_width;
                platform->drawable_height = platform->window_height;

                // Update ImGui display size for proper scaling
                io.DisplaySize =
                    ImVec2((float)platform->drawable_width, (float)platform->drawable_height);

                // Recalculate mouse coordinates after resize
                int current_mouse_x, current_mouse_y;
                SDL_GetMouseState(&current_mouse_x, &current_mouse_y);
                transform_mouse_coordinates(
                    current_mouse_x, current_mouse_y, &input->mouse_x, &input->mouse_y, platform);
            }
        }
        else if( event.type == SDL_MOUSEMOTION )
        {
            if( !imgui_wants_mouse )
            {
                transform_mouse_coordinates(
                    event.motion.x, event.motion.y, &input->mouse_x, &input->mouse_y, platform);
            }
            else
            {
                // Mouse is over ImGui, mark as outside game area
                input->mouse_x = -1;
                input->mouse_y = -1;
            }
        }
        else if( event.type == SDL_MOUSEBUTTONDOWN )
        {
            if( !imgui_wants_mouse )
            {
                // event.button.x,
                // event.button.y,
                // input->mouse_clicked_x = event.button.x;
                // input->mouse_clicked_y = event.button.y;
                // Mouse click coordinates are stored in input if needed
                // For now, we just update the mouse position
                input->mouse_clicked = 1;
                transform_mouse_coordinates(
                    event.button.x,
                    event.button.y,
                    &input->mouse_clicked_x,
                    &input->mouse_clicked_y,
                    platform);
            }
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

static void
on_gio_req_init(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* io,
    struct GIOMessage* message)
{
    assert(message->command == GIO_REQ_INIT);
    switch( message->status )
    {
    case GIO_STATUS_PENDING:
    {
        // platform->cache = gioqb_cache_new();
        platform->cache_dat = gioqb_cache_dat_new();
        gioqb_mark_done(
            io, message->message_id, message->command, message->param_b, message->param_a, NULL, 0);
    }
    break;
    case GIO_STATUS_DONE:
        break;
    case GIO_STATUS_INFLIGHT:
        break;
    case GIO_STATUS_FINALIZED:
        break;
    case GIO_STATUS_ERROR:
        assert(false && "GIO_STATUS_ERROR in on_gio_req_init");
        break;
    }
}

static void
on_gio_req_asset(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* io,
    struct GIOMessage* message)
{
    switch( message->status )
    {
    case GIO_STATUS_PENDING:
        if( platform->cache != NULL )
            gioqb_cache_fullfill(io, platform->cache, message);
        if( platform->cache_dat != NULL )
            gioqb_cache_dat_fullfill(io, platform->cache_dat, message);
        break;
    case GIO_STATUS_DONE:
        break;
    case GIO_STATUS_INFLIGHT:
        break;
    case GIO_STATUS_FINALIZED:
        break;
    case GIO_STATUS_ERROR:
        break;
    }
}

void
Platform2_OSX_SDL2_PollIO(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* queue)
{
    struct GIOMessage message = { 0 };
    while( gioqb_read_next(queue, &message) )
    {
        switch( message.kind )
        {
        case GIO_REQ_INIT:
            on_gio_req_init(platform, queue, &message);
            break;
        case GIO_REQ_ASSET:
            on_gio_req_asset(platform, queue, &message);
            break;
        default:
            assert(false && "Unknown GIO request kind");
            break;
        }
    }

    gioqb_remove_finalized(queue);
}