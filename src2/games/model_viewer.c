#include "model_viewer.h"

#include "../world/world_scene_events.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
game_modelviewer_translate_scene_event(
    const struct WorldScene_Event* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));

    switch( ev->kind )
    {
    case WSE_MODEL_LOAD:
        command->kind = TORIRSRC_MODEL_LOAD;
        command->u.model_load.element_id = ev->element_id;
        command->u.model_load.model = ev->model;
        return true;
    case WSE_MODEL_UNLOAD:
        command->kind = TORIRSRC_MODEL_UNLOAD;
        command->u.model_load.element_id = ev->element_id;
        return true;
    case WSE_BATCH_BEGIN:
        command->kind = TORIRSRC_BATCH3D_BEGIN;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case WSE_BATCH_MODEL_ADD:
        command->kind = TORIRSRC_BATCH3D_MODEL_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.model = ev->model;
        return true;
    case WSE_BATCH_END:
        command->kind = TORIRSRC_BATCH3D_END;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case WSE_BATCH_CLEAR:
        command->kind = TORIRSRC_BATCH3D_CLEAR;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    default:
        return false;
    }
}

static bool
game_modelviewer_translate_texture_event(
    const struct ToriDraw_TextureEvent* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));
    command->u.tex_load.texture_id = ev->texture_id;

    switch( ev->kind )
    {
    case TORIDRAW_TEX_EVENT_LOAD:
        command->kind = TORIRSRC_TEX_LOAD;
        command->u.tex_load.texture = ev->texture;
        return true;
    case TORIDRAW_TEX_EVENT_UNLOAD:
        command->kind = TORIRSRC_TEX_UNLOAD;
        command->u.tex_load.texture = NULL;
        return true;
    default:
        return false;
    }
}

struct GameModelViewer*
game_modelviewer_new(struct LibToriRS_ScriptQueue* script_queue)
{
    struct GameModelViewer* game_model_viewer = malloc(sizeof(struct GameModelViewer));
    if( !game_model_viewer )
        return NULL;
    memset(game_model_viewer, 0, sizeof(struct GameModelViewer));

    game_model_viewer->script_queue = script_queue;

    struct ToriDraw_Position* camera_position = malloc(sizeof(struct ToriDraw_Position));
    memset(camera_position, 0, sizeof(struct ToriDraw_Position));
    camera_position->x = 0;
    camera_position->y = 0;
    camera_position->z = -300;
    camera_position->pitch = 0;
    camera_position->yaw = 0;
    camera_position->roll = 0;

    game_model_viewer->camera_position = camera_position;

    struct ToriDraw_Camera* camera = malloc(sizeof(struct ToriDraw_Camera));
    memset(camera, 0, sizeof(struct ToriDraw_Camera));
    camera->fov_rpi2048 = 512;
    camera->near_plane_z = 50;
    camera->pitch = 148;
    camera->yaw = 0;
    camera->roll = 0;
    game_model_viewer->camera = camera;

    struct ToriDraw_ViewPort* view_port = malloc(sizeof(struct ToriDraw_ViewPort));
    memset(view_port, 0, sizeof(struct ToriDraw_ViewPort));
    view_port->width = 800;
    view_port->height = 600;
    view_port->stride = 800;
    view_port->x_center = 400;
    view_port->y_center = 300;
    game_model_viewer->view_port = view_port;

    toridraw_init();
    game_model_viewer->context = toridraw_context_new();
    if( !game_model_viewer->context )
    {
        game_modelviewer_free(game_model_viewer);
        return NULL;
    }

    game_model_viewer->world_scene = world_scene_new();
    if( !game_model_viewer->world_scene )
    {
        game_modelviewer_free(game_model_viewer);
        return NULL;
    }

    game_model_viewer->current_element_id = WORLD_SCENE_INVALID_ELEMENT_ID;

    return game_model_viewer;
}

static void
game_modelviewer_free_textures(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer || !game_model_viewer->context )
        return;

    for( int i = 0; i < 256; i++ )
    {
        struct ToriDraw_Texture* texture = game_model_viewer->context->texture_map.textures[i];
        if( !texture )
            continue;
        toridraw_texture_free(texture);
        game_model_viewer->context->texture_map.textures[i] = NULL;
    }
    game_model_viewer->context->texture_map.count = 0;
}

