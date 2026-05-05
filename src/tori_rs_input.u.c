#ifndef TORI_RS_INPUT_U_C
#define TORI_RS_INPUT_U_C

#include "tori_rs.h"

#include "osrs/core/clientprot_core.h"
#include "osrs/interface.h"
#include "osrs/minimenu.h"
#include "osrs/revconfig/uitree.h"

static void
game_map_soft3d_window_mouse_to_buffer(
    struct GGame* game,
    int* px,
    int* py)
{
    int x = *px;
    int y = *py;
    int dx = game->soft3d_present_dst_x;
    int dy = game->soft3d_present_dst_y;
    int dw = game->soft3d_present_dst_w;
    int dh = game->soft3d_present_dst_h;
    int bw = game->soft3d_buffer_w;
    int bh = game->soft3d_buffer_h;

    if( dw <= 0 || dh <= 0 || bw <= 0 || bh <= 0 )
        return;

    x -= dx;
    y -= dy;
    if( x < 0 || y < 0 || x >= dw || y >= dh )
    {
        *px = -1;
        *py = -1;
        return;
    }

    long long nx = (long long)x * (long long)bw;
    long long ny = (long long)y * (long long)bh;
    *px = (int)(nx / (long long)dw);
    *py = (int)(ny / (long long)dh);
    if( *px < 0 )
        *px = 0;
    if( *py < 0 )
        *py = 0;
    if( *px >= bw )
        *px = bw - 1;
    if( *py >= bh )
        *py = bh - 1;
}

void
LibToriRS_GameProcessInput(
    struct GGame* game,
    struct GInput* input)
{
    // IO
    const int target_input_fps = 50;
    const float time_delta_step = 1.0f / target_input_fps;
    const int max_ticks_per_frame = 25;

    int time_quanta = 0;
    while(
        input->time_delta_accumulator_seconds > time_delta_step &&
        time_quanta < max_ticks_per_frame )
    {
        time_quanta++;
        input->time_delta_accumulator_seconds -= time_delta_step;
        game->cycles_elapsed++;
        if( !game->latched )
            game->cc++;
    }

    game_input_process_events(input);

