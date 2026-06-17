#include "runescape.h"

#include "../gamecache/gamecache.h"
#include "../world/world_builder.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define RUNESCAPE_CAMERA_MOVEMENT_SPEED 70

static bool
game_runescape_translate_gc_event(
    const struct ToriDraw_GCEvent* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));

    switch( ev->kind )
    {
    case TDGC_MODEL_LOAD:
        command->kind = TORIRSRC_MODEL_LOAD;
        command->u.model_load.element_id = ev->element_id;
        command->u.model_load.model = ev->model;
        command->u.model_load.world_position = ev->world_position;
        return true;
    case TDGC_MODEL_UNLOAD:
        command->kind = TORIRSRC_MODEL_UNLOAD;
        command->u.model_load.element_id = ev->element_id;
        return true;
    case TDGC_BATCH_BEGIN:
        command->kind = TORIRSRC_BATCH3D_BEGIN;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TDGC_BATCH_MODEL_ADD:
        command->kind = TORIRSRC_BATCH3D_MODEL_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TDGC_BATCH_ANIM_ADD:
        command->kind = TORIRSRC_BATCH3D_ANIM_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TDGC_BATCH_END:
        command->kind = TORIRSRC_BATCH3D_END;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TDGC_BATCH_CLEAR:
        command->kind = TORIRSRC_BATCH3D_CLEAR;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TDGC_ANIM_LOAD:
        command->kind = TORIRSRC_ANIM_LOAD;
        command->u.anim_load.element_id = ev->element_id;
        command->u.anim_load.animation = ev->animation;
        return true;
    case TDGC_ANIM_UNLOAD:
        command->kind = TORIRSRC_ANIM_UNLOAD;
        command->u.anim_load.element_id = ev->element_id;
        return true;
    case TDGC_TEX_LOAD:
        command->kind = TORIRSRC_TEX_LOAD;
        command->u.tex_load.texture_id = ev->texture_id;
        command->u.tex_load.texture = ev->texture;
        return true;
    case TDGC_TEX_UNLOAD:
        command->kind = TORIRSRC_TEX_UNLOAD;
        command->u.tex_load.texture_id = ev->texture_id;
        command->u.tex_load.texture = NULL;
        return true;
    default:
        return false;
    }
}

static void
game_runescape_update_world_viewport(struct GameRunescape* game)
{
    int vw = game->view_port ? game->view_port->width : 800;
    int vh = game->view_port ? game->view_port->height : 600;
    int stride = game->view_port ? game->view_port->stride : vw;

    game->world_view_port.width = vw;
    game->world_view_port.height = vh;
    game->world_view_port.stride = stride;
    game->world_view_port.x_center = vw / 2;
    game->world_view_port.y_center = vh / 2;
}

static bool
game_runescape_emit_draw_element(
    struct GameRunescape* game,
    int element_id,
    struct LibToriRS_RenderCommand* command)
{
    if( !toridraw_gc_element_is_live(game->context, element_id) )
        return false;

    struct ToriDraw_GCElement* element = toridraw_gc_element_get(game->context, element_id);
    if( !element || element->model.kind != TORIDRAWMK_MODEL )
        return false;
    if( !toridraw_model_get_bounds_cylinder(element->model) )
        return false;

    struct ToriDraw_Position rel_pos = element->world_position;
    rel_pos.x -= game->camera_position->x;
    rel_pos.y -= game->camera_position->y;
    rel_pos.z -= game->camera_position->z;
    rel_pos.yaw = toridraw_normalize_angle(element->world_position.yaw);

