#include "platform_impl2_osx_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "graphics/dash.h"
#include "libg.h"
#include "osrs/minimap.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene.h"
}

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

static void
render_imgui(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Info window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

    ImGui::Begin("Info");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    Uint64 frequency = SDL_GetPerformanceFrequency();

    ImGui::Text("Buck: %d", game->cc);
    ImGui::Text("Trap command: %d", g_trap_command);
    if( ImGui::Button("Trap command") )
    {
        if( g_trap_command == -1 )
            g_trap_command = game->cc;
        else
            g_trap_command = -1;
    }

    ImGui::InputInt("Trap X", &g_trap_x);
    ImGui::InputInt("Trap Z", &g_trap_z);

    // ImGui::Text(
    //     "Render Time: %.3f ms/frame",
    //     (double)(game->end_time - game->start_time) * 1000.0 / (double)frequency);
    // ImGui::Text(
    //     "Average Render Time: %.3f ms/frame, %.3f ms/frame, %.3f ms/frame",
    //     (double)(game->frame_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->painters_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->texture_upload_time_sum / game->frame_count) * 1000.0 /
    //     (double)frequency);

    // Display mouse position (game screen coordinates)
    ImGui::Text("Mouse (game x, y): %d, %d", game->mouse_x, game->mouse_y);

    // Also show window mouse position for debugging
    if( renderer->platform && renderer->platform->window )
    {
        int window_mouse_x, window_mouse_y;
        SDL_GetMouseState(&window_mouse_x, &window_mouse_y);
        ImGui::Text("Mouse (window x, y): %d, %d", window_mouse_x, window_mouse_y);
    }

    // ImGui::Text("Hover model: %d, %d", game->hover_model, game->hover_loc_yaw);
    // ImGui::Text(
    //     "Hover loc: %d, %d, %d", game->hover_loc_x, game->hover_loc_y, game->hover_loc_level);

    // Camera position with copy button
    char camera_pos_text[256];
    snprintf(
        camera_pos_text,
        sizeof(camera_pos_text),
        "Camera (x, y, z): %d, %d, %d : %d, %d",
        game->camera_world_x,
        game->camera_world_y,
        game->camera_world_z,
        game->camera_world_x / 128,
        game->camera_world_z / 128);
    ImGui::Text("%s", camera_pos_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##pos") )
    {
        ImGui::SetClipboardText(camera_pos_text);
    }

    // Camera rotation with copy button
    char camera_rot_text[256];
    snprintf(
        camera_rot_text,
        sizeof(camera_rot_text),
        "Camera (pitch, yaw, roll): %d, %d, %d",
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll);
    ImGui::Text("%s", camera_rot_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##rot") )
    {
        ImGui::SetClipboardText(camera_rot_text);
    }

    // Add camera speed slider
    ImGui::Separator();
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer->renderer);
}

struct Platform2_OSX_SDL2_Renderer_Soft3D*
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
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

    // Initialize dash buffer (will be allocated when viewport is known)
    renderer->dash_buffer = NULL;
    renderer->dash_buffer_width = 0;
    renderer->dash_buffer_height = 0;
    renderer->dash_offset_x = 0;
    renderer->dash_offset_y = 0;

    // Initialize minimap buffer (will be allocated when needed)
    renderer->minimap_buffer = NULL;
    renderer->minimap_buffer_width = 0;
    renderer->minimap_buffer_height = 0;

    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer)
{
    if( renderer->dash_buffer )
    {
        free(renderer->dash_buffer);
    }
    if( renderer->minimap_buffer )
    {
        free(renderer->minimap_buffer);
    }
    free(renderer);
}

bool
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_OSX_SDL2* platform)
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

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Shutdown(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y)
{
    if( renderer )
    {
        renderer->dash_offset_x = offset_x;
        renderer->dash_offset_y = offset_y;
    }
}

