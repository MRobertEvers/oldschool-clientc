#include "torirs_nuklear_debug_panel.h"

#include "nuklear/torirs_nuklear.h"
#include "platforms/common/mem_format.h"
#include "platforms/common/platform_memory.h"
#include "platforms/platform_impl2_sdl2_renderer_soft3d_shared.h"

#include <SDL.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "tori_rs.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"
extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;
}

static int s_soft3d_show_collision_map = 0;

void
torirs_nk_debug_panel_draw(
    struct nk_context* nk,
    struct GGame* game,
    TorirsNkDebugPanelParams* p)
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
        if( p->include_frame_work_timing && p->frame_work_avg_ms >= 0.0 )
            nk_labelf(nk, NK_TEXT_LEFT, "Avg CPU work (10f): %.2f ms", p->frame_work_avg_ms);

#if ENABLE_HEAP_INFO
        {
            struct PlatformMemoryInfo mem = {};
            if( platform_get_memory_info(&mem) )
            {
                char used_b[48];
                char total_b[48];
                char peak_b[48];
                mem_format_bytes(used_b, sizeof used_b, mem.heap_used);
                mem_format_bytes(total_b, sizeof total_b, mem.heap_total);
                nk_labelf(nk, NK_TEXT_LEFT, "Heap: %s / %s", used_b, total_b);
                if( mem.heap_total > 0 )
                    nk_prog(nk, (nk_size)mem.heap_used, (nk_size)mem.heap_total, NK_FIXED);
                if( mem.heap_peak > 0 )
                {
                    mem_format_bytes(peak_b, sizeof peak_b, mem.heap_peak);
                    nk_labelf(nk, NK_TEXT_LEFT, "Peak: %s", peak_b);
                }
            }
        }
