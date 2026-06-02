#include "model_viewer.h"

#include "../ui/ui_loader.h"
#include "../ui/ui_scene_events.h"
#include "../world/world_scene_events.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_sprite.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
game_modelviewer_translate_world_scene_event(
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
game_modelviewer_translate_uiscene_event(
    const struct UISceneEvent* ev,
    struct UIScene* scene,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));

    switch( ev->kind )
    {
    case UISCENE_EVENT_ELEMENT_ACQUIRED:
    {
        struct UISceneElement* el = ui_scene_element_at(scene, ev->element_id);
        if( !el || !el->toridraw_sprites || el->toridraw_sprites_count <= 0 )
            return false;
        command->kind = TORIRSRC_SPRITE_LOAD;
        command->u.sprite_load.element_id = ev->element_id;
        command->u.sprite_load.sprites = el->toridraw_sprites;
        command->u.sprite_load.count = el->toridraw_sprites_count;
        return true;
    }
    default:
        return false;
    }
}

static bool
game_modelviewer_translate_toridraw_event(
    const struct ToriDraw_Event* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;

    memset(command, 0, sizeof(*command));
    command->u.tex_load.texture_id = ev->texture_id;

    switch( ev->kind )
    {
    case TORIDRAW_EVENT_TEX_LOAD:
        command->kind = TORIRSRC_TEX_LOAD;
        command->u.tex_load.texture = ev->texture;
        return true;
    case TORIDRAW_EVENT_TEX_UNLOAD:
        command->kind = TORIRSRC_TEX_UNLOAD;
        command->u.tex_load.texture = NULL;
        return true;
    default:
        return false;
    }
}

static void
game_modelviewer_update_world_viewport(struct GameModelViewer* mv)
{
    int vx = 0;
    int vy = 0;
    int vw = mv->view_port ? mv->view_port->width : 800;
    int vh = mv->view_port ? mv->view_port->height : 600;
    int stride = mv->view_port ? mv->view_port->stride : vw;

    if( mv->tree )
    {
        for( uint32_t i = 0; i < mv->tree->component_count; i++ )
        {
            struct StaticUIComponent* c = &mv->tree->components[i];
            if( c->type != UIELEM_BUILTIN_WORLD )
                continue;
            vx = c->position.x;
            vy = c->position.y;
            vw = c->position.width > 0 ? c->position.width : vw;
            vh = c->position.height > 0 ? c->position.height : vh;
            break;
        }
    }

    mv->world_view_port.width = vw;
    mv->world_view_port.height = vh;
    mv->world_view_port.stride = stride;
    mv->world_view_port.x_center = vx + vw / 2;
    mv->world_view_port.y_center = vy + vh / 2;
}

static bool
game_modelviewer_emit_sprite_for_node(
    struct GameModelViewer* mv,
    int32_t node_index,
    struct LibToriRS_RenderCommand* command)
{
    if( !mv->tree || node_index < 0 || (uint32_t)node_index >= mv->tree->component_count )
        return false;

    struct StaticUIComponent* c = &mv->tree->components[node_index];
    if( c->type != UIELEM_BUILTIN_SPRITE )
        return false;
    if( c->u.sprite.scene_id < 0 )
        return false;

    struct UISceneElement* el = ui_scene_element_at(mv->scene, c->u.sprite.scene_id);
    if( !el || !el->toridraw_sprites )
        return false;

    int ai = c->u.sprite.atlas_index;
    if( ai < 0 || ai >= el->toridraw_sprites_count || !el->toridraw_sprites[ai] )
        return false;

    struct ToriDraw_Sprite* sp = el->toridraw_sprites[ai];
    int w = c->position.width > 0 ? c->position.width : sp->width;
    int h = c->position.height > 0 ? c->position.height : sp->height;

    command->kind = TORIRSRC_SPRITE;
    command->u.sprite.element_id = c->u.sprite.scene_id;
    command->u.sprite.atlas_index = ai;
    command->u.sprite.x = c->position.x;
    command->u.sprite.y = c->position.y;
    command->u.sprite.w = w;
    command->u.sprite.h = h;
    return true;
}

static void
game_modelviewer_advance_uitree(struct GameModelViewer* mv)
{
    if( !mv->tree )
        return;

    int32_t cur = mv->frame.uitree_current;
    if( cur < 0 )
        return;

    struct StaticUIComponent* c = &mv->tree->components[cur];
    if( c->first_child >= 0 )
    {
        if( mv->frame.uitree_stack_top + 1 < 64 )
        {
            mv->frame.uitree_stack[++mv->frame.uitree_stack_top] = cur;
            mv->frame.uitree_current = c->first_child;
            return;
        }
    }

    for( ;; )
    {
        if( c->next_sibling >= 0 )
        {
            mv->frame.uitree_current = c->next_sibling;
            return;
        }
        if( mv->frame.uitree_stack_top < 0 )
        {
            mv->frame.uitree_current = -1;
            return;
        }
        int32_t parent = mv->frame.uitree_stack[mv->frame.uitree_stack_top--];
        c = &mv->tree->components[parent];
        if( c->next_sibling >= 0 )
        {
            mv->frame.uitree_current = c->next_sibling;
            return;
        }
    }
}