static void
blit_rotated_buffer(
    int* src_buffer,
    int src_width,
    int src_height,
    int* dst_buffer,
    int dst_stride,
    int dst_width,
    int dst_height,
    int dst_x,
    int dst_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int angle_r2pi2048)
{
    int sin = dash_sin(angle_r2pi2048);
    int cos = dash_cos(angle_r2pi2048);

    // Calculate the bounding box of the rotated minimap
    // We'll iterate through a square region that covers the rotated minimap
    // The bounding box is centered around the destination anchor point (rotation center)
    int half_size = (src_width > src_height ? src_width : src_height) / 2;
    int min_x = dst_anchor_x - half_size;
    int min_y = dst_anchor_y - half_size;
    int max_x = dst_anchor_x + half_size;
    int max_y = dst_anchor_y + half_size;

    // Clamp to destination bounds
    if( min_x < 0 )
        min_x = 0;
    if( min_y < 0 )
        min_y = 0;
    if( max_x > dst_width )
        max_x = dst_width;
    if( max_y > dst_height )
        max_y = dst_height;

    // Iterate through destination pixels in the rotated region
    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            // Calculate position relative to destination anchor point
            // When dst_x_abs == dst_anchor_x and dst_y_abs == dst_anchor_y, we want to sample
            // from src_anchor_x, src_anchor_y to ensure the source anchor coincides with dest
            // anchor
            int rel_x = dst_x_abs - dst_anchor_x;
            int rel_y = dst_y_abs - dst_anchor_y;

            // Apply inverse rotation to find which source pixel corresponds to this destination
            // pixel Forward rotation: a source point (sx_rel, sy_rel) relative to src_anchor maps
            // to destination (dx_rel, dy_rel) relative to dst_anchor:
            //   dx_rel = sx_rel*cos(θ) - sy_rel*sin(θ)
            //   dy_rel = sx_rel*sin(θ) + sy_rel*cos(θ)
            //
            // To invert (find source from destination), use inverse rotation matrix:
            //   sx_rel = dx_rel*cos(θ) + dy_rel*sin(θ)
            //   sy_rel = -dx_rel*sin(θ) + dy_rel*cos(θ)
            //
            // When dst is at anchor (rel_x=0, rel_y=0): src_rel_x=0, src_rel_y=0
            // So src_x = src_anchor_x, src_y = src_anchor_y
            // This ensures src_anchor pixel is drawn at dst_anchor position
            // Using fixed-point: cos and sin are in 16.16 format (already shifted left by 16)
            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            // Convert to absolute source buffer coordinates
            // The source anchor pixel will be drawn at the destination anchor position
            int src_x = src_anchor_x + src_rel_x;
            int src_y = src_anchor_y + src_rel_y;

            // Check bounds
            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int src_pixel = src_buffer[src_y * src_width + src_x];
                if( src_pixel != 0 )
                {
                    dst_buffer[dst_y_abs * dst_stride + dst_x_abs] = src_pixel;
                }
            }
        }
    }
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct GRenderCommandBuffer* render_command_buffer)
{
    // Ensure viewport center matches viewport dimensions (critical for coordinate transformations)

    // Handle window resize: update renderer dimensions up to max size
    int window_width, window_height;
    SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);

    // Clamp to maximum renderer size (pixel buffer allocation limit)
    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    if( game->iface_view_port )
    {
        game->iface_view_port->x_center = new_width / 2;
        game->iface_view_port->y_center = new_height / 2;
        game->iface_view_port->width = new_width;
        game->iface_view_port->height = new_height;
    }

    // Allocate/update dash buffer if viewport exists
    if( game->view_port )
    {
        // Ensure viewport center matches viewport dimensions (not renderer dimensions)
        // This is critical for coordinate transformations to work correctly
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;

        // Allocate or reallocate dash buffer if size changed
        if( !renderer->dash_buffer || renderer->dash_buffer_width != game->view_port->width ||
            renderer->dash_buffer_height != game->view_port->height )
        {
            if( renderer->dash_buffer )
            {
                free(renderer->dash_buffer);
            }

            renderer->dash_buffer_width = game->view_port->width;
            renderer->dash_buffer_height = game->view_port->height;
            renderer->dash_buffer = (int*)malloc(
                renderer->dash_buffer_width * renderer->dash_buffer_height * sizeof(int));
            if( !renderer->dash_buffer )
            {
                printf("Failed to allocate dash buffer\n");
                return;
            }

            // Set stride to dash buffer width for dash rendering
            game->view_port->stride = renderer->dash_buffer_width;
        }
    }

    // Only update if size changed
    if( new_width != renderer->width || new_height != renderer->height )
    {
        // renderer->width = new_width;
        // renderer->height = new_height;

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
        }
    }

    // Clear main pixel buffer

    for( int y = 0; y < renderer->height; y++ )
        memset(&renderer->pixel_buffer[y * renderer->width], 0x00, renderer->width * sizeof(int));

    // Clear dash buffer if it exists
    if( renderer->dash_buffer )
    {
        for( int y = 0; y < renderer->dash_buffer_height; y++ )
            memset(
                &renderer->dash_buffer[y * renderer->dash_buffer_width],
                0x00,
                renderer->dash_buffer_width * sizeof(int));
    }

    // Allocate minimap buffer if needed (size based on radius * 2 * 4 pixels per tile)
    int minimap_size = 220; // Slightly larger than 25*2*4 = 200 for safety
    if( !renderer->minimap_buffer || renderer->minimap_buffer_width != minimap_size ||
        renderer->minimap_buffer_height != minimap_size )
    {
        if( renderer->minimap_buffer )
        {
            free(renderer->minimap_buffer);
        }

        renderer->minimap_buffer_width = minimap_size;
        renderer->minimap_buffer_height = minimap_size;
        renderer->minimap_buffer = (int*)malloc(
            renderer->minimap_buffer_width * renderer->minimap_buffer_height * sizeof(int));
        if( !renderer->minimap_buffer )
        {
            printf("Failed to allocate minimap buffer\n");
            return;
        }
    }

    // Clear minimap buffer
    for( int y = 0; y < renderer->minimap_buffer_height; y++ )
        memset(
            &renderer->minimap_buffer[y * renderer->minimap_buffer_width],
            0x00,
            renderer->minimap_buffer_width * sizeof(int));

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
    struct SceneElement* element = NULL;

    int interacting_scene_element = -1;

    for( int i = 0; i < game->sys_painter_buffer->command_count && i < game->cc; i++ )
    {
        struct PaintersElementCommand* cmd = &game->sys_painter_buffer->commands[i];
        memset(&position, 0, sizeof(struct DashPosition));

        switch( cmd->_bf_kind )
        {
        case PNTR_CMD_ELEMENT:
        {
            element = scene_element_at(game->scene->scenery, cmd->_entity._bf_entity);
            memcpy(
                &position,
                scene_element_position(game->scene, cmd->_entity._bf_entity),
                sizeof(struct DashPosition));

            // int tile_x = position.x / 128;
            // int tile_z = position.z / 128;
            // if( !(tile_x == 31 && (tile_z == 24 || tile_z == 23 || tile_z == 25)) )
            // {
            //     continue;
            // }

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            if( element->dash_model && element->animation && element->animation->frame_count > 0 )
            {
                dashmodel_animate(
                    scene_element_model(game->scene, cmd->_entity._bf_entity),
                    element->animation->dash_frames[element->animation->frame_index],
                    element->animation->dash_framemap);
            }

            int cull = dash3d_project_model(
                game->sys_dash,
                scene_element_model(game->scene, cmd->_entity._bf_entity),
                &position,
                game->view_port,
                game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            if( (scene_element_interactable(game->scene, cmd->_entity._bf_entity)) )
            {
                // Adjust mouse coordinates for dash buffer offset
                int mouse_x_adjusted = game->mouse_x - renderer->dash_offset_x;
                int mouse_y_adjusted = game->mouse_y - renderer->dash_offset_y;
                if( dash3d_projected_model_contains(
                        game->sys_dash,
                        scene_element_model(game->scene, cmd->_entity._bf_entity),
                        game->view_port,
                        mouse_x_adjusted,
                        mouse_y_adjusted) )
                {
                    // 1637
                    interacting_scene_element = cmd->_entity._bf_entity;
                    // printf(
                    //     "Interactable: %s\n",
                    //     scene_element_name(game->scene, cmd->_entity._bf_entity));

                    // for( struct SceneAction* action = element->actions; action;
                    //      action = action->next )
                    // {
                    //     printf("Action: %s\n", action->action);
                    // }

                    // Draw AABB rectangle outline on dash buffer
                    // AABB coordinates are in screen space relative to viewport center
                    struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);

                    // AABB coordinates are already in viewport space (0 to viewport->width, 0 to
                    // viewport->height)
                    int db_min_x = aabb->min_screen_x;
                    int db_min_y = aabb->min_screen_y;
                    int db_max_x = aabb->max_screen_x;
                    int db_max_y = aabb->max_screen_y;

                    // Draw on dash buffer if it exists
                    if( renderer->dash_buffer )
                    {
                        // Draw top and bottom horizontal lines
                        for( int x = db_min_x; x <= db_max_x; x++ )
                        {
                            if( x >= 0 && x < renderer->dash_buffer_width )
                            {
                                // Top line
                                if( db_min_y >= 0 && db_min_y < renderer->dash_buffer_height )
                                {
                                    renderer
                                        ->dash_buffer[db_min_y * renderer->dash_buffer_width + x] =
                                        0xFFFFFF;
                                }
                                // Bottom line
                                if( db_max_y >= 0 && db_max_y < renderer->dash_buffer_height )
                                {
                                    renderer
                                        ->dash_buffer[db_max_y * renderer->dash_buffer_width + x] =
                                        0xFFFFFF;
                                }
                            }
                        }

                        // Draw left and right vertical lines
                        for( int y = db_min_y; y <= db_max_y; y++ )
                        {
                            if( y >= 0 && y < renderer->dash_buffer_height )
                            {
                                // Left line
                                if( db_min_x >= 0 && db_min_x < renderer->dash_buffer_width )
                                {
                                    renderer
                                        ->dash_buffer[y * renderer->dash_buffer_width + db_min_x] =
                                        0xFFFFFF;
                                }
                                // Right line
                                if( db_max_x >= 0 && db_max_x < renderer->dash_buffer_width )
                                {
                                    renderer
                                        ->dash_buffer[y * renderer->dash_buffer_width + db_max_x] =
                                        0xFFFFFF;
                                }
                            }
                        }
                    }
                }
            }

            dash3d_raster_projected_model(
                game->sys_dash,
                scene_element_model(game->scene, cmd->_entity._bf_entity),
                &position,
                game->view_port,
                game->camera,
                renderer->dash_buffer);
        }
        break;
        case PNTR_CMD_TERRAIN:
        {
            struct SceneTerrainTile* tile_model = NULL;
            int sx = cmd->_terrain._bf_terrain_x;
            int sz = cmd->_terrain._bf_terrain_z;
            int slevel = cmd->_terrain._bf_terrain_y;

            tile_model = scene_terrain_tile_at(game->scene->terrain, sx, sz, slevel);

            position.x = sx * 128 - game->camera_world_x;
            position.z = sz * 128 - game->camera_world_z;
            position.y = -game->camera_world_y;
            dash3d_render_model(
                game->sys_dash,
                tile_model->dash_model,
                &position,
                game->view_port,
                game->camera,
                renderer->dash_buffer);
        }
        break;
        default:
            break;
        }
    }