#endif

        if( p->include_soft3d_extras && p->soft3d )
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
                int dyn = p->soft3d->pixel_size_dynamic ? 1 : 0;
                nk_checkbox_label(nk, "Dynamic pixel size", &dyn);
                p->soft3d->pixel_size_dynamic = dyn != 0;
            }
            nk_labelf(
                nk, NK_TEXT_LEFT, "Render size: %d x %d", p->soft3d->width, p->soft3d->height);

            if( game->view_port )
            {
                int w = game->view_port->width;
                int h = game->view_port->height;
                if( p->soft3d->pixel_size_dynamic )
                {
                    nk_widget_disable_begin(nk);
                    nk_property_int(nk, "World viewport W", 1, &w, p->soft3d->max_width, 1, 0.1f);
                    nk_property_int(nk, "World viewport H", 1, &h, p->soft3d->max_height, 1, 0.1f);
                    nk_widget_disable_end(nk);
                }
                else
                {
                    nk_property_int(nk, "World viewport W", 1, &w, p->soft3d->max_width, 1, 0.1f);
                    nk_property_int(nk, "World viewport H", 1, &h, p->soft3d->max_height, 1, 0.1f);
                }
                if( w != game->view_port->width || h != game->view_port->height )
                    LibToriRS_GameSetWorldViewportSize(game, w, h);
            }

            nk_labelf(nk, NK_TEXT_LEFT, "Mouse (game x, y): %d, %d", game->mouse_x, game->mouse_y);
            if( p->sdl_window )
            {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                nk_labelf(nk, NK_TEXT_LEFT, "Mouse (window x, y): %d, %d", mx, my);
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
            nk_layout_row_template_begin(nk, 22);
            nk_layout_row_template_push_dynamic(nk);
            nk_layout_row_template_push_static(nk, 48);
            nk_layout_row_template_end(nk);
            nk_label(nk, camera_pos_text, NK_TEXT_LEFT);
            if( nk_button_label(nk, "Copy##pos") )
                SDL_SetClipboardText(camera_pos_text);

            char camera_rot_text[256];
            snprintf(
                camera_rot_text,
                sizeof(camera_rot_text),
                "Camera (pitch, yaw, roll): %d, %d, %d",
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll);
            nk_layout_row_template_begin(nk, 22);
            nk_layout_row_template_push_dynamic(nk);
            nk_layout_row_template_push_static(nk, 48);
            nk_layout_row_template_end(nk);
            nk_label(nk, camera_rot_text, NK_TEXT_LEFT);
            if( nk_button_label(nk, "Copy##rot") )
                SDL_SetClipboardText(camera_rot_text);

            if( p->soft3d->clicked_tile_x != -1 && p->soft3d->clicked_tile_z != -1 )
            {
                char clicked_tile_text[256];
                snprintf(
                    clicked_tile_text,
                    sizeof(clicked_tile_text),
                    "Clicked Tile: (%d, %d, level %d)",
                    p->soft3d->clicked_tile_x,
                    p->soft3d->clicked_tile_z,
                    p->soft3d->clicked_tile_level);
                nk_layout_row_template_begin(nk, 22);
                nk_layout_row_template_push_dynamic(nk);
                nk_layout_row_template_push_static(nk, 48);
                nk_layout_row_template_end(nk);
                nk_label(nk, clicked_tile_text, NK_TEXT_LEFT);
                if( nk_button_label(nk, "Copy##tile") )
                    SDL_SetClipboardText(clicked_tile_text);
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
            if( p->gpu_submitted_model_draws || p->gpu_pose_invalid_skips ||
                p->gpu_dynamic_index_draws || p->gpu_model_draws )
            {
                nk_labelf(nk, NK_TEXT_LEFT, "WebGL GPU submits: %u", p->gpu_submitted_model_draws);
                nk_labelf(
                    nk, NK_TEXT_LEFT, "WebGL pose invalid skips: %u", p->gpu_pose_invalid_skips);
                nk_labelf(
                    nk, NK_TEXT_LEFT, "WebGL dynamic index draws: %u", p->gpu_dynamic_index_draws);
            }
            if( p->gpu_gl_pass_subdraws || p->gpu_gl_index_draw_calls ||
                p->gpu_gl_merge_brk_chunk || p->gpu_gl_merge_brk_vbo || p->gpu_gl_merge_brk_pool ||
                p->gpu_gl_merge_brk_invalid || p->gpu_gl_merge_outer_skips )
            {
                nk_labelf(
                    nk, NK_TEXT_LEFT, "WGL1 subdraw records (frame): %u", p->gpu_gl_pass_subdraws);
                nk_labelf(
                    nk,
                    NK_TEXT_LEFT,
                    "WGL1 glDrawElements (merged): %u",
                    p->gpu_gl_index_draw_calls);
                nk_labelf(
                    nk,
                    NK_TEXT_LEFT,
                    "WGL1 merge break: chunk=%u vbo=%u pool=%u invalid=%u outer_skip=%u",
                    p->gpu_gl_merge_brk_chunk,
                    p->gpu_gl_merge_brk_vbo,
                    p->gpu_gl_merge_brk_pool,
                    p->gpu_gl_merge_brk_invalid,
                    p->gpu_gl_merge_outer_skips);
            }
        }
    }

    /* ---- Packet sim ---- */
    if( nk_tree_push(nk, NK_TREE_TAB, "Packet sim", NK_MINIMIZED) )
    {
        /* Static simulation state persists across frames. */
        static int sim_comp_id  = 0x10000001;
        static int sim_obj_id   = 314;  /* iron ore */
        static int sim_model_id = 100;
        static int sim_anim_id  = 0;
        static int sim_npc_id   = 1;    /* goblin */
        static int sim_zoom     = 1000;
        static int sim_hide     = 0;
        static int sim_cam_lx   = 3222 * 8;
        static int sim_cam_lz   = 3218 * 8;
        static int sim_cam_h    = 400;
        static int sim_cam_axis = 0;
        static int sim_cam_amp  = 3;
        static int sim_varp_id  = 0;
        static int sim_varp_val = 0;
        static int sim_stat_id  = 0;
        static int sim_stat_xp  = 0;
        static int sim_stat_lvl = 1;
        static int sim_energy   = 100;
        static int sim_zone_x   = 403;  /* Lumbridge area */
        static int sim_zone_z   = 403;
        static int sim_loc_id   = 10;
        static int sim_seq_id   = 0;
        static int sim_spotanim = 0;

        /* Helper: allocate and append a simulated packet to the LC245_2 revision queue. */
        auto simulate_pkt = [&](struct RevPacket_LC245_2* pkt) {
            assert(game->revision.kind == REVISION_KIND_LC245_2 && game->revision.impl);
            gameproto_rev245_2_enqueue((struct RevisionLC245_2*)game->revision.impl, pkt);
        };

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Interface --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "comp_id", 0, &sim_comp_id, 0x7fffffff, 1, 1);
        nk_property_int(nk, "model_id", 0, &sim_model_id, 65535, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "IF_OPENMAIN(comp_id)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_OPENMAIN;
            pkt._if_openmain.component_id = sim_comp_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_CLOSE") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_CLOSE;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "hide", 0, &sim_hide, 1, 1, 1);
        nk_property_int(nk, "zoom", 1, &sim_zoom, 10000, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "IF_SETHIDE(comp_id,hide)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_SETHIDE;
            pkt._if_sethide.component_id = sim_comp_id;
            pkt._if_sethide.hide = sim_hide;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_SETMODEL(comp_id,model_id)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_SETMODEL;
            pkt._if_setmodel.component_id = sim_comp_id;
            pkt._if_setmodel.model_id = sim_model_id;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "anim_id", 0, &sim_anim_id, 65535, 1, 1);
        nk_property_int(nk, "obj_id", 0, &sim_obj_id, 65535, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "IF_SETANIM(comp_id,anim)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_SETANIM;
            pkt._if_setanim.component_id = sim_comp_id;
            pkt._if_setanim.anim_id = sim_anim_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_SETOBJECT(comp_id,obj,zoom)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_IF_SETOBJECT;
            pkt._if_setobject.component_id = sim_comp_id;
            pkt._if_setobject.obj_id = sim_obj_id;
            pkt._if_setobject.zoom = sim_zoom;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Inventory --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "slot", 0, &sim_anim_id, 127, 1, 1); /* reuse anim_id as slot */
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_INV_PARTIAL(comp,slot,obj,1)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_UPDATE_INV_PARTIAL;
            pkt._update_inv_partial.component_id = sim_comp_id;
            pkt._update_inv_partial.count = 1;
            pkt._update_inv_partial.entries =
                (struct PktUpdateInvPartialEntry*)calloc(1, sizeof(struct PktUpdateInvPartialEntry));
            pkt._update_inv_partial.entries[0].slot   = sim_anim_id;
            pkt._update_inv_partial.entries[0].obj_id = sim_obj_id;
            pkt._update_inv_partial.entries[0].count  = 1;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Camera --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 3);
        nk_property_int(nk, "cam_lx", 0, &sim_cam_lx, 16384, 1, 1);
        nk_property_int(nk, "cam_lz", 0, &sim_cam_lz, 16384, 1, 1);
        nk_property_int(nk, "cam_h",  0, &sim_cam_h,  2000,  1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "CAM_LOOKAT(lx,lz,h)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_CAM_LOOKAT;
            pkt._cam_lookat.local_x = sim_cam_lx;
            pkt._cam_lookat.local_z = sim_cam_lz;
            pkt._cam_lookat.height  = sim_cam_h;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "CAM_MOVETO(lx,lz,h)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_CAM_MOVETO;
            pkt._cam_moveto.local_x = sim_cam_lx;
            pkt._cam_moveto.local_z = sim_cam_lz;
            pkt._cam_moveto.height  = sim_cam_h;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "CAM_RESET") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_CAM_RESET;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "cam_axis", 0, &sim_cam_axis, 3, 1, 1);
        nk_property_int(nk, "cam_amp",  0, &sim_cam_amp,  20, 1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "CAM_SHAKE(axis,amp)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_CAM_SHAKE;
            pkt._cam_shake.axis      = sim_cam_axis;
            pkt._cam_shake.amplitude = sim_cam_amp;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Vars / Stats --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "varp_id",  0, &sim_varp_id,  2047, 1, 1);
        nk_property_int(nk, "varp_val", INT_MIN, &sim_varp_val, INT_MAX, 1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "VARP_SMALL(id,val)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_VARP_SMALL;
            pkt._varp_small.variable = sim_varp_id;
            pkt._varp_small.value    = sim_varp_val;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 3);
        nk_property_int(nk, "stat_id",  0, &sim_stat_id,  24,       1, 1);
        nk_property_int(nk, "stat_xp",  0, &sim_stat_xp,  200000000, 1, 1);
        nk_property_int(nk, "stat_lvl", 1, &sim_stat_lvl, 99,       1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_STAT(id,xp,lvl)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_UPDATE_STAT;
            pkt._update_stat.stat  = sim_stat_id;
            pkt._update_stat.xp    = sim_stat_xp;
            pkt._update_stat.level = sim_stat_lvl;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "run_energy", 0, &sim_energy, 100, 1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_RUNENERGY(val)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_UPDATE_RUNENERGY;
            pkt._update_run_energy.run_energy = sim_energy;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Zone --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "zone_x", 0, &sim_zone_x, 800, 1, 1);
        nk_property_int(nk, "zone_z", 0, &sim_zone_z, 800, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_ZONE_FULL_FOLLOWS(zone_x,zone_z)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS;
            pkt._update_zone_full_follows.base_x = sim_zone_x;
            pkt._update_zone_full_follows.base_z = sim_zone_z;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 3);
        nk_property_int(nk, "loc_id",  0, &sim_loc_id,  65535, 1, 1);
        nk_property_int(nk, "seq_id",  0, &sim_seq_id,  65535, 1, 1);
        nk_property_int(nk, "spotanim", 0, &sim_spotanim, 65535, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "LOC_ADD_CHANGE(pos=0,shape=0,loc_id)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_LOC_ADD_CHANGE;
            pkt._loc_add_change.pos    = 0;
            pkt._loc_add_change.info   = 0;
            pkt._loc_add_change.loc_id = sim_loc_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "LOC_DEL(pos=0,info=0)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_LOC_DEL;
            pkt._loc_del.pos  = 0;
            pkt._loc_del.info = 0;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "LOC_ANIM(pos=0,seq_id)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_LOC_ANIM;
            pkt._loc_anim.pos    = 0;
            pkt._loc_anim.seq_id = sim_seq_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "OBJ_ADD(pos=0,obj_id,count=1)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_OBJ_ADD;
            pkt._obj_add.pos    = 0;
            pkt._obj_add.obj_id = sim_obj_id;
            pkt._obj_add.count  = 1;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "OBJ_DEL(pos=0,obj_id)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_OBJ_DEL;
            pkt._obj_del.pos    = 0;
            pkt._obj_del.obj_id = sim_obj_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "MAP_ANIM(pos=0,spotanim,h=0,delay=0)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_MAP_ANIM;
            pkt._map_anim.pos    = 0;
            pkt._map_anim.id     = sim_spotanim;
            pkt._map_anim.height = 0;
            pkt._map_anim.delay  = 0;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Maps --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "REBUILD_NORMAL(zone_x, zone_z)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_REBUILD_NORMAL;
            pkt._map_rebuild.zonex = sim_zone_x;
            pkt._map_rebuild.zonez = sim_zone_z;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Misc --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "HINT_ARROW tile(zone_x,zone_z)") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_HINT_ARROW;
            pkt._hint_arrow.type   = 2; /* tile type */
            pkt._hint_arrow.id     = sim_zone_x * 8;
            pkt._hint_arrow.z      = sim_zone_z * 8;
            pkt._hint_arrow.height = 0;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "UNSET_MAP_FLAG") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_UNSET_MAP_FLAG;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "RESET_ANIMS") )
        {
            struct RevPacket_LC245_2 pkt = {};
            pkt.packet_type = PKTIN_LC245_2_RESET_ANIMS;
            simulate_pkt(&pkt);
        }

        nk_tree_pop(nk);
    }

    nk_end(nk);
}