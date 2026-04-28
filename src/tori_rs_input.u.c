#ifndef TORI_RS_INPUT_U_C
#define TORI_RS_INPUT_U_C

#include "tori_rs.h"

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

    int time_quanta = 0;
    while( input->time_delta_accumulator_seconds > time_delta_step )
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
    game->mouse_clicked = false;
    for( int i = 0; i < input->event_count; i++ )
    {
        switch( input->events[i].type )
        {
        case TORIRSEV2_CLICK:
        {
            int button = input->events[i].click.button;
            if( button == TORIRSM_LEFT )
            {
                int cx = input->events[i].click.start_mouse_x;
                int cy = input->events[i].click.start_mouse_y;
                if( game->soft3d_mouse_from_window )
                    game_map_soft3d_window_mouse_to_buffer(game, &cx, &cy);
                game->mouse_clicked = true;
                game->mouse_cycle = 0;
                game->mouse_clicked_x = cx;
                game->mouse_clicked_y = cy;
            }
            else if( button == TORIRSM_RIGHT )
            {
                int cx = input->events[i].click.start_mouse_x;
                int cy = input->events[i].click.start_mouse_y;
                if( game->soft3d_mouse_from_window )
                    game_map_soft3d_window_mouse_to_buffer(game, &cx, &cy);
                game->mouse_clicked_right = true;
                game->mouse_cycle = 0;
                game->mouse_clicked_right_x = cx;
                game->mouse_clicked_right_y = cy;
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