struct GameModelViewer*
game_modelviewer_new(struct LibToriRS_ScriptQueue* script_queue)
{
    struct GameModelViewer* mv = calloc(1, sizeof(struct GameModelViewer));
    if( !mv )
        return NULL;

    mv->script_queue = script_queue;

    struct ToriDraw_Position* camera_position = malloc(sizeof(struct ToriDraw_Position));
    if( !camera_position )
    {
        game_modelviewer_free(mv);
        return NULL;
    }
    memset(camera_position, 0, sizeof(struct ToriDraw_Position));
    camera_position->x = 0;
    camera_position->y = 0;
    camera_position->z = -300;
    mv->camera_position = camera_position;

    struct ToriDraw_Camera* camera = malloc(sizeof(struct ToriDraw_Camera));
    if( !camera )
    {
        game_modelviewer_free(mv);
        return NULL;
    }
    memset(camera, 0, sizeof(struct ToriDraw_Camera));
    camera->fov_rpi2048 = 512;
    camera->near_plane_z = 50;
    camera->pitch = 148;
    mv->camera = camera;

    struct ToriDraw_ViewPort* view_port = malloc(sizeof(struct ToriDraw_ViewPort));
    if( !view_port )
    {
        game_modelviewer_free(mv);
        return NULL;
    }
    memset(view_port, 0, sizeof(struct ToriDraw_ViewPort));
    view_port->width = 765;
    view_port->height = 503;
    view_port->stride = 765;
    view_port->x_center = 382;
    view_port->y_center = 251;
    mv->view_port = view_port;

    toridraw_init();
    mv->context = toridraw_context_new();
    if( !mv->context )
    {
        game_modelviewer_free(mv);
        return NULL;
    }

    mv->world_scene = world_scene_new();
    if( !mv->world_scene )
    {
        game_modelviewer_free(mv);
        return NULL;
    }

    mv->tree = uitree_new(256);
    mv->scene = ui_scene_new(256);
    mv->loader_state = ui_loader_state_new();

    if( !mv->tree || !mv->scene || !mv->loader_state )
    {
        game_modelviewer_free(mv);
        return NULL;
    }

    mv->current_element_id = WORLD_SCENE_INVALID_ELEMENT_ID;
    game_modelviewer_update_world_viewport(mv);

    return mv;
}

static void
game_modelviewer_free_textures(struct GameModelViewer* mv)
{
    if( !mv || !mv->context )
        return;

    for( int i = 0; i < 256; i++ )
    {
        struct ToriDraw_Texture* texture = mv->context->texture_map.textures[i];
        if( !texture )
            continue;
        toridraw_texture_free(texture);
        mv->context->texture_map.textures[i] = NULL;
    }
    mv->context->texture_map.count = 0;
}

void
game_modelviewer_free(struct GameModelViewer* mv)
{
    if( !mv )
        return;
    game_modelviewer_free_textures(mv);
    if( mv->loader_state )
        ui_loader_state_free(mv->loader_state);
    if( mv->scene )
        ui_scene_free(mv->scene);
    if( mv->tree )
        uitree_free(mv->tree);
    if( mv->world_scene )
        world_scene_free(mv->world_scene);
    if( mv->context )
        toridraw_context_free(mv->context);
    free(mv->camera_position);
    free(mv->camera);
    free(mv->view_port);
    free(mv);
}

void
game_modelviewer_next(
    struct GameModelViewer* mv,
    int step)
{
    if( !mv )
        return;

    int model_id = mv->current_model_id + step;
    if( model_id < 0 )
        model_id = 0;

    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(mv->script_queue, "model_viewer/render_model.lua");

    LibToriRS_ScriptPushArg_Int(script, model_id);
    script->is_inline = false;

    mv->current_model_id = model_id;
}

void
game_modelviewer_set_model(
    struct GameModelViewer* mv,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    int element_id;

    (void)model_id;

    if( !mv || !mv->world_scene )
        return;

    if( mv->current_element_id != WORLD_SCENE_INVALID_ELEMENT_ID )
        world_scene_remove_element(mv->world_scene, mv->current_element_id);

    element_id = world_scene_add_element(mv->world_scene);
    assert(element_id != WORLD_SCENE_INVALID_ELEMENT_ID);

    world_scene_element_set_model(mv->world_scene, element_id, model);
    mv->current_element_id = element_id;
    mv->model = model;
}

