#include "platform_impl2_osx_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "graphics/convex_hull.u.c"
#include "graphics/dash.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/collision_map.h"
#include "osrs/colors.h"
#include "osrs/dash_utils.h"
#include "osrs/game_entity.h"
#include "osrs/gio.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/interface.h"
#include "osrs/minimap.h"
#include "osrs/minimenu.h"
#include "osrs/model_transforms.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/script_queue.h"
#include "server/prot.h"
#include "server/server.h"
#include "tori_rs.h"
}

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

#include <SDL.h>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

static bool g_show_collision_map = false;

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

    ImGui::Text("Max paint command: %d", game->cc);
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

    // Display clicked tile information
    ImGui::Separator();
    if( renderer->clicked_tile_x != -1 && renderer->clicked_tile_z != -1 )
    {
        char clicked_tile_text[256];
        snprintf(
            clicked_tile_text,
            sizeof(clicked_tile_text),
            "Clicked Tile: (%d, %d, level %d)",
            renderer->clicked_tile_x,
            renderer->clicked_tile_z,
            renderer->clicked_tile_level);
        ImGui::Text("%s", clicked_tile_text);
        ImGui::SameLine();
        if( ImGui::SmallButton("Copy##tile") )
        {
            ImGui::SetClipboardText(clicked_tile_text);
        }
    }
    else
    {
        ImGui::Text("Clicked Tile: None");
    }

    // Add camera speed slider
    ImGui::Separator();

    ImGui::Separator();
    ImGui::Checkbox("Show collision map", &g_show_collision_map);

    // Interface controls removed - now server-controlled via IF_SETTAB/IF_SETTAB_ACTIVE packets
    ImGui::Separator();
    ImGui::Text("Interface System:");
    ImGui::Text("Current viewport ID: %d", game->viewport_interface_id);
    ImGui::Text("Current sidebar ID: %d", game->sidebar_interface_id);
    ImGui::Text("Selected tab: %d", game->selected_tab);
    if( game->selected_tab >= 0 && game->selected_tab < 14 )
    {
        ImGui::Text(
            "Tab %d interface ID: %d",
            game->selected_tab,
            game->tab_interface_id[game->selected_tab]);
    }

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
    renderer->highlight_poly_valid = 0;

    // Initialize minimap buffer (will be allocated when needed)
    renderer->minimap_buffer = NULL;
    renderer->minimap_buffer_width = 0;
    renderer->minimap_buffer_height = 0;

    // Initialize outgoing message buffer
    renderer->outgoing_message_size = 0;

    // Initialize client-side position interpolation
    renderer->client_player_pos_tile_x = 0;
    renderer->client_player_pos_tile_z = 0;
    renderer->client_target_tile_x = 0;
    renderer->client_target_tile_z = 0;
    renderer->last_move_time_ms = 0;

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
    int src_anchor_x,
    int src_anchor_y,
    int* dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    int dst_anchor_x,
    int dst_anchor_y,
    int angle_r2pi2048);

/* Callbacks for static UI buffer execute (C-compatible). */
static void
static_ui_blit_sprite_cb(
    void* userdata,
    struct GGame* game,
    struct DashSprite* sprite,
    int x,
    int y)
{
    auto* r = (struct Platform2_OSX_SDL2_Renderer_Soft3D*)userdata;
    if( game->sys_dash && sprite && r->pixel_buffer )
        dash2d_blit_sprite(game->sys_dash, sprite, game->iface_view_port, x, y, r->pixel_buffer);
}

static void
static_ui_blit_rotated_cb(
    void* userdata,
    struct GGame* game,
    struct DashSprite* sprite,
    int x,
    int y,
    int dest_w,
    int dest_h,
    int dest_anchor_x,
    int dest_anchor_y,
    int rotation_cw)
{
    (void)game;
    auto* r = (struct Platform2_OSX_SDL2_Renderer_Soft3D*)userdata;
    if( !sprite || !r->pixel_buffer )
        return;
    blit_rotated_buffer(
        (int*)sprite->pixels_argb,
        sprite->width,
        sprite->height,
        sprite->width >> 1,
        sprite->height >> 1,
        r->pixel_buffer,
        r->width,
        x,
        y,
        dest_w,
        dest_h,
        dest_anchor_x,
        dest_anchor_y,
        rotation_cw);
}