void
game_modelviewer_free(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer )
        return;
    game_modelviewer_free_textures(game_model_viewer);
    if( game_model_viewer->world_scene )
        world_scene_free(game_model_viewer->world_scene);
    if( game_model_viewer->context )
        toridraw_context_free(game_model_viewer->context);
    free(game_model_viewer->camera_position);
    free(game_model_viewer->camera);
    free(game_model_viewer->view_port);
    free(game_model_viewer);
}

void
game_modelviewer_next(
    struct GameModelViewer* game_model_viewer,
    int step)
{
    if( !game_model_viewer )
        return;

    int model_id = game_model_viewer->current_model_id + step;
    if( model_id < 0 )
        model_id = 0;

    struct LibToriRS_Script* script = LibToriRS_ScriptQueueEmplace(
        game_model_viewer->script_queue, "model_viewer/render_model.lua");

    LibToriRS_ScriptPushArg_Int(script, model_id);
    script->is_inline = false;

    game_model_viewer->current_model_id = model_id;
}

void
game_modelviewer_set_model(
    struct GameModelViewer* game_model_viewer,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    int element_id;

    (void)model_id;

    if( !game_model_viewer || !game_model_viewer->world_scene )
        return;

    if( game_model_viewer->current_element_id != WORLD_SCENE_INVALID_ELEMENT_ID )
    {
        world_scene_remove_element(
            game_model_viewer->world_scene, game_model_viewer->current_element_id);
    }

    element_id = world_scene_add_element(game_model_viewer->world_scene);
    assert(element_id != WORLD_SCENE_INVALID_ELEMENT_ID);

    world_scene_element_set_model(game_model_viewer->world_scene, element_id, model);
    game_model_viewer->current_element_id = element_id;
    game_model_viewer->model = model;
}

void
game_modelviewer_process_input(
    struct GameModelViewer* game_model_viewer,
    struct LibToriRS_Input* input)
{
    (void)game_model_viewer;
    (void)input;
}

void
game_modelviewer_move_forward(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    // Relative to the direction the camera is facing
    int direction_x_ish16 = toridraw_sin(game_model_viewer->camera->yaw);
    int direction_z_ish16 = toridraw_cos(game_model_viewer->camera->yaw);

    int delta_x = (direction_x_ish16 * amount) >> 16;
    int delta_z = (direction_z_ish16 * amount) >> 16;
    game_model_viewer->camera_position->x -= delta_x;
    game_model_viewer->camera_position->z += delta_z;
}

void
game_modelviewer_move_backward(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_move_forward(game_model_viewer, -amount);
}

void
game_modelviewer_move_left(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    // Relative to the direction the camera is facing
    int direction_x_ish16 = toridraw_cos(game_model_viewer->camera->yaw);
    int direction_z_ish16 = toridraw_sin(game_model_viewer->camera->yaw);
    game_model_viewer->camera_position->x += (direction_x_ish16 * amount) >> 16;
    game_model_viewer->camera_position->z += (direction_z_ish16 * amount) >> 16;
}

void
game_modelviewer_move_right(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_move_left(game_model_viewer, -amount);
}

void
game_modelviewer_move_up(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera_position->y -= amount;
}

void
game_modelviewer_move_down(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera_position->y += amount;
}

void
game_modelviewer_rotate_left(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera->yaw = toridraw_add_angle(game_model_viewer->camera->yaw, amount);
}

void
game_modelviewer_rotate_right(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_rotate_left(game_model_viewer, -amount);
}

void
game_modelviewer_rotate_up(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera->pitch = toridraw_add_angle(game_model_viewer->camera->pitch, amount);
}

void
game_modelviewer_rotate_down(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_rotate_up(game_model_viewer, -amount);
}

void
game_modelviewer_frame_begin(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer )
        return;

    game_model_viewer->frame.phase = MV_FRAME_PHASE_SCENE_EVENTS;
    game_model_viewer->frame.event_index = 0;
    game_model_viewer->frame.model_index = 0;
}

