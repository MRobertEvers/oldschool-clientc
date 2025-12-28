#include "platform_impl2_osx_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "graphics/dash.h"
#include "libg.h"
}

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
render_imgui(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer, struct GGame* game)
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Info window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);

    ImGui::Begin("Info");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    Uint64 frequency = SDL_GetPerformanceFrequency();
    // ImGui::Text(
    //     "Render Time: %.3f ms/frame",
    //     (double)(game->end_time - game->start_time) * 1000.0 / (double)frequency);
    // ImGui::Text(
    //     "Average Render Time: %.3f ms/frame, %.3f ms/frame, %.3f ms/frame",
    //     (double)(game->frame_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->painters_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->texture_upload_time_sum / game->frame_count) * 1000.0 /
    //     (double)frequency);
    // ImGui::Text("Mouse (x, y): %d, %d", game->mouse_x, game->mouse_y);

    // ImGui::Text("Hover model: %d, %d", game->hover_model, game->hover_loc_yaw);
    // ImGui::Text(
    //     "Hover loc: %d, %d, %d", game->hover_loc_x, game->hover_loc_y, game->hover_loc_level);

    // Camera position with copy button

    // Camera rotation with copy button
    // Add camera speed slider
    ImGui::Separator();
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer->renderer);
}

struct Platform2_OSX_SDL2_Renderer_Soft3D*
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(int width, int height, int max_width, int max_height)
{
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer =
        (struct Platform2_OSX_SDL2_Renderer_Soft3D*)malloc(
            sizeof(struct Platform2_OSX_SDL2_Renderer_Soft3D));
    memset(renderer, 0, sizeof(struct Platform2_OSX_SDL2_Renderer_Soft3D));

    renderer->width = width;
    renderer->height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;

    renderer->pixel_buffer = (int*)malloc(max_width * max_height * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, width * height * sizeof(int));

    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer)
{
    free(renderer);
}

bool
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer, struct Platform2_OSX_SDL2* platform)
{
    renderer->platform = platform;

    renderer->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("Failed to create texture\n");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, renderer->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(renderer->renderer) )
    {
        printf("ImGui Renderer init failed\n");
        return false;
    }

    return true;
}

void PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Shutdown(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct GRenderCommandBuffer* render_command_buffer)
{
    // Handle window resize: update renderer dimensions up to max size
    int window_width, window_height;
    SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);

    // Clamp to maximum renderer size (pixel buffer allocation limit)
    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    // Only update if size changed
    if( new_width != renderer->width || new_height != renderer->height )
    {
        renderer->width = new_width;
        renderer->height = new_height;

        // Recreate texture with new dimensions
        if( renderer->texture )
        {
            SDL_DestroyTexture(renderer->texture);
        }

        renderer->texture = SDL_CreateTexture(
            renderer->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            renderer->width,
            renderer->height);

        if( !renderer->texture )
        {
            printf(
                "Failed to recreate texture for new size %dx%d\n",
                renderer->width,
                renderer->height);
        }
        else
        {
            printf(
                "Window resized to %dx%d (max: %dx%d)\n",
                renderer->width,
                renderer->height,
                renderer->max_width,
                renderer->max_height);
        }
    }

    // memset(renderer->pixel_buffer, 0x00FF00FF, renderer->width * renderer->height * sizeof(int));
    for( int y = 0; y < renderer->height; y++ )
        memset(
            &renderer->pixel_buffer[y * renderer->width],
            0x00FF00FF,
            renderer->width * sizeof(int));

    // struct AABB aabb;
    struct GRenderCommand* command = NULL;

    if( game->tasks_nullable == NULL )
        painter_paint(
            game->sys_painter,
            game->sys_painter_buffer,
            game->camera_world_x / 128,
            game->camera_world_z / 128,
            game->camera_world_y / 240);

    struct DashPosition position = { 0 };

    for( int i = 0; i < game->sys_painter_buffer->command_count; i++ )
    {
        struct PaintersElementCommand* cmd = &game->sys_painter_buffer->commands[i];

        switch( cmd->_bf_kind )
        {
        case PNTR_CMD_ELEMENT:
        {
            struct SceneElement* scene_element =
                (struct SceneElement*)vec_get(game->scene_elements, cmd->_entity._bf_entity);
            memcpy(&position, scene_element->position, sizeof(struct DashPosition));
            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;
            dash3d_render_model(
                game->sys_dash,
                scene_element->model,
                &position,
                game->view_port,
                game->camera,
                renderer->pixel_buffer);
        }
        break;
        case PNTR_CMD_TERRAIN:
        {
            struct TerrainTileModel* tile_model = NULL;
            int sx = cmd->_terrain._bf_terrain_x;
            int sz = cmd->_terrain._bf_terrain_z;
            int slevel = cmd->_terrain._bf_terrain_y;
            tile_model = terrain_tile_model_at(game->terrain, sx, sz, slevel);
            position.x = sx * 128 - game->camera_world_x;
            position.z = sz * 128 - game->camera_world_z;
            position.y = slevel * 240 - game->camera_world_y;
            dash3d_render_model(
                game->sys_dash,
                &tile_model->dash_model,
                &position,
                game->view_port,
                game->camera,
                renderer->pixel_buffer);
        }
        break;
        default:
            break;
        }
    }
    // for( int i = 0; i < vec_size(game->scene_elements); i++ )
    // {
    //     struct SceneElement* scene_element = (struct SceneElement*)vec_get(game->scene_elements,
    //     i); memcpy(&position, scene_element->position, sizeof(struct DashPosition)); position.x =
    //     position.x - game->camera_world_x; position.y = position.y - game->camera_world_y;
    //     position.z = position.z - game->camera_world_z;
    //     dash3d_render_model(
    //         game->sys_dash,
    //         scene_element->model,
    //         &position,
    //         game->view_port,
    //         game->camera,
    //         renderer->pixel_buffer);
    // }

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;
    for( int i = 0; i < grendercb_count(render_command_buffer); i++ )
    {
        command = grendercb_at(render_command_buffer, i);
        switch( command->kind )
        {
        case GRENDER_CMD_MODEL_DRAW:
        {
            position = *game->position;
            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;
            dash3d_render_model(
                game->sys_dash,
                game->model,
                &position,
                game->view_port,
                game->camera,
                renderer->pixel_buffer);
            // render_model_frame(
            //     renderer->pixel_buffer,
            //     renderer->width,
            //     renderer->height,
            //     50,
            //     0,
            //     0,
            //     0,
            //     -game->camera_world_x,
            //     -game->camera_world_y,
            //     -game->camera_world_z,
            //     game->camera_pitch,
            //     game->camera_yaw,
            //     game->camera_roll,
            //     512,
            //     &aabb,
            //     game->model,
            //     game->lighting,
            //     game->bounds_cylinder,
            //     NULL,
            //     NULL,
            //     NULL,
            //     NULL);
        }
        break;
        }
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int _pitch_unused = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        return;

    int row_size = renderer->width * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        // Calculate offset in texture to write a single row of pixels
        int* row = &pix_write[(src_y * renderer->width)];
        // Copy a single row of pixels
        memcpy(row, &src_pixels[(src_y - 0) * renderer->width], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // window_width and window_height already retrieved at the top of function
    // Calculate destination rectangle to scale the texture to fill the window
    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = renderer->width;
    dst_rect.h = renderer->height;

    // Use nearest neighbor (point) filtering when window is larger than rendered size
    // This maintains crisp pixels when scaling up beyond max renderer size
    if( window_width > renderer->width || window_height > renderer->height )
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest neighbor (point sampling)
    }
    else
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Bilinear when scaling down
    }

    // Calculate aspect ratio preserving dimensions
    float src_aspect = (float)renderer->width / (float)renderer->height;
    float window_aspect = (float)window_width / (float)window_height;

    if( src_aspect > window_aspect )
    {
        // Renderer is wider - fit to window width
        dst_rect.w = window_width;
        dst_rect.h = (int)(window_width / src_aspect);
        dst_rect.x = 0;
        dst_rect.y = (window_height - dst_rect.h) / 2;
    }
    else
    {
        // Renderer is taller - fit to window height
        dst_rect.h = window_height;
        dst_rect.w = (int)(window_height * src_aspect);
        dst_rect.y = 0;
        dst_rect.x = (window_width - dst_rect.w) / 2;
    }

    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);
    SDL_FreeSurface(surface);

    render_imgui(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}