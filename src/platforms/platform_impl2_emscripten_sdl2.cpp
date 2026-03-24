#include "platform_impl2_emscripten_sdl2.h"

// #include "osrs/rscache/cache_dat.h"
// #include "tori_rs.h"

#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include ImGui SDL backend to process events (C++)
#include "imgui.h"
#include "imgui_impl_sdl2.h"

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_Emscripten_SDL2* platform)
{
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect =
        (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        float relative_x = (float)(window_mouse_x - dst_x) / (float)dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / (float)dst_h;

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

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

    platform->window_width = canvas_width;
    platform->window_height = canvas_height;
    platform->drawable_width = canvas_width;
    platform->drawable_height = canvas_height;
    platform->game_screen_width = canvas_width;
    platform->game_screen_height = canvas_height;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
Platform2_Emscripten_SDL2_InitForWebGL1(
    struct Platform2_Emscripten_SDL2* platform,
    int canvas_width,
    int canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    platform->window = SDL_CreateWindow(
        "3D Raster - WebGL1",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window_width = canvas_width;
    platform->window_height = canvas_height;
    platform->drawable_width = canvas_width;
    platform->drawable_height = canvas_height;
    platform->game_screen_width = canvas_width;
    platform->game_screen_height = canvas_height;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
Platform2_Emscripten_SDL2_SyncCanvasCssSize(
    struct Platform2_Emscripten_SDL2* platform,
    struct GGame* game_nullable)
{
    if( !platform )
        return;

    double css_width = 0.0;
    double css_height = 0.0;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);
    int new_w = (int)css_width;
    int new_h = (int)css_height;
    if( new_w <= 0 || new_h <= 0 )
        return;

    if( new_w == platform->window_width && new_h == platform->window_height )
        return;

    platform->window_width = new_w;
    platform->window_height = new_h;
    platform->drawable_width = new_w;
    platform->drawable_height = new_h;

    if( platform->window )
        SDL_SetWindowSize(platform->window, new_w, new_h);

    emscripten_set_canvas_element_size("#canvas", new_w, new_h);

    if( ImGui::GetCurrentContext() != nullptr )
        ImGui::GetIO().DisplaySize = ImVec2((float)new_w, (float)new_h);

    if( game_nullable && game_nullable->iface_view_port )
    {
        struct DashViewPort* ivp = game_nullable->iface_view_port;
        ivp->x_center = new_w / 2;
        ivp->y_center = new_h / 2;
        ivp->width = new_w;
        ivp->height = new_h;
        ivp->stride = new_w;
        ivp->clip_left = 0;
        ivp->clip_top = 0;
        ivp->clip_right = new_w;
        ivp->clip_bottom = new_h;
    }
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

    Platform2_Emscripten_SDL2_SyncCanvasCssSize(platform, platform->current_game);

    input->mouse_clicked = 0;
    input->mouse_clicked_x = -1;
    input->mouse_clicked_y = -1;
    input->mouse_clicked_right = 0;
    input->mouse_clicked_right_x = -1;
    input->mouse_clicked_right_y = -1;

    {
        Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
        input->mouse_button_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
    }

    /* Advance input time accumulator based on real frame time, so
     * LibToriRS_GameProcessInput can step movement in fixed quanta. */
    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Forward events to ImGui only if backend was initialized.
        if( ImGui::GetCurrentContext() != nullptr )
            ImGui_ImplSDL2_ProcessEvent(&event);

        bool imgui_wants_mouse = false;
        if( ImGui::GetCurrentContext() != nullptr )
            imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;

        switch( event.type )
        {
        case SDL_QUIT:
            input->quit = 1;
            break;

        case SDL_WINDOWEVENT:
            if( event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED )
            {
                if( platform->window )
                {
                    SDL_GetWindowSize(
                        platform->window, &platform->window_width, &platform->window_height);
                    platform->drawable_width = platform->window_width;
                    platform->drawable_height = platform->window_height;
                }
                if( !imgui_wants_mouse && platform->window )
                {
                    int mx = 0;
                    int my = 0;
                    SDL_GetMouseState(&mx, &my);
                    transform_mouse_coordinates(mx, my, &input->mouse_x, &input->mouse_y, platform);
                }
            }
            break;

        case SDL_MOUSEMOTION:
            if( !imgui_wants_mouse )
            {
                transform_mouse_coordinates(
                    event.motion.x,
                    event.motion.y,
                    &input->mouse_x,
                    &input->mouse_y,
                    platform);
            }
            else
            {
                input->mouse_x = -1;
                input->mouse_y = -1;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if( !imgui_wants_mouse && event.button.button == SDL_BUTTON_LEFT )
            {
                input->mouse_button_down = 1;
                input->mouse_clicked = 1;
                transform_mouse_coordinates(
                    event.button.x,
                    event.button.y,
                    &input->mouse_clicked_x,
                    &input->mouse_clicked_y,
                    platform);
            }
            else if( !imgui_wants_mouse && event.button.button == SDL_BUTTON_RIGHT )
            {
                input->mouse_clicked_right = 1;
                transform_mouse_coordinates(
                    event.button.x,
                    event.button.y,
                    &input->mouse_clicked_right_x,
                    &input->mouse_clicked_right_y,
                    platform);
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if( event.button.button == SDL_BUTTON_LEFT )
                input->mouse_button_down = 0;
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

    /* Keep game mouse coords in sync when the canvas resizes without a motion event. */
    if( ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse )
    {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        transform_mouse_coordinates(mx, my, &input->mouse_x, &input->mouse_y, platform);
    }
    else
    {
        input->mouse_x = -1;
        input->mouse_y = -1;
    }
}

static void
send_lua_game_script_to_js(struct LuaGameScript* script)
{
    // clang-format off
    EM_ASM(
        {
            if (typeof window.done === 'undefined') {
                window.done = false;
            }
            
            var script = $0; // Convert C++ pointer to JS LuaGameScript
            window.LUA_SCRIPT_QUEUE = window.LUA_SCRIPT_QUEUE || [];
            window.LUA_SCRIPT_QUEUE.push(script); // Add to queue
        },
        script);

    // clang-format on
}

void
Platform2_Emscripten_SDL2_RunLuaScripts(struct Platform2_Emscripten_SDL2* platform)
{
    struct LuaGameScript* script = NULL;
    int script_status = 0;

    while( !LibToriRS_LuaScriptQueueIsEmpty(platform->current_game) )
    {
        script = LuaGameScript_New();
        LibToriRS_LuaScriptQueuePop(platform->current_game, script);

        send_lua_game_script_to_js(script);
    }
}