    if( game->context )
    {
        const int cull = toridraw_render_model1_project(
            element->model, game->context, &rel_pos, &game->world_view_port, game->camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
            return false;
        if( toridraw_render_model2_sort_faces(element->model, game->context) <= 0 )
            return false;
    }

    command->kind = TORIRSRC_DRAW_MODEL;
    command->u.model.model = element->model;
    command->u.model.element_id = element_id;
    command->u.model.position = rel_pos;
    command->u.model.animation = element->animation;
    command->u.model.anim_frame = element->anim_frame;

    return true;
}

static void
game_runescape_move_forward(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = toridraw_sin(game->camera->yaw);
    int direction_z = toridraw_cos(game->camera->yaw);
    game->camera_position->x -= (direction_x * amount) >> 16;
    game->camera_position->z += (direction_z * amount) >> 16;
}

static void
game_runescape_move_left(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = toridraw_cos(game->camera->yaw);
    int direction_z = toridraw_sin(game->camera->yaw);
    game->camera_position->x += (direction_x * amount) >> 16;
    game->camera_position->z += (direction_z * amount) >> 16;
}

static void
game_runescape_move_right(
    struct GameRunescape* game,
    int amount)
{
    game_runescape_move_left(game, -amount);
}

static void
game_runescape_camera_tile(
    const struct GameRunescape* game,
    int* out_sx,
    int* out_sz,
    int* out_slevel)
{
    *out_sx = game->camera_position->x / 128;
    *out_sz = game->camera_position->z / 128;
    *out_slevel = game->camera_position->y / 240;
}

struct GameRunescape*
game_runescape_new(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Context* context)
{
    struct GameRunescape* game = calloc(1, sizeof(struct GameRunescape));
    assert(game && "game_runescape_new: failed to allocate game");

    game->script_queue = script_queue;
    game->context = context;
    game->zone_center_x = RUNESCAPE_ZONE_CENTER_X;
    game->zone_center_z = RUNESCAPE_ZONE_CENTER_Z;

    game->camera_position = calloc(1, sizeof(struct ToriDraw_Position));
    game->camera = calloc(1, sizeof(struct ToriDraw_Camera));
    game->view_port = calloc(1, sizeof(struct ToriDraw_ViewPort));
    assert(game->camera_position && "game_runescape_new: failed to allocate camera position");
    assert(game->camera && "game_runescape_new: failed to allocate camera");
    assert(game->view_port && "game_runescape_new: failed to allocate view port");

    game->camera_position->z = -800;
    game->camera->fov_rpi2048 = 512;
    game->camera->near_plane_z = 50;
    game->camera->pitch = 148;
    game->view_port->width = 765;
    game->view_port->height = 503;
    game->view_port->stride = 765;
    game->view_port->x_center = 382;
    game->view_port->y_center = 251;

    assert(game->context && "game_runescape_new: failed to allocate context");

    game->world = world_new();
    assert(game->world && "game_runescape_new: failed to allocate world");

    game->painter_buffer = painter_buffer_new();
    assert(game->painter_buffer && "game_runescape_new: failed to allocate painter buffer");

    return game;
}

void
game_runescape_free(struct GameRunescape* game)
{
    if( !game )
        return;
    if( game->painter_buffer )
    {
        free(game->painter_buffer->commands);
        free(game->painter_buffer);
    }
    if( game->world )
        world_free(game->world);
    free(game->camera_position);
    free(game->camera);
    free(game->view_port);
    free(game);
}

void
game_runescape_set_gamecache(
    struct GameRunescape* game,
    struct GameCache* gamecache)
{
    if( !game )
        return;
    game->gamecache = gamecache;
}

void
game_runescape_set_toridrawx(
    struct GameRunescape* game,
    struct ToriDrawX* toridrawx)
{
    if( !game )
        return;
    game->toridrawx = toridrawx;
}

void
game_runescape_build_world(struct GameRunescape* game)
{
    struct WorldBuilder* builder =
        world_builder_new(game->world, game->gamecache, game->context, game->toridrawx);
    assert(builder && "game_runescape_build_world: failed to allocate world builder");
    world_builder_rebuild_centerzone(builder, game->zone_center_x, game->zone_center_z, 104);
    world_builder_free(builder);
    game->world_built = true;

    if( game->camera_position && game->camera )
    {
        int const scene_center = (104 / 2) * 128;
        game->camera_position->x = scene_center;
        game->camera_position->z = scene_center - 1500;
        game->camera_position->y = -2000;
        game->camera->pitch = 450;
        game->camera->yaw = 0;
    }
}

void
game_runescape_process_input(
    struct GameRunescape* game,
    struct LibToriRS_Input* input)
{
    const int move = RUNESCAPE_CAMERA_MOVEMENT_SPEED;
    const int rotate = 10;

    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
        game_runescape_move_forward(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
        game_runescape_move_forward(game, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
        game_runescape_move_right(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
        game_runescape_move_left(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
        game->camera_position->y -= move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
        game->camera_position->y += move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
        game->camera->yaw = toridraw_add_angle(game->camera->yaw, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
        game->camera->yaw = toridraw_add_angle(game->camera->yaw, -rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
        game->camera->pitch = toridraw_add_angle(game->camera->pitch, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
        game->camera->pitch = toridraw_add_angle(game->camera->pitch, -rotate);
}

void
game_runescape_frame_begin(struct GameRunescape* game)
{
    game->frame.phase = RS_FRAME_PHASE_GC_EVENTS;
    game->frame.event_index = 0;
    game->frame.element_index = 0;
    game->frame.painter_command_index = 0;
    game->frame.world_emitted = false;
    game->frame.painter_paint_done = false;
    game_runescape_update_world_viewport(game);
}

bool
game_runescape_frame_next_command(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        switch( game->frame.phase )
        {
        case RS_FRAME_PHASE_GC_EVENTS:
        {
            struct ToriDraw_GCEventQueue* eq =
                game->context ? toridraw_gc_events(game->context) : NULL;
            if( eq )
            {
                while( game->frame.event_index < eq->count )
                {
                    const struct ToriDraw_GCEvent* ev = &eq->events[game->frame.event_index++];
                    if( game_runescape_translate_gc_event(ev, command) )
                        return true;
                }
            }
            game->frame.phase = RS_FRAME_PHASE_BEGIN_3D;
            game->frame.event_index = 0;
            continue;
        }
        case RS_FRAME_PHASE_BEGIN_3D:
        {
            if( game->frame.world_emitted )
            {
                game->frame.phase = RS_FRAME_PHASE_END_3D;
                continue;
            }

            command->kind = TORIRSRC_BEGIN_3D;
            command->u.begin_3d.view_port = game->world_view_port;
            command->u.begin_3d.camera = *game->camera;
            command->u.begin_3d.camera_position.x = -game->camera_position->x;
            command->u.begin_3d.camera_position.y = -game->camera_position->y;
            command->u.begin_3d.camera_position.z = -game->camera_position->z;
            game->frame.world_emitted = true;
            game->frame.phase = RS_FRAME_PHASE_MODELS;
            game->frame.element_index = 0;
            game->frame.painter_command_index = 0;
            game->frame.painter_paint_done = false;
            return true;
        }
        case RS_FRAME_PHASE_MODELS:
        {
            struct World* world = game->world;
            if( world && world->load_complete && world->painter && game->painter_buffer &&
                !game->frame.painter_paint_done )
            {
                painter_set_camera_angles(world->painter, game->camera->pitch, game->camera->yaw);
                painter_set_level_mask(world->painter, 0xF);
                int camera_sx;
                int camera_sz;
                int camera_slevel;
                game_runescape_camera_tile(game, &camera_sx, &camera_sz, &camera_slevel);
                painter_paint_bucket(
                    world->painter, game->painter_buffer, camera_sx, camera_sz, camera_slevel);
                game->frame.painter_paint_done = true;
            }

            if( world && world->load_complete && world->painter && game->painter_buffer &&
                game->frame.painter_paint_done )
            {
                while( game->frame.painter_command_index < game->painter_buffer->command_count )
                {
                    const struct PaintersElementCommand* cmd =
                        &game->painter_buffer->commands[game->frame.painter_command_index++];
                    int element_id = -1;

                    switch( cmd->_bf_kind )
                    {
                    case PNTR_CMD_ELEMENT:
                        element_id = (int)cmd->_entity._bf_entity;
                        break;
                    case PNTR_CMD_TERRAIN:
                        element_id = world_terrain_element_at(
                            world,
                            (int)cmd->_terrain._bf_terrain_x,
                            (int)cmd->_terrain._bf_terrain_z,
                            (int)cmd->_terrain._bf_terrain_y);
                        break;
                    default:
                        break;
                    }

                    if( element_id < 0 )
                        continue;
                    if( game_runescape_emit_draw_element(game, element_id, command) )
                        return true;
                }

                game->frame.phase = RS_FRAME_PHASE_END_3D;
                continue;
            }

            int slot_count = toridraw_gc_element_slot_count(game->context);
            while( game->frame.element_index < slot_count )
            {
                int element_id = game->frame.element_index++;
                if( game_runescape_emit_draw_element(game, element_id, command) )
                    return true;
            }

            game->frame.phase = RS_FRAME_PHASE_END_3D;
            continue;
        }
        case RS_FRAME_PHASE_END_3D:
            command->kind = TORIRSRC_END_3D;
            game->frame.phase = RS_FRAME_PHASE_DONE;
            return true;
        default:
            return false;
        }
    }
}

void
game_runescape_frame_end(struct GameRunescape* game)
{
    toridraw_gc_frame_end(game->context);

    game->frame.phase = RS_FRAME_PHASE_DONE;
}