static void
blit_rotated_buffer(
    int* src_buffer,
    int src_width,
    int src_height,
    int src_anchor_x,
    int src_anchor_y,
    int* dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    int dst_anchor_x,
    int dst_anchor_y,
    int angle_r2pi2048)
{
    assert(dst_width + dst_x <= dst_stride);
    int sin = dash_sin(angle_r2pi2048);
    int cos = dash_cos(angle_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_width;
    int max_y = dst_y + dst_height;

    if( max_x > dst_stride )
        max_x = dst_stride;
    // if (max_y > dst_height)
    //     max_y = dst_height;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

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
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    /* Sync viewport offset so frame logic (menu, cross, hover) uses correct coords. */
    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

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

        game->iface_view_port->clip_bottom = new_height;
        game->iface_view_port->clip_right = new_width;
        game->iface_view_port->clip_left = 0;
        game->iface_view_port->clip_top = 0;
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
    struct ToriRSRenderCommand command = { 0 };

    renderer->highlight_poly_valid = 0;

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_MODEL_LOAD:
        case TORIRS_GFX_TEXTURE_LOAD:
        case TORIRS_GFX_SCENE_ELEMENT_LOAD:
            break;
        case TORIRS_GFX_MODEL_DRAW:
            dash3d_raster_projected_model(
                game->sys_dash,
                command._model_draw.model,
                &command._model_draw.position,
                game->view_port,
                game->camera,
                renderer->dash_buffer,
                false);
            break;
        // case TORIRS_GFX_MODEL_DRAW_HIGHLIGHT:
        // {
        //     struct DashModel* model = command._model_draw.model;
        //     struct DashPosition* position = &command._model_draw.position;
        //     if( game->sys_dash && model && model->vertex_count >= 3 && game->view_port &&
        //         game->camera )
        //     {
        //         static const int DASH_MAX_VERTICES = 4096;
        //         static float hull_in_x[DASH_MAX_VERTICES];
        //         static float hull_in_y[DASH_MAX_VERTICES];
        //         static float hull_out_x[DASH_MAX_VERTICES];
        //         static float hull_out_y[DASH_MAX_VERTICES];
        //         int n = model->vertex_count;
        //         if( n > DASH_MAX_VERTICES )
        //             n = DASH_MAX_VERTICES;
        //         dash3d_copy_screen_vertices_float(game->sys_dash, hull_in_x, hull_in_y, n);
        //         size_t hull_n =
        //             compute_convex_hull(hull_in_x, hull_in_y, (size_t)n, hull_out_x, hull_out_y);
        //         if( hull_n >= 3 && hull_n <= (size_t)PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX )
        //         {
        //             int cx = game->view_port->x_center;
        //             int cy = game->view_port->y_center;
        //             for( size_t i = 0; i < hull_n; i++ )
        //             {
        //                 renderer->highlight_poly_x[i] = (int)hull_out_x[i] + cx;
        //                 renderer->highlight_poly_y[i] = (int)hull_out_y[i] + cy;
        //             }
        //             renderer->highlight_poly_n = (int)hull_n;
        //             renderer->highlight_poly_valid = 1;
        //         }
        //     }
        //     break;
        // }
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    int camera_tile_x = game->camera_world_x / 128;
    int camera_tile_z = game->camera_world_z / 128;

    int radius = 25;
    int sw_x = camera_tile_x - radius;
    int sw_z = camera_tile_z - radius;
    int ne_x = camera_tile_x + radius;
    int ne_z = camera_tile_z + radius;

    struct MinimapRenderCommandBuffer* minimap_command_buffer = minimap_commands_new(1024);
    if( game->sys_minimap )
        minimap_render(game->sys_minimap, sw_x, sw_z, ne_x, ne_z, 0, minimap_command_buffer);

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

    int mm_side = 800 - (512 + 10);
    blit_rotated_buffer(
        renderer->minimap_buffer,
        renderer->minimap_buffer_width,
        renderer->minimap_buffer_height,
        renderer->minimap_buffer_width >> 1,
        renderer->minimap_buffer_height >> 1,
        renderer->pixel_buffer,
        renderer->width,
        550 + 25,
        4 + 5,
        146,
        151,
        73,
        75,
        game->camera_yaw);

    /* Draw highlight polygon into dash_buffer (viewport-local); alpha-blends with 3D scene. */
    if( renderer->highlight_poly_valid && renderer->highlight_poly_n >= 3 &&
        renderer->highlight_poly_n <= PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX && renderer->dash_buffer )
    {
        int px[PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX];
        int py[PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX];
        int dw = renderer->dash_buffer_width;
        int dh = renderer->dash_buffer_height;
        for( int i = 0; i < renderer->highlight_poly_n; i++ )
        {
            px[i] = renderer->highlight_poly_x[i];
            py[i] = renderer->highlight_poly_y[i];
        }
        /* Inclusive clip bounds so we never write past the buffer (valid x: 0..dw-1, y: 0..dh-1).
         */
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = dw > 0 ? dw - 1 : 0;
        int clip_b = dh > 0 ? dh - 1 : 0;
        dash2d_fill_polygon_alpha(
            renderer->dash_buffer,
            dw,
            px,
            py,
            renderer->highlight_poly_n,
            0x00FF00,
            80,
            clip_l,
            clip_t,
            clip_r,
            clip_b);
    }

    /* Blit 3D view (dash_buffer) into pixel_buffer at viewport offset so path overlay draws on top.
     */
    if( renderer->dash_buffer && renderer->dash_buffer_width > 0 &&
        renderer->dash_buffer_height > 0 )
    {
        int* dash_buf = renderer->dash_buffer;
        int dash_w = renderer->dash_buffer_width;
        int dash_h = renderer->dash_buffer_height;
        int off_x = renderer->dash_offset_x;
        int off_y = renderer->dash_offset_y;
        int* pix = renderer->pixel_buffer;
        int pix_stride = renderer->width;
        for( int y = 0; y < dash_h; y++ )
        {
            int dst_y = y + off_y;
            if( dst_y >= 0 && dst_y < renderer->height )
                for( int x = 0; x < dash_w; x++ )
                {
                    int dst_x = x + off_x;
                    if( dst_x >= 0 && dst_x < renderer->width )
                        pix[dst_y * pix_stride + dst_x] = dash_buf[y * dash_w + x];
                }
        }
    }

    /* Client.ts drawTooltip: draw hovered loc/NPC/player text at (4, 15) in top left. */
    if( renderer->dash_buffer && game->pixfont_b12 && game->option_set.option_count > 0 )
    {
        struct WorldOption* top =
            world_option_set_get_option(&game->option_set, game->option_set.option_count - 1);
        char tooltip_buf[80];
        size_t len = (size_t)snprintf(tooltip_buf, sizeof(tooltip_buf), "%s", top->text);
        if( game->option_set.option_count > 1 && len < sizeof(tooltip_buf) - 16 )
            snprintf(
                tooltip_buf + len,
                sizeof(tooltip_buf) - len,
                "@whi@  / %d more options",
                game->option_set.option_count - 1);
        dashfont_draw_text_ex(
            game->pixfont_b12,
            (uint8_t*)tooltip_buf,
            4,
            15,
            0xFFFFFF,
            renderer->pixel_buffer,
            renderer->width);
    }

    // Render minimap to buffer starting at (0,0)
    // Calculate the center of the minimap content for rotation anchor

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

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_ProcessServer(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Server* server,
    struct GGame* game,
    uint64_t timestamp_ms)
{
    if( !server )
    {
        return;
    }

    // if( game->build_player == 1 )
    // {
    //     build_player(game);
    // }

    if( renderer->first_frame == 0 )
    {
        struct ProtConnect connect;
        connect.pid = 0;
        renderer->first_frame = 1;
        renderer->outgoing_message_size = prot_connect_encode(
            &connect, renderer->outgoing_message_buffer, sizeof(renderer->outgoing_message_buffer));
    }

    // Check if a tile was clicked and pack message into buffer
    if( renderer->clicked_tile_x != -1 && renderer->clicked_tile_z != -1 )
    {
        struct ProtTileClick tile_click;
        tile_click.x = renderer->clicked_tile_x;
        tile_click.z = renderer->clicked_tile_z;

        renderer->outgoing_message_size = prot_tile_click_encode(
            &tile_click,
            renderer->outgoing_message_buffer,
            sizeof(renderer->outgoing_message_buffer));

        // Clear the clicked tile after packing the message
        renderer->clicked_tile_x = -1;
        renderer->clicked_tile_z = -1;
        renderer->clicked_tile_level = -1;
    }
}