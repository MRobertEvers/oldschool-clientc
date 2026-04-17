#include "torirs_nuklear_debug_panel.h"

#include "nuklear/torirs_nuklear.h"

#include "platforms/common/platform_memory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "tori_rs.h"
extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;
}

static int s_soft3d_show_collision_map = 0;

void
torirs_nk_debug_panel_draw(struct nk_context* nk, struct GGame* game, TorirsNkDebugPanelParams* p)
{
    if( !nk || !game || !p || !p->window_title )
        return;

    const double dt = p->delta_time_sec > 1e-12 ? p->delta_time_sec : 1e-3;
    const double ms = dt * 1000.0;
    const double ms_rounded = round(ms * 1000.0) / 1000.0;
    const double fps = 1.0 / dt;

    if( nk_begin(
            nk,
            p->window_title,
            nk_rect(10, 10, 320, 480),
            NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE |
                NK_WINDOW_MINIMIZABLE) )
    {
        nk_layout_row_dynamic(nk, 18, 1);
        nk_labelf(nk, NK_TEXT_LEFT, "%.3f ms/frame (%.2f FPS)", ms_rounded, fps);

#if ENABLE_HEAP_INFO
        {
            struct PlatformMemoryInfo mem = {};
            if( platform_get_memory_info(&mem) )
            {
                float used_mb = mem.heap_used / (1024.0f * 1024.0f);
                float total_mb = mem.heap_total / (1024.0f * 1024.0f);
                nk_labelf(nk, NK_TEXT_LEFT, "Heap: %.1f / %.1f MB", used_mb, total_mb);
                if( mem.heap_total > 0 )
                    nk_prog(nk, (nk_size)mem.heap_used, (nk_size)mem.heap_total, NK_FIXED);
                if( mem.heap_peak > 0 )
                    nk_labelf(nk, NK_TEXT_LEFT, "Peak: %.1f MB", mem.heap_peak / (1024.0f * 1024.0f));
            }
        }
#endif

        if( p->pixel_size_dynamic_inout )
        {
            nk_labelf(nk, NK_TEXT_LEFT, "Max paint command: %d", game->cc);
            nk_labelf(nk, NK_TEXT_LEFT, "Trap command: %d", g_trap_command);
            if( nk_button_label(nk, "Trap command") )
            {
                if( g_trap_command == -1 )
                    g_trap_command = game->cc;
                else
                    g_trap_command = -1;
            }
            nk_property_int(nk, "Trap X", -1, &g_trap_x, 100000, 1, 0.1f);
            nk_property_int(nk, "Trap Z", -1, &g_trap_z, 100000, 1, 0.1f);

            nk_layout_row_dynamic(nk, 22, 1);
            {
                int dyn = *p->pixel_size_dynamic_inout ? 1 : 0;
                nk_checkbox_label(nk, "Dynamic pixel size", &dyn);
                *p->pixel_size_dynamic_inout = dyn != 0;
            }
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "Render size: %d x %d",
                p->render_width,
                p->render_height);

            if( game->view_port )
            {
                int w = game->view_port->width;
                int h = game->view_port->height;
                if( *p->pixel_size_dynamic_inout )
                {
                    nk_widget_disable_begin(nk);
                    nk_property_int(nk, "World viewport W", 1, &w, p->max_width, 1, 0.1f);
                    nk_property_int(nk, "World viewport H", 1, &h, p->max_height, 1, 0.1f);
                    nk_widget_disable_end(nk);
                }
                else
                {
                    nk_property_int(nk, "World viewport W", 1, &w, p->max_width, 1, 0.1f);
                    nk_property_int(nk, "World viewport H", 1, &h, p->max_height, 1, 0.1f);
                }
                if( w != game->view_port->width || h != game->view_port->height )
                    LibToriRS_GameSetWorldViewportSize(game, w, h);
            }

            nk_labelf(nk, NK_TEXT_LEFT, "Mouse (game x, y): %d, %d", game->mouse_x, game->mouse_y);
            if( p->window_mouse_valid )
            {
                nk_labelf(
                    nk,
                    NK_TEXT_LEFT,
                    "Mouse (window x, y): %d, %d",
                    p->window_mouse_x,
                    p->window_mouse_y);
            }

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
            if( p->set_clipboard_text )
            {
                nk_layout_row_template_begin(nk, 22);
                nk_layout_row_template_push_dynamic(nk);
                nk_layout_row_template_push_static(nk, 48);
                nk_layout_row_template_end(nk);
                nk_label(nk, camera_pos_text, NK_TEXT_LEFT);
                if( nk_button_label(nk, "Copy##pos") )
                    p->set_clipboard_text(camera_pos_text);
            }
            else
            {
                nk_label(nk, camera_pos_text, NK_TEXT_LEFT);
            }

            char camera_rot_text[256];
            snprintf(
                camera_rot_text,
                sizeof(camera_rot_text),
                "Camera (pitch, yaw, roll): %d, %d, %d",
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll);
            if( p->set_clipboard_text )
            {
                nk_layout_row_template_begin(nk, 22);
                nk_layout_row_template_push_dynamic(nk);
                nk_layout_row_template_push_static(nk, 48);
                nk_layout_row_template_end(nk);
                nk_label(nk, camera_rot_text, NK_TEXT_LEFT);
                if( nk_button_label(nk, "Copy##rot") )
                    p->set_clipboard_text(camera_rot_text);
            }
            else
            {
                nk_label(nk, camera_rot_text, NK_TEXT_LEFT);
            }

            if( p->clicked_tile_x != -1 && p->clicked_tile_z != -1 )
            {
                char clicked_tile_text[256];
                snprintf(
                    clicked_tile_text,
                    sizeof(clicked_tile_text),
                    "Clicked Tile: (%d, %d, level %d)",
                    p->clicked_tile_x,
                    p->clicked_tile_z,
                    p->clicked_tile_level);
                if( p->set_clipboard_text )
                {
                    nk_layout_row_template_begin(nk, 22);
                    nk_layout_row_template_push_dynamic(nk);
                    nk_layout_row_template_push_static(nk, 48);
                    nk_layout_row_template_end(nk);
                    nk_label(nk, clicked_tile_text, NK_TEXT_LEFT);
                    if( nk_button_label(nk, "Copy##tile") )
                        p->set_clipboard_text(clicked_tile_text);
                }
                else
                {
                    nk_label(nk, clicked_tile_text, NK_TEXT_LEFT);
                }
            }
            else
            {
                nk_label(nk, "Clicked Tile: None", NK_TEXT_LEFT);
            }

            nk_checkbox_label(nk, "Show collision map", &s_soft3d_show_collision_map);
            nk_label(nk, "Interface System:", NK_TEXT_LEFT);
        }
        else
        {
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "Camera: %d %d %d",
                game->camera_world_x,
                game->camera_world_y,
                game->camera_world_z);
            nk_labelf(nk, NK_TEXT_LEFT, "Mouse: %d %d", game->mouse_x, game->mouse_y);
            if( game->view_port )
            {
                int w = game->view_port->width;
                int h = game->view_port->height;
                nk_property_int(nk, "World viewport W", 1, &w, p->view_w_cap, 1, 0.1f);
                nk_property_int(nk, "World viewport H", 1, &h, p->view_h_cap, 1, 0.1f);
                if( w != game->view_port->width || h != game->view_port->height )
                    LibToriRS_GameSetWorldViewportSize(game, w, h);
            }
        }

        if( p->include_load_counts )
        {
            nk_labelf(nk, NK_TEXT_LEFT, "Loaded model keys: %zu", p->loaded_models);
            nk_labelf(nk, NK_TEXT_LEFT, "Loaded scene keys: %zu", p->loaded_scenes);
            nk_labelf(nk, NK_TEXT_LEFT, "Loaded textures: %zu", p->loaded_textures);
        }

        if( p->include_gpu_frame_stats )
        {
            nk_labelf(nk, NK_TEXT_LEFT, "Frame model draws: %u", p->gpu_model_draws);
            nk_labelf(nk, NK_TEXT_LEFT, "Frame triangles: %u", p->gpu_tris);
        }
    }
    nk_end(nk);
}
