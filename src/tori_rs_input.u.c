#ifndef TORI_RS_INPUT_U_C
#define TORI_RS_INPUT_U_C

#include "osrs/chat.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/minimenu_game.h"
#include "osrs/revconfig/uitree.h"
#include "tori_rs.h"

/* -------------------------------------------------------------------------
 * Coordinate transform: window pixels → game/buffer pixels.
 * For soft3D backends, the game blits a smaller buffer into a letterboxed
 * destination rect inside the window; we map the click into buffer space.
 * For hardware backends, SDL already reports logical (game-coord) pixels.
 * --------------------------------------------------------------------- */

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

/** Transform one point from window coords to game/buffer coords. */
static void
mouse_window_to_game(struct GGame* game, int wx, int wy, int* gx, int* gy)
{
    *gx = wx;
    *gy = wy;
    if( game->soft3d_mouse_from_window )
        game_map_soft3d_window_mouse_to_buffer(game, gx, gy);
}

/**
 * Populate game->mouse (game/buffer coords) from input->raw_mouse (window coords).
 * Called once per frame at the start of LibToriRS_GameProcessInput.
 *
 * Cursor: always transformed.
 * Persistent press/release coords: transformed on the frame they occur.
 * Per-frame click/press/release coords: transformed when the flag is set.
 */
static void
game_mouse_capture(struct GGame* game, struct GInput* input)
{
    /* Cursor */
    mouse_window_to_game(
        game,
        input->raw_mouse.cursor_x,
        input->raw_mouse.cursor_y,
        &game->mouse.cursor_x,
        &game->mouse.cursor_y);

    /* Wheel */
    game->mouse.wheel_delta = input->raw_mouse.wheel_delta;

    for( int b = 0; b < TORIRSM_COUNT; b++ )
    {
        struct MouseButtonState const* src = &input->raw_mouse.buttons[b];
        struct MouseButtonState*       dst = &game->mouse.buttons[b];

        dst->down              = src->down;
        dst->press_this_frame  = src->press_this_frame;
        dst->release_this_frame = src->release_this_frame;
        dst->click_this_frame  = src->click_this_frame;
        dst->press_timestamp   = src->press_timestamp;

        if( src->press_this_frame )
        {
            mouse_window_to_game(game, src->press_x, src->press_y, &dst->press_x, &dst->press_y);
        }
        if( src->release_this_frame )
        {
            mouse_window_to_game(
                game, src->release_x, src->release_y, &dst->release_x, &dst->release_y);
        }
        if( src->click_this_frame )
        {
            mouse_window_to_game(
                game,
                src->click_press_x,
                src->click_press_y,
                &dst->click_press_x,
                &dst->click_press_y);
            mouse_window_to_game(
                game,
                src->click_release_x,
                src->click_release_y,
                &dst->click_release_x,
                &dst->click_release_y);
        }
    }
}

void
LibToriRS_GameProcessInput(
    struct GGame* game,
    struct GInput* input)
{
    const int   target_input_fps   = 50;
    const float time_delta_step    = 1.0f / target_input_fps;
    const int   max_ticks_per_frame = 25;

    int time_quanta = 0;
    while( input->time_delta_accumulator_seconds > time_delta_step &&
           time_quanta < max_ticks_per_frame )
    {
        time_quanta++;
        input->time_delta_accumulator_seconds -= time_delta_step;
        game->cycles_elapsed++;
        if( !game->latched )
            game->cc++;
    }

    game_input_process_events(input);

    /* Populate game->mouse (game-coord snapshot) from raw window-coord snapshot. */
    game_mouse_capture(game, input);

    /* Fresh each GameStep. Must NOT be cleared in LibToriRS_FrameBegin: minimenu / iface may set
     * this during GameProcessInput and FrameEnd must see it to avoid duplicating the same left
     * click as a world walk (MOVE_GAMECLICK after MOVE_OPCLICK). */
    game->interface_consumed_click = 0;

    game_chat_process_input(game, input);