bool
game_modelviewer_frame_next_command(
    struct GameModelViewer* game_model_viewer,
    struct LibToriRS_RenderCommand* command)
{
    if( !game_model_viewer || !command )
        return false;

    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        switch( game_model_viewer->frame.phase )
        {
        case MV_FRAME_PHASE_SCENE_EVENTS:
        {
            struct WorldScene_EventQueue* eq = NULL;
            if( game_model_viewer->world_scene )
                eq = world_scene_get_event_queue(game_model_viewer->world_scene);

            if( eq )
            {
                while( game_model_viewer->frame.event_index < eq->count )
                {
                    const struct WorldScene_Event* ev =
                        &eq->events[game_model_viewer->frame.event_index++];
                    if( game_modelviewer_translate_scene_event(ev, command) )
                        return true;
                }
            }

            game_model_viewer->frame.phase = MV_FRAME_PHASE_TEXTURE_EVENTS;
            game_model_viewer->frame.event_index = 0;
            continue;
        }
        case MV_FRAME_PHASE_TEXTURE_EVENTS:
        {
            if( game_model_viewer->context )
            {
                struct ToriDraw_TextureEventQueue* teq =
                    &game_model_viewer->context->texture_events;
                while( game_model_viewer->frame.event_index < teq->count )
                {
                    const struct ToriDraw_TextureEvent* ev =
                        &teq->events[game_model_viewer->frame.event_index++];
                    if( game_modelviewer_translate_texture_event(ev, command) )
                        return true;
                }
                toridraw_texture_eventqueue_clear(teq);
            }

            game_model_viewer->frame.phase = MV_FRAME_PHASE_BEGIN_3D;
            continue;
        }
        case MV_FRAME_PHASE_BEGIN_3D:
        {
            command->kind = TORIRSRC_BEGIN_3D;
            if( game_model_viewer->view_port )
                command->u.begin_3d.view_port = *game_model_viewer->view_port;
            if( game_model_viewer->camera )
                command->u.begin_3d.camera = *game_model_viewer->camera;
            if( game_model_viewer->camera_position )
            {
                command->u.begin_3d.camera_position.x = -game_model_viewer->camera_position->x;
                command->u.begin_3d.camera_position.y = -game_model_viewer->camera_position->y;
                command->u.begin_3d.camera_position.z = -game_model_viewer->camera_position->z;
            }
            game_model_viewer->frame.phase = MV_FRAME_PHASE_MODELS;
            return true;
        }
        case MV_FRAME_PHASE_MODELS:
        {
            if( game_model_viewer->frame.model_index > 0 )
            {
                game_model_viewer->frame.phase = MV_FRAME_PHASE_END_3D;
                continue;
            }

            if( game_model_viewer->model.kind != TORIDRAWMK_MODEL )
            {
                game_model_viewer->frame.phase = MV_FRAME_PHASE_END_3D;
                continue;
            }

            if( game_model_viewer->context && game_model_viewer->view_port &&
                game_model_viewer->camera && game_model_viewer->camera_position )
            {
                struct ToriDraw_Position camera_pos = { 0 };
                camera_pos.x = -game_model_viewer->camera_position->x;
                camera_pos.y = -game_model_viewer->camera_position->y;
                camera_pos.z = -game_model_viewer->camera_position->z;

                const int cull = toridraw_render_model1_project(
                    game_model_viewer->model,
                    game_model_viewer->context,
                    &camera_pos,
                    game_model_viewer->view_port,
                    game_model_viewer->camera);
                if( cull != TORIDRAW_CULL_VISIBLE )
                {
                    game_model_viewer->frame.model_index++;
                    game_model_viewer->frame.phase = MV_FRAME_PHASE_END_3D;
                    continue;
                }

                if( toridraw_render_model2_sort_faces(
                        game_model_viewer->model, game_model_viewer->context) <= 0 )
                {
                    game_model_viewer->frame.model_index++;
                    game_model_viewer->frame.phase = MV_FRAME_PHASE_END_3D;
                    continue;
                }
            }

            command->kind = TORIRSRC_MODEL;
            command->u.model.model = game_model_viewer->model;
            command->u.model.element_id = game_model_viewer->current_element_id;
            memset(&command->u.model.position, 0, sizeof(command->u.model.position));
            game_model_viewer->frame.model_index++;
            return true;
        }
        case MV_FRAME_PHASE_END_3D:
            command->kind = TORIRSRC_END_3D;
            game_model_viewer->frame.phase = MV_FRAME_PHASE_DONE;
            return true;
        case MV_FRAME_PHASE_DONE:
        default:
            return false;
        }
    }
}

void
game_modelviewer_frame_end(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer )
        return;

    if( game_model_viewer->world_scene )
    {
        struct WorldScene_EventQueue* eq =
            world_scene_get_event_queue(game_model_viewer->world_scene);
        if( eq )
            worldscene_eventqueue_clear(eq);
    }

    if( game_model_viewer->context )
        toridraw_texture_eventqueue_clear(&game_model_viewer->context->texture_events);

    game_model_viewer->frame.phase = MV_FRAME_PHASE_DONE;
}