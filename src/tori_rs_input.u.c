#ifndef TORI_RS_INPUT_U_C
#define TORI_RS_INPUT_U_C

#include "tori_rs.h"

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
        if( game->mouse_cycle < 400 && game->mouse_cycle != -1 )
        {
            game->mouse_cycle += 20;
            if( game->mouse_cycle >= 400 )
                game->mouse_cycle = -1;
        }

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
            if( input->events[i].click.button == TORIRSM_LEFT )
            {
                game->mouse_clicked = true;
                game->mouse_cycle = 0;
                game->mouse_clicked_x = input->events[i].click.start_mouse_x;
                game->mouse_clicked_y = input->events[i].click.start_mouse_y;
            }
            else if( input->events[i].click.button == TORIRSM_RIGHT )
            {
                game->mouse_clicked_right = true;
                game->mouse_cycle = 0;
                game->mouse_clicked_right_x = input->events[i].click.start_mouse_x;
                game->mouse_clicked_right_y = input->events[i].click.start_mouse_y;
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