void
game_modelviewer_process_input(
    struct GameModelViewer* mv,
    struct LibToriRS_Input* input)
{
    (void)mv;
    (void)input;
}

void
game_modelviewer_move_forward(
    struct GameModelViewer* mv,
    int amount)
{
    int direction_x_ish16 = toridraw_sin(mv->camera->yaw);
    int direction_z_ish16 = toridraw_cos(mv->camera->yaw);

    int delta_x = (direction_x_ish16 * amount) >> 16;
    int delta_z = (direction_z_ish16 * amount) >> 16;
    mv->camera_position->x -= delta_x;
    mv->camera_position->z += delta_z;
}

void
game_modelviewer_move_backward(
    struct GameModelViewer* mv,
    int amount)
{
    game_modelviewer_move_forward(mv, -amount);
}

void
game_modelviewer_move_left(
    struct GameModelViewer* mv,
    int amount)
{
    int direction_x_ish16 = toridraw_cos(mv->camera->yaw);
    int direction_z_ish16 = toridraw_sin(mv->camera->yaw);
    mv->camera_position->x += (direction_x_ish16 * amount) >> 16;
    mv->camera_position->z += (direction_z_ish16 * amount) >> 16;
}

void
game_modelviewer_move_right(
    struct GameModelViewer* mv,
    int amount)
{
    game_modelviewer_move_left(mv, -amount);
}

void
game_modelviewer_move_up(
    struct GameModelViewer* mv,
    int amount)
{
    mv->camera_position->y -= amount;
}

void
game_modelviewer_move_down(
    struct GameModelViewer* mv,
    int amount)
{
    mv->camera_position->y += amount;
}

void
game_modelviewer_rotate_left(
    struct GameModelViewer* mv,
    int amount)
{
    mv->camera->yaw = toridraw_add_angle(mv->camera->yaw, amount);
}

void
game_modelviewer_rotate_right(
    struct GameModelViewer* mv,
    int amount)
{
    game_modelviewer_rotate_left(mv, -amount);
}

void
game_modelviewer_rotate_up(
    struct GameModelViewer* mv,
    int amount)
{
    mv->camera->pitch = toridraw_add_angle(mv->camera->pitch, amount);
}

void
game_modelviewer_rotate_down(
    struct GameModelViewer* mv,
    int amount)
{
    game_modelviewer_rotate_up(mv, -amount);
}

void
game_modelviewer_frame_begin(struct GameModelViewer* mv)
{
    if( !mv )
        return;

    mv->frame.phase = MV_FRAME_PHASE_SCENE_EVENTS;
    mv->frame.event_index = 0;
    mv->frame.model_index = 0;
    mv->frame.uitree_stack_top = -1;
    mv->frame.world_emitted = false;

    if( mv->tree )
    {
        uitree_mark_all_dirty(mv->tree);
        mv->frame.uitree_current = mv->tree->root_index;
    }
    else
        mv->frame.uitree_current = -1;

    game_modelviewer_update_world_viewport(mv);
}