    for( int i = 0; i < time_quanta; i++ )
    {
        if( !game_chat_is_typing(game) )
        {
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
                game->camera_world_y -= game->camera_movement_speed;
            if( game_input_keydown_or_pressed(input, TORIRSK_F) )
                game->camera_world_y += game->camera_movement_speed;

            if( game_input_keydown_or_pressed(input, TORIRSK_UP) )
                game->camera_pitch = (game->camera_pitch + game->camera_rotation_speed) % 2048;
            if( game_input_keydown_or_pressed(input, TORIRSK_DOWN) )
                game->camera_pitch =
                    (game->camera_pitch - game->camera_rotation_speed + 2048) % 2048;

            if( game_input_keydown_or_pressed(input, TORIRSK_LEFT) )
                game->camera_yaw = (game->camera_yaw + game->camera_rotation_speed) % 2048;
            if( game_input_keydown_or_pressed(input, TORIRSK_RIGHT) )
                game->camera_yaw =
                    (game->camera_yaw - game->camera_rotation_speed + 2048) % 2048;

            if( game_input_keydown_or_pressed(input, TORIRSK_SPACE) )
                game->cc = 0;
            if( game_input_keydown_or_pressed(input, TORIRSK_I) )
                game->latched = !game->latched;
            if( game_input_keydown_or_pressed(input, TORIRSK_J) )
                game->cc += 1;
            if( game_input_keydown_or_pressed(input, TORIRSK_K) )
                game->cc -= 1;
            if( game_input_keydown_or_pressed(input, TORIRSK_L) )
                game->cc += 100;
            if( game_input_keydown_or_pressed(input, TORIRSK_COMMA) )
                game->cc -= 100;
        } /* !game_chat_is_typing */

        if( game_input_keydown_or_pressed(input, TORIRSK_9) && game->mouse_tile_x != -1 )
        {
            int sx = game->mouse_tile_x;
            int sz = game->mouse_tile_z;
            int sl = game->mouse_tile_level;
            struct ScriptArgs args = {
                .tag = SCRIPT_SPAWN_ELEMENT,
                .u.spawn_element =
                    {
                        .world_x     = sx * 128 + 64,
                        .world_z     = sz * 128 + 64,
                        .world_level = sl,
                        .model_id    = 3081,
                        .seq_id      = 659,
                    },
            };
            script_queue_push(&game->script_queue, &args);
        }
    }

    if( input->quit )
    {
        game->running = false;
    }
    else if( game_input_keydown_or_pressed(input, TORIRSK_ESCAPE) )
    {
        if( game->chat && game->chat->chat_typing_active )
        {
            game->chat->chat_typing_active = 0;
            game->chat->chat_input[0]      = '\0';
        }
        else
        {
            game->running = false;
        }
    }

    /* ---- Inventory drag: press/release edge detection ---- */
    bool const left_down = game->mouse.buttons[TORIRSM_LEFT].down;
    int  const mx        = game->mouse.cursor_x;
    int  const my        = game->mouse.cursor_y;

    if( game->ui_root_buffer && game->iface && mx >= 0 && my >= 0 )
    {
        if( game->mouse.buttons[TORIRSM_LEFT].press_this_frame )
            interface_inv_try_drag_mouse_down(game, mx, my);

        /* Client.ts increments objDragCycles once per mainloop tick, and GameShell
         * runs up to ~50 ticks per second regardless of render frame rate.  The C
         * port calls GameProcessInput once per rendered frame, so we advance the
         * cycle counter by time_quanta (the number of logical 1/50s ticks that
         * elapsed this frame) to keep the >= 5 threshold consistent with TS. */
        int const n_drag_ticks = time_quanta > 0 ? time_quanta : 1;
        for( int i = 0; i < n_drag_ticks; i++ )
            interface_inv_drag_tick(game);

        if( game->mouse.buttons[TORIRSM_LEFT].release_this_frame )
            interface_inv_try_drag_mouse_up(game, input);
    }

    /* ---- Release: clear scrollbar drag and scroll cycle ---- */
    if( game->mouse.buttons[TORIRSM_LEFT].release_this_frame )
    {
        if( game->ui_root_buffer )
            game->ui_root_buffer->ui_scrollbar_drag_component_id = -1;
        game->scroll_cycle = 0;
    }

