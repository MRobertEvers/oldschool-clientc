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
        auto simulate_pkt = [&](struct RevServerProtPacket* pkt) {
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
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_OPENMAIN_V1;
            pkt.u.if_openmain_v1.component_id = sim_comp_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_CLOSE") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_CLOSE_V1;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "hide", 0, &sim_hide, 1, 1, 1);
        nk_property_int(nk, "zoom", 1, &sim_zoom, 10000, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "IF_SETHIDE(comp_id,hide)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_SETHIDE_V1;
            pkt.u.if_sethide_v1.component_id = sim_comp_id;
            pkt.u.if_sethide_v1.hide = sim_hide;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_SETMODEL(comp_id,model_id)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_SETMODEL_V1;
            pkt.u.if_setmodel_v1.component_id = sim_comp_id;
            pkt.u.if_setmodel_v1.model_id = sim_model_id;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "anim_id", 0, &sim_anim_id, 65535, 1, 1);
        nk_property_int(nk, "obj_id", 0, &sim_obj_id, 65535, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "IF_SETANIM(comp_id,anim)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_SETANIM_V1;
            pkt.u.if_setanim_v1.component_id = sim_comp_id;
            pkt.u.if_setanim_v1.anim_id = sim_anim_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "IF_SETOBJECT(comp_id,obj,zoom)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_IF_SETOBJECT_V1;
            pkt.u.if_setobject_v1.component_id = sim_comp_id;
            pkt.u.if_setobject_v1.obj_id = sim_obj_id;
            pkt.u.if_setobject_v1.zoom = sim_zoom;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Inventory --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "slot", 0, &sim_anim_id, 127, 1, 1); /* reuse anim_id as slot */
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_INV_PARTIAL(comp,slot,obj,1)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_UPDATE_INV_PARTIAL_V1;
            pkt.u.update_inv_partial_v1.component_id = sim_comp_id;
            pkt.u.update_inv_partial_v1.count = 1;
            pkt.u.update_inv_partial_v1.entries =
                (struct PktServerProt_UpdateInvPartialEntry_v1*)calloc(1, sizeof(struct PktServerProt_UpdateInvPartialEntry_v1));
            pkt.u.update_inv_partial_v1.entries[0].slot   = sim_anim_id;
            pkt.u.update_inv_partial_v1.entries[0].obj_id = sim_obj_id;
            pkt.u.update_inv_partial_v1.entries[0].count  = 1;
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
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_CAM_LOOKAT_V1;
            pkt.u.cam_lookat_v1.local_x = sim_cam_lx;
            pkt.u.cam_lookat_v1.local_z = sim_cam_lz;
            pkt.u.cam_lookat_v1.height  = sim_cam_h;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "CAM_MOVETO(lx,lz,h)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_CAM_MOVETO_V1;
            pkt.u.cam_moveto_v1.local_x = sim_cam_lx;
            pkt.u.cam_moveto_v1.local_z = sim_cam_lz;
            pkt.u.cam_moveto_v1.height  = sim_cam_h;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "CAM_RESET") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_CAM_RESET_V1;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "cam_axis", 0, &sim_cam_axis, 3, 1, 1);
        nk_property_int(nk, "cam_amp",  0, &sim_cam_amp,  20, 1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "CAM_SHAKE(axis,amp)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_CAM_SHAKE_V1;
            pkt.u.cam_shake_v1.axis      = sim_cam_axis;
            pkt.u.cam_shake_v1.amplitude = sim_cam_amp;
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
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_VARP_SMALL_V1;
            pkt.u.varp_small_v1.variable = sim_varp_id;
            pkt.u.varp_small_v1.value    = sim_varp_val;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 3);
        nk_property_int(nk, "stat_id",  0, &sim_stat_id,  24,       1, 1);
        nk_property_int(nk, "stat_xp",  0, &sim_stat_xp,  200000000, 1, 1);
        nk_property_int(nk, "stat_lvl", 1, &sim_stat_lvl, 99,       1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_STAT(id,xp,lvl)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_UPDATE_STAT_V1;
            pkt.u.update_stat_v1.stat  = sim_stat_id;
            pkt.u.update_stat_v1.xp    = sim_stat_xp;
            pkt.u.update_stat_v1.level = sim_stat_lvl;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 2);
        nk_property_int(nk, "run_energy", 0, &sim_energy, 100, 1, 1);
        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "UPDATE_RUNENERGY(val)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_UPDATE_RUNENERGY_V1;
            pkt.u.update_run_energy_v1.run_energy = sim_energy;
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
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_UPDATE_ZONE_FULL_FOLLOWS_V1;
            pkt.u.update_zone_full_follows_v1.base_x = sim_zone_x;
            pkt.u.update_zone_full_follows_v1.base_z = sim_zone_z;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 3);
        nk_property_int(nk, "loc_id",  0, &sim_loc_id,  65535, 1, 1);
        nk_property_int(nk, "seq_id",  0, &sim_seq_id,  65535, 1, 1);
        nk_property_int(nk, "spotanim", 0, &sim_spotanim, 65535, 1, 1);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "LOC_ADD_CHANGE(pos=0,shape=0,loc_id)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_LOC_ADD_CHANGE_V1;
            pkt.u.loc_add_change_v1.pos    = 0;
            pkt.u.loc_add_change_v1.info   = 0;
            pkt.u.loc_add_change_v1.loc_id = sim_loc_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "LOC_DEL(pos=0,info=0)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_LOC_DEL_V1;
            pkt.u.loc_del_v1.pos  = 0;
            pkt.u.loc_del_v1.info = 0;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "LOC_ANIM(pos=0,seq_id)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_LOC_ANIM_V1;
            pkt.u.loc_anim_v1.pos    = 0;
            pkt.u.loc_anim_v1.seq_id = sim_seq_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "OBJ_ADD(pos=0,obj_id,count=1)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_OBJ_ADD_V1;
            pkt.u.obj_add_v1.pos    = 0;
            pkt.u.obj_add_v1.obj_id = sim_obj_id;
            pkt.u.obj_add_v1.count  = 1;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "OBJ_DEL(pos=0,obj_id)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_OBJ_DEL_V1;
            pkt.u.obj_del_v1.pos    = 0;
            pkt.u.obj_del_v1.obj_id = sim_obj_id;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "MAP_ANIM(pos=0,spotanim,h=0,delay=0)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_MAP_ANIM_V1;
            pkt.u.map_anim_v1.pos    = 0;
            pkt.u.map_anim_v1.id     = sim_spotanim;
            pkt.u.map_anim_v1.height = 0;
            pkt.u.map_anim_v1.delay  = 0;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Maps --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "REBUILD_NORMAL(zone_x, zone_z)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_REBUILD_NORMAL_V1;
            pkt.u.map_rebuild_v1.zonex = sim_zone_x;
            pkt.u.map_rebuild_v1.zonez = sim_zone_z;
            simulate_pkt(&pkt);
        }

        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "-- Misc --", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 22, 1);
        if( nk_button_label(nk, "HINT_ARROW tile(zone_x,zone_z)") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_HINT_ARROW_V1;
            pkt.u.hint_arrow_v1.type   = 2; /* tile type */
            pkt.u.hint_arrow_v1.id     = sim_zone_x * 8;
            pkt.u.hint_arrow_v1.z      = sim_zone_z * 8;
            pkt.u.hint_arrow_v1.height = 0;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "UNSET_MAP_FLAG") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_UNSET_MAP_FLAG_V1;
            simulate_pkt(&pkt);
        }
        if( nk_button_label(nk, "RESET_ANIMS") )
        {
            struct RevServerProtPacket pkt = {};
            pkt.packet_type = SERVERPROT_RESET_ANIMS_V1;
            simulate_pkt(&pkt);
        }

        nk_tree_pop(nk);
    }
    }

    nk_end(nk);
}