done_draw:;
    // for( int i = 0; i < vec_size(game->scene_elements); i++ )
    // {
    //     struct SceneElement* scene_element = (struct
    //     SceneElement*)vec_get(game->scene_elements, i); memcpy(&position,
    //     scene_element->position, sizeof(struct DashPosition)); position.x = position.x -
    //     game->camera_world_x; position.y = position.y - game->camera_world_y; position.z =
    //     position.z - game->camera_world_z; dash3d_render_model(
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
                renderer->dash_buffer);
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

    if( game->sprite_invback )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_invback,
            game->iface_view_port,
            553,
            205,
            renderer->pixel_buffer);

    if( game->sprite_cross[0] && game->mouse_cycle != -1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_cross[game->mouse_cycle / 100],
            game->iface_view_port,
            game->mouse_clicked_x - 8 - 4,
            game->mouse_clicked_y - 8 - 4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backleft1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backleft1,
            game->iface_view_port,
            0,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backleft2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backleft2,
            game->iface_view_port,
            0,
            357,
            renderer->pixel_buffer);
    }

    if( game->sprite_backright1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backright1,
            game->iface_view_port,
            722,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backright2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backright2,
            game->iface_view_port,
            743,
            205,
            renderer->pixel_buffer);
    }

    if( game->sprite_backtop1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backtop1,
            game->iface_view_port,
            0,
            0,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid1,
            game->iface_view_port,
            516,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid2,
            game->iface_view_port,
            516,
            205,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid3 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid3,
            game->iface_view_port,
            496,
            357,
            renderer->pixel_buffer);
    }

    if( game->sprite_backhmid2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backhmid2,
            game->iface_view_port,
            0,
            338,
            renderer->pixel_buffer);
    }

    if( game->sprite_mapback )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_mapback,
            game->iface_view_port,
            550,
            4,
            renderer->pixel_buffer);
    }

    int bind_x = 516;
    int bind_y = 160;

    if( game->sprite_backhmid1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backhmid1,
            game->iface_view_port,
            bind_x,
            bind_y,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[0] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[0],
            game->iface_view_port,
            bind_x + 29,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[1] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[1],
            game->iface_view_port,
            bind_x + 53,
            bind_y + 11,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[2] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[2],
            game->iface_view_port,
            bind_x + 82,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[3] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[3],
            game->iface_view_port,
            bind_x + 115,
            bind_y + 12,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[4] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[4],
            game->iface_view_port,
            bind_x + 153,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[5] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[5],
            game->iface_view_port,
            bind_x + 180,
            bind_y + 11,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[6] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[6],
            game->iface_view_port,
            bind_x + 208,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    int camera_tile_x = game->camera_world_x / 128;
    int camera_tile_z = game->camera_world_z / 128;

    int radius = 25;
    int sw_x = camera_tile_x - radius;
    int sw_z = camera_tile_z - radius;
    int ne_x = camera_tile_x + radius;
    int ne_z = camera_tile_z + radius;

    struct MinimapRenderCommandBuffer* minimap_command_buffer = minimap_commands_new(1024);
    minimap_render(game->sys_minimap, sw_x, sw_z, ne_x, ne_z, 0, minimap_command_buffer);

    // Render minimap to buffer starting at (0,0)
    // Calculate the center of the minimap content for rotation anchor
    int minimap_content_width = (ne_x - sw_x) * 4;
    int minimap_content_height = (ne_z - sw_z) * 4;
    int minimap_content_center_x = minimap_content_width / 2;
    int minimap_content_center_y = minimap_content_height / 2;
    int rgb_foreground;
    int rgb_background;
    int shape;
    int angle;
    for( int i = 0; i < minimap_command_buffer->count; i++ )
    {
        struct MinimapRenderCommand* command = &minimap_command_buffer->commands[i];
        switch( command->kind )
        {
        case MINIMAP_RENDER_COMMAND_LOC:
        {
            break;
        }
        case MINIMAP_RENDER_COMMAND_TILE:
        {
            shape = minimap_tile_shape(
                game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);

            angle = minimap_tile_rotation(
                game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);
            rgb_background = minimap_tile_rgb(
                game->sys_minimap,
                command->_tile.tile_sx,
                command->_tile.tile_sz,
                0,
                MINIMAP_BACKGROUND);
            rgb_foreground = minimap_tile_rgb(
                game->sys_minimap,
                command->_tile.tile_sx,
                command->_tile.tile_sz,
                0,
                MINIMAP_FOREGROUND);
            if( rgb_foreground == 0 && rgb_background == 0 )
                break;

            // Write minimap starting at (0,0) in the buffer
            // X: tile_sx increases from sw_x to ne_x, map to 0 to (ne_x - sw_x) * 4
            int tile_x = (command->_tile.tile_sx - sw_x) * 4;
            // Y: tile_sz increases from sw_z to ne_z, but we want ne_z at top (y=0) and sw_z at
            // bottom Original: minimap_center_y - (tile_sz + 1 - sw_z) * 4 For (0,0) start: (ne_z -
            // tile_sz) * 4
            int tile_y = (ne_z - command->_tile.tile_sz) * 4;
            dash2d_fill_minimap_tile(
                renderer->minimap_buffer,
                renderer->minimap_buffer_width,
                tile_x,
                tile_y,
                rgb_background,
                rgb_foreground,
                angle,
                shape,
                renderer->minimap_buffer_width,
                renderer->minimap_buffer_height);

            {
                int wall = minimap_tile_wall(
                    game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);
                if( wall != 0 )
                {
                    int tx = tile_x;
                    int ty = tile_y;
                    dash2d_draw_minimap_wall(
                        renderer->minimap_buffer,
                        renderer->minimap_buffer_width,
                        tx,
                        ty,
                        wall,
                        renderer->minimap_buffer_width,
                        renderer->minimap_buffer_height);
                }
            }
        }

        break;
        }
    }
    minimap_commands_free(minimap_command_buffer);

    // Rotate and blit the minimap buffer to the main pixel buffer
    // The minimap rotates around its center
    int minimap_dst_x = 350;
    int minimap_dst_y = 350;
    // Destination anchor point is where the minimap center will appear
    int minimap_dst_anchor_x = minimap_dst_x;
    int minimap_dst_anchor_y = minimap_dst_y;
    // Source anchor point is the center of the minimap content (which starts at 0,0 in buffer)
    // The minimap content center is at (minimap_content_center_x, minimap_content_center_y)
    int minimap_src_anchor_x = minimap_content_center_x;
    int minimap_src_anchor_y = minimap_content_center_y;
    blit_rotated_buffer(
        renderer->minimap_buffer,
        renderer->minimap_buffer_width,
        renderer->minimap_buffer_height,
        renderer->pixel_buffer,
        renderer->width,
        renderer->width,
        renderer->height,
        minimap_dst_x,
        minimap_dst_y,
        minimap_dst_anchor_x,
        minimap_dst_anchor_y,
        minimap_src_anchor_x,
        minimap_src_anchor_y,
        game->camera_yaw);

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
    int texture_pitch = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
        return;

    int row_size = renderer->width * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    int texture_w = texture_pitch / sizeof(int); // Convert pitch (bytes) to pixels

    uint8_t buffer[100];
    strcpy((char*)buffer, "Hello world!");
    buffer[0] = 163;
    // if( game->pixfont_b12 )
    //     dashfont_draw_text(
    //         game->pixfont_b12,
    //         buffer,
    //         0,
    //         0,
    //         0xFFFFFF,
    //         renderer->dash_buffer,
    //         renderer->dash_buffer_width);

    if( interacting_scene_element != -1 )
    {
        struct SceneElement* element =
            scene_element_at(game->scene->scenery, interacting_scene_element);
        if( element )
        {
            snprintf((char*)buffer, sizeof(buffer), "%s", element->_dbg_name);
            if( game->pixfont_b12 )
                dashfont_draw_text(
                    game->pixfont_b12,
                    buffer,
                    10,
                    10,
                    0xFFFF,
                    renderer->dash_buffer,
                    renderer->dash_buffer_width);
        }
    }

    // Copy dash buffer directly to texture at offset position
    if( renderer->dash_buffer )
    {
        int* dash_buf = renderer->dash_buffer;
        int dash_w = renderer->dash_buffer_width;
        int dash_h = renderer->dash_buffer_height;
        int offset_x = renderer->dash_offset_x;
        int offset_y = renderer->dash_offset_y;

        for( int y = 0; y < dash_h; y++ )
        {
            int dst_y = y + offset_y;
            if( dst_y >= 0 && dst_y < renderer->height )
            {
                for( int x = 0; x < dash_w; x++ )
                {
                    int dst_x = x + offset_x;
                    if( dst_x >= 0 && dst_x < renderer->width )
                    {
                        int src_pixel = dash_buf[y * dash_w + x];
                        pix_write[dst_y * texture_w + dst_x] = src_pixel;
                    }
                }
            }
        }
    }

    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        int* row = &pix_write[(src_y * renderer->width)];
        for( int x = 0; x < renderer->width; x++ )
        {
            int pixel = src_pixels[src_y * renderer->width + x];
            if( pixel != 0 )
            {
                row[x] = pixel;
            }
        }
        // memcpy(row, &src_pixels[(src_y - 0) * renderer->width], row_size);
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