    /* ---- Held-button scrollbar ---- */
    if( left_down && game->ui_root_buffer && game->iface && mx >= 0 && my >= 0 )
    {
        game->scroll_cycle++;

        if( game->ui_root_buffer->ui_scrollbar_drag_component_id >= 0 )
        {
            struct UITreeScrollbarHit sb_drag;
            if( uitree_find_scrollbar_at(game, mx, my, &sb_drag) &&
                sb_drag.component_id ==
                    game->ui_root_buffer->ui_scrollbar_drag_component_id )
            {
                interface_handle_scrollbar_drag(
                    game,
                    sb_drag.component_id,
                    sb_drag.layer_y,
                    sb_drag.layer_height,
                    sb_drag.scroll_height,
                    my);
            }
            else
            {
                struct UITreeScrollbarHit sb_any;
                if( uitree_find_scrollbar_at(game, mx, my, &sb_any) )
                {
                    interface_handle_scrollbar_drag(
                        game,
                        game->ui_root_buffer->ui_scrollbar_drag_component_id,
                        sb_any.layer_y,
                        sb_any.layer_height,
                        sb_any.scroll_height,
                        my);
                }
            }
        }
        else
        {
            struct UITreeScrollbarHit sb_hold;
            if( uitree_find_scrollbar_at(game, mx, my, &sb_hold) )
            {
                if( sb_hold.region == 0 )
                    interface_handle_scrollbar_arrow_step(
                        game, sb_hold.component_id, sb_hold.max_scroll, 1, game->scroll_cycle * 4);
                else if( sb_hold.region == 1 )
                    interface_handle_scrollbar_arrow_step(
                        game, sb_hold.component_id, sb_hold.max_scroll, 0, game->scroll_cycle * 4);
                else if( sb_hold.region == 2 || sb_hold.region == 3 )
                {
                    game->ui_root_buffer->ui_scrollbar_drag_component_id = sb_hold.component_id;
                    interface_handle_scrollbar_drag(
                        game,
                        sb_hold.component_id,
                        sb_hold.layer_y,
                        sb_hold.layer_height,
                        sb_hold.scroll_height,
                        my);
                }
            }
        }
    }

    /* ---- Wheel scrollbar ---- */
    if( game->mouse.wheel_delta != 0 && game->ui_root_buffer && game->iface && mx >= 0 &&
        my >= 0 )
    {
        int32_t layer_idx = uitree_innermost_scroll_layer_at(game, mx, my);
        if( layer_idx >= 0 )
        {
            struct StaticUIComponent* L = &game->ui_root_buffer->components[layer_idx];
            if( L->component_id >= 0 )
            {
                int lh        = L->position.height;
                int sh        = L->u.rs_layer.scroll_height;
                int max_scroll = sh - lh;
                if( max_scroll > 0 )
                {
                    int up = (game->mouse.wheel_delta > 0) ? 1 : 0;
                    interface_handle_scrollbar_arrow_step(
                        game, L->component_id, max_scroll, up, 24);
                }
            }
        }
    }

    /* ---- Left click dispatch ---- */
    if( game->mouse.buttons[TORIRSM_LEFT].click_this_frame )
    {
        int cx = game->mouse.buttons[TORIRSM_LEFT].click_press_x;
        int cy = game->mouse.buttons[TORIRSM_LEFT].click_press_y;

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
                    interface_handle_scrollbar_click(
                        game,
                        hit.component_id,
                        hit.layer_y,
                        hit.layer_height,
                        hit.scroll_height,
                        cy);
                    game->ui_root_buffer->ui_scrollbar_drag_component_id = hit.component_id;
                }
                else if( hit.region == 3 )
                {
                    game->ui_root_buffer->ui_scrollbar_drag_component_id = hit.component_id;
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
        else if( game->minimenu.visible )
        {
            int opt = minimenu_game_click_option(game, cx, cy);
            if( opt >= 0 )
                minimenu_game_use_option(game, input, opt);
            game->minimenu.visible         = 0;
            game->interface_consumed_click = 1;
        }
        else if( game->iface && game->iface->inv_suppress_next_left_click )
        {
            game->iface->inv_suppress_next_left_click = 0;
            game->interface_consumed_click            = 1;
        }
        else if( game->ui_root_buffer && game->iface && cx >= 0 && cy >= 0 &&
                 interface_point_in_ui_inv_primary_region(cx, cy) &&
                 !chat_builtin_click_is_chrome(game, cx, cy) )
        {
            if( minimenu_game_use_primary_at(game, input, cx, cy) )
                game->interface_consumed_click = 1;
        }
        /* else: world/minimap click is handled in FrameEnd via game->mouse state */
    }

    /* ---- Right click: show context menu ---- */
    if( game->mouse.buttons[TORIRSM_RIGHT].click_this_frame )
    {
        int cx = game->mouse.buttons[TORIRSM_RIGHT].click_press_x;
        int cy = game->mouse.buttons[TORIRSM_RIGHT].click_press_y;
        game->mouse_cycle = 0;
        minimenu_game_show(game, cx, cy);
    }

    game_input_frame_reset(input);
}

#endif