bool
game_modelviewer_frame_next_command(
    struct GameModelViewer* mv,
    struct LibToriRS_RenderCommand* command)
{
    if( !mv || !command )
        return false;

    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        switch( mv->frame.phase )
        {
        case MV_FRAME_PHASE_SCENE_EVENTS:
        {
            struct WorldScene_EventQueue* eq = NULL;
            if( mv->world_scene )
                eq = world_scene_get_event_queue(mv->world_scene);

            if( eq )
            {
                while( mv->frame.event_index < eq->count )
                {
                    const struct WorldScene_Event* ev = &eq->events[mv->frame.event_index++];
                    if( game_modelviewer_translate_world_scene_event(ev, command) )
                        return true;
                }
            }

            mv->frame.phase = MV_FRAME_PHASE_UISCENE_EVENTS;
            mv->frame.event_index = 0;
            continue;
        }
        case MV_FRAME_PHASE_UISCENE_EVENTS:
        {
            struct UISceneEventQueue* eq = ui_scene_get_event_queue(mv->scene);
            if( eq )
            {
                while( mv->frame.event_index < eq->count )
                {
                    const struct UISceneEvent* ev = &eq->events[mv->frame.event_index++];
                    if( game_modelviewer_translate_uiscene_event(ev, mv->scene, command) )
                        return true;
                }
            }

            mv->frame.phase = MV_FRAME_PHASE_TEXTURE_EVENTS;
            mv->frame.event_index = 0;
            continue;
        }
        case MV_FRAME_PHASE_TEXTURE_EVENTS:
        {
            if( mv->context )
            {
                struct ToriDraw_EventQueue* eq = &mv->context->events;
                while( mv->frame.event_index < eq->count )
                {
                    const struct ToriDraw_Event* ev = &eq->events[mv->frame.event_index++];
                    if( game_modelviewer_translate_toridraw_event(ev, command) )
                        return true;
                }
                toridraw_eventqueue_clear(eq);
            }

            mv->frame.phase = MV_FRAME_PHASE_BEGIN_3D;
            continue;
        }
        case MV_FRAME_PHASE_BEGIN_3D:
        {
            if( mv->frame.world_emitted )
            {
                mv->frame.phase = MV_FRAME_PHASE_END_3D;
                continue;
            }

            command->kind = TORIRSRC_BEGIN_3D;
            command->u.begin_3d.view_port = mv->world_view_port;
            if( mv->camera )
                command->u.begin_3d.camera = *mv->camera;
            if( mv->camera_position )
            {
                command->u.begin_3d.camera_position.x = -mv->camera_position->x;
                command->u.begin_3d.camera_position.y = -mv->camera_position->y;
                command->u.begin_3d.camera_position.z = -mv->camera_position->z;
            }
            mv->frame.world_emitted = true;
            mv->frame.phase = MV_FRAME_PHASE_MODELS;
            return true;
        }
        case MV_FRAME_PHASE_MODELS:
        {
            if( mv->frame.model_index > 0 )
            {
                mv->frame.phase = MV_FRAME_PHASE_END_3D;
                continue;
            }

            if( mv->model.kind != TORIDRAWMK_MODEL )
            {
                mv->frame.phase = MV_FRAME_PHASE_END_3D;
                continue;
            }

            if( mv->context && mv->camera && mv->camera_position )
            {
                struct ToriDraw_Position camera_pos = { 0 };
                camera_pos.x = -mv->camera_position->x;
                camera_pos.y = -mv->camera_position->y;
                camera_pos.z = -mv->camera_position->z;

                const int cull = toridraw_render_model1_project(
                    mv->model, mv->context, &camera_pos, &mv->world_view_port, mv->camera);
                if( cull != TORIDRAW_CULL_VISIBLE )
                {
                    mv->frame.model_index++;
                    mv->frame.phase = MV_FRAME_PHASE_END_3D;
                    continue;
                }

                if( toridraw_render_model2_sort_faces(mv->model, mv->context) <= 0 )
                {
                    mv->frame.model_index++;
                    mv->frame.phase = MV_FRAME_PHASE_END_3D;
                    continue;
                }
            }

            command->kind = TORIRSRC_DRAW_MODEL;
            command->u.model.model = mv->model;
            command->u.model.element_id = mv->current_element_id;
            memset(&command->u.model.position, 0, sizeof(command->u.model.position));
            mv->frame.model_index++;
            return true;
        }
        case MV_FRAME_PHASE_END_3D:
            command->kind = TORIRSRC_END_3D;
            mv->frame.phase = MV_FRAME_PHASE_BEGIN_2D;
            return true;
        case MV_FRAME_PHASE_BEGIN_2D:
            command->kind = TORIRSRC_BEGIN_2D;
            mv->frame.phase = MV_FRAME_PHASE_UITREE;
            return true;
        case MV_FRAME_PHASE_UITREE:
        {
            while( mv->frame.uitree_current >= 0 )
            {
                int32_t cur = mv->frame.uitree_current;
                if( game_modelviewer_emit_sprite_for_node(mv, cur, command) )
                {
                    game_modelviewer_advance_uitree(mv);
                    return true;
                }
                game_modelviewer_advance_uitree(mv);
            }
            mv->frame.phase = MV_FRAME_PHASE_END_2D;
            continue;
        }
        case MV_FRAME_PHASE_END_2D:
            command->kind = TORIRSRC_END_2D;
            mv->frame.phase = MV_FRAME_PHASE_DONE;
            return true;
        case MV_FRAME_PHASE_DONE:
        default:
            return false;
        }
    }
}

void
game_modelviewer_frame_end(struct GameModelViewer* mv)
{
    if( !mv )
        return;

    if( mv->world_scene )
    {
        struct WorldScene_EventQueue* eq = world_scene_get_event_queue(mv->world_scene);
        if( eq )
            worldscene_eventqueue_clear(eq);
    }

    struct UISceneEventQueue* ueq = ui_scene_get_event_queue(mv->scene);
    if( ueq )
        uiscene_eventqueue_clear(ueq);

    if( mv->context )
        toridraw_eventqueue_clear(&mv->context->events);

    mv->frame.phase = MV_FRAME_PHASE_DONE;
}
