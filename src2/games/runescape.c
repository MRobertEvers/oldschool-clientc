#include "runescape.h"

#include "../buildcache/dat1_buildcache.h"
#include "../toriauxlib/c/dat1io.h"
#include "../toriauxlib/c/toriauxlibc.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "../world/heightmap.h"
#include "../world/world_builder.h"
#include "3rd/minipt.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNESCAPE_CAMERA_MOVEMENT_SPEED 70
#define RUNESCAPE_PROJECTILE_VEL_EAST 1

static int
clamp_terrain_level(int level)
{
    if( level < 0 )
        return 0;
    if( level >= WORLD_MAP_TERRAIN_LEVELS )
        return WORLD_MAP_TERRAIN_LEVELS - 1;
    return level;
}

int
game_runescape_camera_terrain_level(const struct GameRunescape* game)
{
    if( !game || !game->camera_position )
        return 0;
    return clamp_terrain_level(game->camera_position->y / 240);
}

static bool
game_runescape_translate_gc_event(
    const struct ToriDraw_Event* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));

    switch( ev->kind )
    {
    case TORIDRAW_EVENT_MODEL_LOAD:
        command->kind = TORIRSRC_MODEL_LOAD;
        command->u.model_load.element_id = ev->element_id;
        command->u.model_load.model = ev->model;
        command->u.model_load.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_MODEL_UNLOAD:
        command->kind = TORIRSRC_MODEL_UNLOAD;
        command->u.model_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_BATCH_BEGIN:
        command->kind = TORIRSRC_BATCH3D_BEGIN;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_MODEL_ADD:
        command->kind = TORIRSRC_BATCH3D_MODEL_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_ANIM_ADD:
        command->kind = TORIRSRC_BATCH3D_ANIM_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.anim_index = ev->anim_index;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_END:
        command->kind = TORIRSRC_BATCH3D_END;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_CLEAR:
        command->kind = TORIRSRC_BATCH3D_CLEAR;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_ANIM_LOAD:
        command->kind = TORIRSRC_ANIM_LOAD;
        command->u.anim_load.element_id = ev->element_id;
        command->u.anim_load.animation = ev->animation;
        command->u.anim_load.model = ev->model;
        command->u.anim_load.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_ANIM_UNLOAD:
        command->kind = TORIRSRC_ANIM_UNLOAD;
        command->u.anim_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_TEX_LOAD:
        command->kind = TORIRSRC_TEX_LOAD;
        command->u.tex_load.texture_id = ev->texture_id;
        command->u.tex_load.texture = ev->texture;
        return true;
    case TORIDRAW_EVENT_TEX_UNLOAD:
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
    if( !ToriDraw_SceneElementIsLive(game->scene, element_id) )
        return false;

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( !element || element->model.kind != TORIDRAWMK_MODEL )
        return false;
    if( !ToriDraw_ModelGetBoundsCylinder(element->model) )
        return false;

    struct ToriDraw_Position rel_pos = element->world_position;
    rel_pos.x -= game->camera_position->x;
    rel_pos.y -= game->camera_position->y;
    rel_pos.z -= game->camera_position->z;
    rel_pos.yaw = ToriDraw_NormalizeAngle(element->world_position.yaw);

    if( element->anim_seq_id != -1 && element->animation )
        ToriDraw_SceneElementApplyAnimation(game->scene, element_id, true, element->anim_frame);

    if( game->scene )
    {
        const int cull = ToriDraw_RenderModel1Project(
            element->model, game->scene, &rel_pos, &game->world_view_port, game->camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
            return false;
        if( ToriDraw_RenderModel2SortFaces(element->model, game->scene) <= 0 )
            return false;
    }

    command->kind = TORIRSRC_DRAW_MODEL;
    command->u.model.model = element->model;
    command->u.model.element_id = element_id;
    command->u.model.position = rel_pos;
    command->u.model.world_position = element->world_position;
    command->u.model.animation = element->animation;
    command->u.model.anim_index = 0;
    command->u.model.anim_frame = element->anim_frame;
    command->u.model.dynamic = element->dynamic;

    return true;
}

static void
game_runescape_move_forward(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = ToriDraw_Sin(game->camera->yaw);
    int direction_z = ToriDraw_Cos(game->camera->yaw);
    game->camera_position->x -= (direction_x * amount) >> 16;
    game->camera_position->z += (direction_z * amount) >> 16;
}

static void
game_runescape_move_left(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = ToriDraw_Cos(game->camera->yaw);
    int direction_z = ToriDraw_Sin(game->camera->yaw);
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
    struct ToriDraw_Scene* scene)
{
    struct GameRunescape* game = calloc(1, sizeof(struct GameRunescape));
    assert(game && "game_runescape_new: failed to allocate game");

    game->script_queue = script_queue;
    game->scene = scene;
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

    assert(game->scene && "game_runescape_new: failed to allocate context");

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
game_runescape_set_core(
    struct GameRunescape* game,
    struct ToriAuxLibCore* gamecache)
{
    if( !game )
        return;
    game->core = gamecache;
}

void
game_runescape_set_td(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td)
{
    if( !game )
        return;
    game->td = td;
}

void
game_runescape_build_world(struct GameRunescape* game)
{
    struct WorldBuilder* builder =
        world_builder_new(game->world, game->core, game->scene, game->td);
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
        game->camera->yaw = ToriDraw_AddAngle(game->camera->yaw, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
        game->camera->yaw = ToriDraw_AddAngle(game->camera->yaw, -rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
        game->camera->pitch = ToriDraw_AddAngle(game->camera->pitch, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
        game->camera->pitch = ToriDraw_AddAngle(game->camera->pitch, -rotate);
}

static void
game_runescape_check_projectiles(struct GameRunescape* game)
{
    struct World* world = game->world;
    if( !world )
        return;

    for( int i = 0; i < world->projectile_count; i++ )
    {
        struct WorldProjectile* p = &world->projectiles[i];
        if( !p->alive || !p->has_dst )
            continue;
        if( p->pos_x >= p->dst_x )
            world_projectile_despawn(world, i);
    }
}

static void
game_runescape_drain_world_events(struct GameRunescape* game)
{
    struct World* world = game->world;
    if( !world || !game->scene )
        return;

    int count = world_events_count(world);
    for( int i = 0; i < count; i++ )
    {
        const struct WorldEvent* ev = world_events_peek(world, i);
        if( !ev )
            continue;
        if( ev->kind == WORLD_EVENT_ENTITY_REMOVED && ev->element_id >= 0 )
            ToriDraw_SceneElementRemove(game->scene, ev->element_id);
    }
    world_events_clear(world);
}

static void
game_runescape_sync_projectiles_to_scene(struct GameRunescape* game)
{
    struct World* world = game->world;
    if( !world || !game->scene || !world->load_complete )
        return;

    for( int i = 0; i < world->projectile_count; i++ )
    {
        struct WorldProjectile* p = &world->projectiles[i];
        if( !p->alive || p->element_id < 0 )
            continue;

        int pos_y = 0;
        if( world->heightmap )
            pos_y = heightmap_get_interpolated(world->heightmap, p->pos_x, p->pos_z, p->level);

        ToriDraw_SceneElementSetPosition(
            game->scene, p->element_id, p->pos_x, pos_y, p->pos_z, p->yaw);
    }
}

static void
game_runescape_tick_animations(struct GameRunescape* game)
{
    if( !game->scene )
        return;

    int slot_count = ToriDraw_SceneElementSlotCount(game->scene);
    for( int element_id = 0; element_id < slot_count; element_id++ )
    {
        if( !ToriDraw_SceneElementIsLive(game->scene, element_id) )
            continue;

        struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
        if( !element || element->anim_seq_id == -1 || !element->animation )
            continue;

        const struct ToriDraw_Animation* anim = element->animation;
        if( anim->frame_count <= 0 || !anim->frames )
            continue;

        element->anim_cycle++;
        const int delay = anim->frames[element->anim_frame].delay;
        if( element->anim_cycle >= delay )
        {
            element->anim_frame = (element->anim_frame + 1) % anim->frame_count;
            element->anim_cycle = 0;
        }
    }
}

void
game_runescape_frame_begin(
    struct GameRunescape* game,
    int cycles_elapsed)
{
    game->frame.phase = RS_FRAME_PHASE_GC_EVENTS;
    game->frame.event_index = 0;
    game->frame.element_index = 0;
    game->frame.painter_command_index = 0;
    game->frame.world_emitted = false;
    game->frame.painter_paint_done = false;
    for( int i = 0; i < cycles_elapsed; i++ )
        game_runescape_tick_animations(game);
    if( game->world )
        world_cycle(game->world, cycles_elapsed);
    game_runescape_check_projectiles(game);
    game_runescape_drain_world_events(game);
    game_runescape_sync_projectiles_to_scene(game);
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
            struct ToriDraw_EventQueue* eq = game->scene ? ToriDraw_SceneEvents(game->scene) : NULL;
            if( eq )
            {
                while( game->frame.event_index < eq->count )
                {
                    const struct ToriDraw_Event* ev = &eq->events[game->frame.event_index++];
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

            int slot_count = ToriDraw_SceneElementSlotCount(game->scene);
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
    ToriDraw_SceneFrameEnd(game->scene);

    game->frame.phase = RS_FRAME_PHASE_DONE;
}

int
game_runescape_spawn_projectile(
    struct GameRunescape* game,
    int model_id,
    int seq_id,
    int sx,
    int sz,
    int level,
    int sub_x,
    int sub_z,
    int vel_x,
    int vel_z,
    int yaw)
{
    if( !game || !game->world || !game->scene || !game->td )
        return -1;

    level = clamp_terrain_level(level);

    struct ToriDraw_ModelHandle cached = ToriAuxLibTD_Model(game->td, model_id);
    if( cached.kind != TORIDRAWMK_MODEL || !cached.u.model.model )
        return -1;
    if( !ToriDraw_ModelGetBoundsCylinder(cached) )
        return -1;

    struct ToriDraw_Model* model = ToriDraw_ModelCopy(cached.u.model.model);
    if( !model )
        return -1;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };

    if( ToriDraw_ModelIsLightable(model) )
    {
        ToriDraw_LightModelDefault(hnd, 0, 0);
        ToriDraw_ModelFreeNormals(model);
    }

    int element_id = ToriDraw_SceneElementAdd(game->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(game->scene, element_id, hnd);

    if( seq_id != -1 )
    {
        ToriDraw_SceneElementSetAnimationSeq(game->scene, element_id, seq_id);
        struct ToriDraw_Animation* anim = ToriAuxLibTD_SequenceAnimation(game->td, seq_id);
        ToriDraw_SceneElementSetAnimation(game->scene, element_id, anim, true);
    }

    int pos_x = sx * 128 + sub_x;
    int pos_z = sz * 128 + sub_z;
    int projectile_idx = world_projectile_spawn(
        game->world, element_id, level, pos_x, pos_z, vel_x, vel_z, yaw);
    if( projectile_idx < 0 )
        return -1;

    int pos_y = 0;
    if( game->world->heightmap )
        pos_y = heightmap_get_interpolated(game->world->heightmap, pos_x, pos_z, level);
    ToriDraw_SceneElementSetPosition(game->scene, element_id, pos_x, pos_y, pos_z, yaw);

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( element )
        element->dynamic = true;

    return projectile_idx;
}

struct ToriRunescape_Task_AddProjectile
{
    struct pt thread;
    struct GameRunescape* game;
    int model_id;
    int seq_id;
    int spawn_sx;
    int spawn_sz;
    int level;
    int dst_tiles_east;
};

struct ToriRunescape_Task_AddProjectile*
ToriRunescape_Task_AddProjectile_New(
    struct GameRunescape* game,
    int model_id,
    int seq_id,
    int spawn_sx,
    int spawn_sz,
    int level,
    int dst_tiles_east)
{
    struct ToriRunescape_Task_AddProjectile* task =
        calloc(1, sizeof(struct ToriRunescape_Task_AddProjectile));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->model_id = model_id;
    task->seq_id = seq_id;
    task->spawn_sx = spawn_sx;
    task->spawn_sz = spawn_sz;
    task->level = level;
    task->dst_tiles_east = dst_tiles_east;
    return task;
}

void
ToriRunescape_Task_AddProjectile_Free(struct ToriRunescape_Task_AddProjectile* task)
{
    free(task);
}

int
ToriRunescape_Task_AddProjectile_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct ToriRunescape_Task_AddProjectile* task = (struct ToriRunescape_Task_AddProjectile*)task_state;
    struct ToriAuxLibTD* td = NULL;
    struct RSCacheDat2A_Model* model;
    int decoded_model_id;
    int idx;

    PT_BEGIN(&task->thread);

    td = task->game ? task->game->td : NULL;
    if( !task->game || !td || !task->game->world )
    {
        ToriRunescape_Task_AddProjectile_Free(task);
        return PT_EXITED;
    }

    if( !ToriAuxLibTD_ModelReady(td, task->model_id) )
    {
        dat1io_model_fetch(ctx, task->model_id);
        PT_YIELD(&task->thread);

        td = task->game->td;
        decoded_model_id = -1;
        model = dat1io_model_decode(ctx, &decoded_model_id);
        if( !model )
        {
            fprintf(
                stderr,
                "ToriRunescape_Task_AddProjectile: failed to decode model %d\n",
                task->model_id);
            ToriRunescape_Task_AddProjectile_Free(task);
            return PT_EXITED;
        }

        dat1_buildcache_model_add(dat1(ToriAuxLibTD_C(td)), task->model_id, model);
        ToriAuxLibTD_SubmitModelFromDat1(td, task->model_id);
        ToriAuxLibTD_Model(td, task->model_id);
    }

    (void)ToriAuxLibTD_SequenceAnimation(td, task->seq_id);

    idx = game_runescape_spawn_projectile(
        task->game,
        task->model_id,
        task->seq_id,
        task->spawn_sx,
        task->spawn_sz,
        task->level,
        64,
        64,
        RUNESCAPE_PROJECTILE_VEL_EAST,
        0,
        0);
    if( idx >= 0 )
    {
        int const spawn_pos_x = task->spawn_sx * 128 + 64;
        int const spawn_pos_z = task->spawn_sz * 128 + 64;
        world_projectile_set_destination(
            task->game->world,
            idx,
            spawn_pos_x + task->dst_tiles_east * 128,
            spawn_pos_z);
    }

    ToriRunescape_Task_AddProjectile_Free(task);
    }
    return PT_ENDED;
}