    for( int i = 0; i < time_quanta; i++ )
    {
        // if( game->mouse_cycle < 400 && game->mouse_cycle != -1 )
        // {
        //     game->mouse_cycle += 20;
        //     if( game->mouse_cycle >= 400 )
        //         game->mouse_cycle = -1;
        // }

        if( game_input_keydown_or_pressed(input, TORIRSK_W) )
        {
            game->camera_world_x -=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_S) )
        {
            game->camera_world_x +=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_A) )
        {
            game->camera_world_x -=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_D) )
        {
            game->camera_world_x +=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_R) )
        {
            game->camera_world_y -= game->camera_movement_speed;
        }
        if( game_input_keydown_or_pressed(input, TORIRSK_F) )
        {
            game->camera_world_y += game->camera_movement_speed;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_UP) )
        {
            game->camera_pitch = (game->camera_pitch + game->camera_rotation_speed) % 2048;
        }
        if( game_input_keydown_or_pressed(input, TORIRSK_DOWN) )
        {
            game->camera_pitch = (game->camera_pitch - game->camera_rotation_speed + 2048) % 2048;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_LEFT) )
        {
            game->camera_yaw = (game->camera_yaw + game->camera_rotation_speed) % 2048;
        }
        if( game_input_keydown_or_pressed(input, TORIRSK_RIGHT) )
        {
            game->camera_yaw = (game->camera_yaw - game->camera_rotation_speed + 2048) % 2048;
        }

        if( input->quit || game_input_keydown_or_pressed(input, TORIRSK_ESCAPE) )
        {
            game->running = false;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_SPACE) )
        {
            game->cc = 0;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_I) )
        {
            game->latched = !game->latched;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_J) )
        {
            game->cc += 1;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_K) )
        {
            game->cc -= 1;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_L) )
        {
            game->cc += 100;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_COMMA) )
        {
            game->cc -= 100;
        }

        if( game_input_keydown_or_pressed(input, TORIRSK_9) && game->mouse_tile_x != -1 )
        {
            int sx = game->mouse_tile_x;
            int sz = game->mouse_tile_z;
            int sl = game->mouse_tile_level;
            struct ScriptArgs args = {
                .tag = SCRIPT_SPAWN_ELEMENT,
                .u.spawn_element =
                    {
                        .world_x = sx * 128 + 64,
                        .world_z = sz * 128 + 64,
                        .world_level = sl,
                        .model_id = 3081,
                        .seq_id = 659,
                    },
            };
            script_queue_push(&game->script_queue, &args);
        }
    }

    game->mouse_x = input->mouse_state.x;
    game->mouse_y = input->mouse_state.y;
    if( game->soft3d_mouse_from_window )
    {
        game_map_soft3d_window_mouse_to_buffer(game, &game->mouse_x, &game->mouse_y);
    }

    game->mouse_clicked = false;
    game->mouse_clicked_right = false;

    /* Update mouse_button_down; clear scrollbar drag when left button is released. */
    int left_down = input->mouse_button_states[TORIRSM_LEFT].down;
    if( game->mouse_button_down && !left_down )
    {
        /* Mouse-up: release scrollbar drag and reset scrollCycle. */
        game->ui_scrollbar_drag_component_id = -1;
        game->scroll_cycle = 0;
    }
    game->mouse_button_down = left_down;

    /* Per-frame scrollbar logic: mirrors Client.ts doScrollbar + scrollCycle.
     * While the left button is held:
     *  - arrows (region 0/1): scroll by 4 px per frame (scrollCycle * 4)
     *  - track/grip (region 2/3) or an active drag id: recompute scrollPos from
     *    mouse Y each frame (Client.ts 10543–10558).  scrollInputPadding is mirrored
     *    by keeping game->ui_scrollbar_drag_component_id set while dragging. */
    if( left_down && game->ui_root_buffer && game->iface &&
        game->mouse_x >= 0 && game->mouse_y >= 0 )
    {
        game->scroll_cycle++;

        /* If already dragging a specific component, continue dragging it even if the
         * mouse drifts off the scrollbar (mirrors scrollInputPadding ±32px). */
        if( game->ui_scrollbar_drag_component_id >= 0 )
        {
            /* Re-find the scrollbar to get current geometry. */
            struct UITreeScrollbarHit sb_drag;
            if( uitree_find_scrollbar_at(game, game->mouse_x, game->mouse_y, &sb_drag) &&
                sb_drag.component_id == game->ui_scrollbar_drag_component_id )
            {
                interface_handle_scrollbar_drag(
                    game,
                    sb_drag.component_id,
                    sb_drag.layer_y,
                    sb_drag.layer_height,
                    sb_drag.scroll_height,
                    game->mouse_y);
            }
            else
            {
                /* Mouse drifted off; still update by looking up component directly. */
                struct UITreeScrollbarHit sb_any;
                if( uitree_find_scrollbar_at(game, game->mouse_x, game->mouse_y, &sb_any) )
                {
                    interface_handle_scrollbar_drag(
                        game,
                        game->ui_scrollbar_drag_component_id,
                        sb_any.layer_y,
                        sb_any.layer_height,
                        sb_any.scroll_height,
                        game->mouse_y);
                }
            }
        }
        else
        {
            struct UITreeScrollbarHit sb_hold;
            if( uitree_find_scrollbar_at(game, game->mouse_x, game->mouse_y, &sb_hold) )
            {
                if( sb_hold.region == 0 ) /* up arrow */
                    interface_handle_scrollbar_arrow_step(
                        game, sb_hold.component_id, sb_hold.max_scroll, 1, game->scroll_cycle * 4);
                else if( sb_hold.region == 1 ) /* down arrow */
                    interface_handle_scrollbar_arrow_step(
                        game, sb_hold.component_id, sb_hold.max_scroll, 0, game->scroll_cycle * 4);
                else if( sb_hold.region == 2 || sb_hold.region == 3 )
                {
                    /* Track or grip: engage drag mode and compute position from mouse. */
                    game->ui_scrollbar_drag_component_id = sb_hold.component_id;
                    interface_handle_scrollbar_drag(
                        game,
                        sb_hold.component_id,
                        sb_hold.layer_y,
                        sb_hold.layer_height,
                        sb_hold.scroll_height,
                        game->mouse_y);
                }
            }
        }
    }

    for( int i = 0; i < input->event_count; i++ )
    {
        switch( input->events[i].type )
        {
        case TORIRSEV_MOUSE_WHEEL:
        {
            int mx = input->events[i].mouse_wheel.mouse_x;
            int my = input->events[i].mouse_wheel.mouse_y;
            int wh = input->events[i].mouse_wheel.wheel;
            if( game->soft3d_mouse_from_window )
                game_map_soft3d_window_mouse_to_buffer(game, &mx, &my);
            if( !game->ui_root_buffer || !game->iface || mx < 0 || my < 0 )
                break;
            int32_t layer_idx = uitree_innermost_scroll_layer_at(game, mx, my);
            if( layer_idx < 0 )
                break;
            struct StaticUIComponent* L = &game->ui_root_buffer->components[layer_idx];
            if( L->component_id < 0 )
                break;
            int lh = L->position.height;
            int sh = L->u.rs_layer.scroll_height;
            int max_scroll = sh - lh;
            if( max_scroll <= 0 )
                break;
            int up = (wh > 0) ? 1 : 0;
            interface_handle_scrollbar_arrow_step(
                game, L->component_id, max_scroll, up, 24);
        }
        break;
        case TORIRSEV2_CLICK:
        {
            int button = input->events[i].click.button;
            if( button == TORIRSM_LEFT )
            {
                int cx = input->events[i].click.start_mouse_x;
                int cy = input->events[i].click.start_mouse_y;
                if( game->soft3d_mouse_from_window )
                    game_map_soft3d_window_mouse_to_buffer(game, &cx, &cy);
                int consumed_sb = 0;
                if( game->ui_root_buffer && game->iface && cx >= 0 && cy >= 0 )
                {
                    struct UITreeScrollbarHit hit;
                    if( uitree_find_scrollbar_at(game, cx, cy, &hit) )
                    {
                        if( hit.region == 0 )
                            interface_handle_scrollbar_arrow_step(
                                game, hit.component_id, hit.max_scroll, 1, 4);
                        else if( hit.region == 1 )
                            interface_handle_scrollbar_arrow_step(
                                game, hit.component_id, hit.max_scroll, 0, 4);
                        else if( hit.region == 2 )
                        {
                            /* Track click: jump scroll position; also begin drag so
                             * holding continues to track (mirrors TS first frame of drag). */
                            interface_handle_scrollbar_click(
                                game,
                                hit.component_id,
                                hit.layer_y,
                                hit.layer_height,
                                hit.scroll_height,
                                cy);
                            game->ui_scrollbar_drag_component_id = hit.component_id;
                        }
                        else if( hit.region == 3 )
                        {
                            /* Grip click: begin drag immediately. */
                            game->ui_scrollbar_drag_component_id = hit.component_id;
                            interface_handle_scrollbar_drag(
                                game,
                                hit.component_id,
                                hit.layer_y,
                                hit.layer_height,
                                hit.scroll_height,
                                cy);
                        }
                        consumed_sb = 1;
                    }
                }
                if( consumed_sb )
                {
                    game->interface_consumed_click = 1;
                }
                else if( game->minimenu_visible )
                {
                    /* Dispatch to context menu; close regardless of where click lands. */
                    int opt = minimenu_click_option(game, cx, cy);
                    if( opt >= 0 )
                        minimenu_use_option(game, opt);
                    /* minimenu_click_option already cleared minimenu_visible for outside clicks.
                     * For header or option clicks, close explicitly. */
                    game->minimenu_visible = 0;
                    game->interface_consumed_click = 1;
                }
                else
                {
                    game->mouse_clicked = true;
                    game->mouse_cycle = 0;
                    game->mouse_clicked_x = cx;
                    game->mouse_clicked_y = cy;

                    /* Emit mouse-click event to server. */
                    if( GAME_NET_STATE_GAME == game->net_state )
                        clientprot_event_mouse_click(game, 1, cx, cy, game->cycle);
                }
            }
            else if( button == TORIRSM_RIGHT )
            {
                int cx = input->events[i].click.start_mouse_x;
                int cy = input->events[i].click.start_mouse_y;
                if( game->soft3d_mouse_from_window )
                    game_map_soft3d_window_mouse_to_buffer(game, &cx, &cy);
                game->mouse_clicked_right   = true;
                game->mouse_cycle           = 0;
                game->mouse_clicked_right_x = cx;
                game->mouse_clicked_right_y = cy;
                /* Open context menu immediately on right-click. */
                minimenu_show(game, cx, cy);
            }
        }
        break;
        default:
            break;
        }
    }

    if( time_quanta > 0 )
    {
        game_input_frame_reset(input);
    }
}

#endif