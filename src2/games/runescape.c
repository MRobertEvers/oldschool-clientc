#include "runescape.h"

#include "../buildcache/dat1_buildcache.h"
#include "../toriauxlib/c/dat1io.h"
#include "../toriauxlib/c/toriauxlibc.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "../toriauxlib/vm/toriauxlibvm.h"
#include "../world/heightmap.h"
#include "../world/minimap.h"
#include "../world/world_builder.h"
#include "3rd/minipt.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNESCAPE_CAMERA_MOVEMENT_SPEED 70

enum RsPhaseResult
{
    RS_PHASE_YIELD,
    RS_PHASE_ADVANCE,
};

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
    case TORIDRAW_EVENT_SPRITE_LOAD:
        command->kind = TORIRSRC_SPRITE_LOAD;
        command->u.sprite_load.element_id = ev->element_id;
        command->u.sprite_load.sprites = ev->sprites;
        command->u.sprite_load.count = ev->sprite_count;
        return true;
    case TORIDRAW_EVENT_SPRITE_UNLOAD:
        command->kind = TORIRSRC_SPRITE_UNLOAD;
        command->u.sprite_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_FONT_LOAD:
        command->kind = TORIRSRC_FONT_LOAD;
        command->u.font_load.font_id = ev->texture_id;
        command->u.font_load.font = ev->font;
        return true;
    case TORIDRAW_EVENT_FONT_UNLOAD:
        command->kind = TORIRSRC_FONT_UNLOAD;
        command->u.font_load.font_id = ev->texture_id;
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
    game->world_view_port.clip_left = 0;
    game->world_view_port.clip_top = 0;
    game->world_view_port.clip_right = vw;
    game->world_view_port.clip_bottom = vh;

    if( game->ui_tree )
    {
        for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
        {
            struct StaticUIComponent* world = &game->ui_tree->components[i];
            if( world->type != UIELEM_BUILTIN_WORLD )
                continue;

            int wx = world->position.x;
            int wy = world->position.y;
            int ww = world->position.width;
            int wh = world->position.height;
            if( ww <= 0 || wh <= 0 )
                break;

            game->world_view_port.width = ww;
            game->world_view_port.height = wh;
            game->world_view_port.x_center = wx + ww / 2;
            game->world_view_port.y_center = wy + wh / 2;
            game->world_view_port.clip_left = wx;
            game->world_view_port.clip_top = wy;
            game->world_view_port.clip_right = wx + ww;
            game->world_view_port.clip_bottom = wy + wh;
            break;
        }
    }
}

static bool
game_runescape_emit_draw_element(
    struct GameRunescape* game,
    int element_id,
    enum WorldPickType pick_type,
    int tile_x,
    int tile_z,
    int tile_level,
    struct LibToriRS_RenderCommand* command)
{
    assert(ToriDraw_SceneElementIsLive(game->scene, element_id));

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    assert(!(!element || element->model.kind != TORIDRAWMK_MODEL));
    assert(ToriDraw_ModelGetBoundsCylinder(element->model));

    struct ToriDraw_Position rel_pos = element->world_position;
    rel_pos.x -= game->camera_position->x;
    rel_pos.y -= game->camera_position->y;
    rel_pos.z -= game->camera_position->z;
    rel_pos.pitch = ToriDraw_NormalizeAngle(element->world_position.pitch);
    rel_pos.yaw = ToriDraw_NormalizeAngle(element->world_position.yaw);

    if( element->anim_seq_id != -1 && element->animation )
        ToriDraw_SceneElementApplyAnimation(game->scene, element_id, true, element->anim_frame);

    if( game->scene )
    {
        const int cull = ToriDraw_RenderModel1Project(
            element->model, game->scene, &rel_pos, &game->world_view_port, game->camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
            return false;

        if( game->mouse_in_viewport &&
            ToriDraw_ProjectedModelContainsPoint(
                game->scene, element->model, &game->world_view_port, game->mouse_x, game->mouse_y) )
        {
            world_pickset_add(&game->pickset, element_id, pick_type);
            if( pick_type == WORLD_PICK_TERRAIN )
            {
                game->last_tile_sx = tile_x;
                game->last_tile_sz = tile_z;
                game->last_tile_level = tile_level;
                game->last_tile_valid = true;
            }
        }

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

    game->ui_tree = uitree_new(64);
    assert(game->ui_tree && "game_runescape_new: failed to allocate ui tree");

    game->world_map_scene_id = -1;
    game->world_map_w = 0;
    game->world_map_h = 0;

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
    if( game->ui_tree )
        uitree_free(game->ui_tree);
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
game_runescape_set_vm(
    struct GameRunescape* game,
    struct ToriAuxLibVM* vm)
{
    if( !game )
        return;
    game->vm = vm;
}

static void
game_runescape_attach_world_map_to_ui_tree(struct GameRunescape* game)
{
    if( !game || game->world_map_scene_id < 0 || !game->ui_tree )
        return;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_MINIMAP )
        {
            game->ui_tree->components[i].u.minimap.scene_id = game->world_map_scene_id;
            break;
        }
    }
}

void
game_runescape_set_ui_tree(
    struct GameRunescape* game,
    struct UITree* ui_tree)
{
    if( !game )
        return;
    game->ui_tree = ui_tree;
    game_runescape_attach_world_map_to_ui_tree(game);
}

void
game_runescape_set_ui_tree_ready(
    struct GameRunescape* game,
    bool ready)
{
    if( !game )
        return;
    game->ui_tree_ready = ready;
    if( ready )
        game_runescape_attach_world_map_to_ui_tree(game);
}

void
game_runescape_rebuild_world_map(struct GameRunescape* game)
{
    if( !game || !game->world || !game->world->minimap || !game->scene )
        return;

    struct Minimap* mm = game->world->minimap;
    int pw = 0;
    int ph = 0;
    uint32_t* argb = minimap_bake_argb(mm, &pw, &ph);
    if( !argb )
        return;

    struct ToriDraw_Sprite* sp = ToriDraw_SpriteNewFromArgbOwned(argb, pw, ph);
    if( !sp )
    {
        free(argb);
        return;
    }

    struct ToriDraw_Sprite** sprites_array = (struct ToriDraw_Sprite**)malloc(sizeof(*sprites_array));
    if( !sprites_array )
    {
        ToriDraw_SpriteFree(sp);
        return;
    }
    sprites_array[0] = sp;

    ToriDraw_SceneSpriteAdd(game->scene, RUNESCAPE_WORLD_MAP_SCENE_ID, sprites_array, 1);
    game->world_map_scene_id = RUNESCAPE_WORLD_MAP_SCENE_ID;
    game->world_map_w = pw;
    game->world_map_h = ph;
    game_runescape_attach_world_map_to_ui_tree(game);
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

    game_runescape_rebuild_world_map(game);
}

static bool
game_runescape_point_in_component(
    struct StaticUIComponent const* c,
    int px,
    int py)
{
    if( !c || c->position.kind != UIPOS_XY )
        return false;
    int x = c->position.x;
    int y = c->position.y;
    int w = c->position.width;
    int h = c->position.height;
    if( w <= 0 || h <= 0 )
        return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool
game_runescape_ui_node_visible(
    struct GameRunescape* game,
    struct StaticUIComponent const* c,
    int32_t node_index)
{
    if( !c )
        return false;
    if( !c->behavior.hide )
        return true;
    return game->ui_hovered_node == node_index;
}

static int32_t
game_runescape_ui_hit_test_recursive(
    struct GameRunescape* game,
    struct UITree* tree,
    int32_t node_index,
    int px,
    int py)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    struct StaticUIComponent* c = &tree->components[node_index];

    int32_t hit = -1;
    if( game_runescape_point_in_component(c, px, py) )
        hit = node_index;

    for( int32_t child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
    {
        int32_t child_hit = game_runescape_ui_hit_test_recursive(game, tree, child, px, py);
        if( child_hit >= 0 )
            hit = child_hit;
    }

    return hit;
}

int32_t
game_runescape_ui_hit_test(
    struct GameRunescape* game,
    int px,
    int py)
{
    if( !game || !game->ui_tree || game->ui_tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = game->ui_tree->root_index; root >= 0;
         root = game->ui_tree->components[root].next_sibling )
    {
        int32_t root_hit = game_runescape_ui_hit_test_recursive(game, game->ui_tree, root, px, py);
        if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

static char const*
game_runescape_expand_ui_text(
    struct GameRunescape* game,
    struct StaticUIComponent* component)
{
    char const* src = component->u.rs_text.text;
    if( !src || !game || !game->vm )
        return src;

    char* dst = game->ui_text_scratch;
    size_t dst_cap = sizeof(game->ui_text_scratch);
    size_t di = 0;

    for( size_t i = 0; src[i] != '\0' && di + 1 < dst_cap; i++ )
    {
        if( src[i] == '%' && src[i + 1] >= '1' && src[i + 1] <= '5' )
        {
            int script_idx = src[i + 1] - '1';
            int val = ToriAuxLibVM_EvalScript(game->vm, component, script_idx);
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", val);
            if( n < 0 )
                n = 0;
            for( int j = 0; j < n && di + 1 < dst_cap; j++ )
                dst[di++] = num[j];
            i++;
            continue;
        }
        dst[di++] = src[i];
    }
    dst[di] = '\0';
    return dst;
}

static int
game_runescape_ui_rect_color(
    struct GameRunescape* game,
    struct StaticUIComponent* component)
{
    int color = component->u.rs_rect.color;
    bool hovered = game && game->ui_hovered_node >= 0 &&
                   &game->ui_tree->components[game->ui_hovered_node] == component;
    bool active = game && game->vm && component->behavior.script_comparator &&
                  ToriAuxLibVM_IsActive(game->vm, component);

    if( active )
        color = component->behavior.active_color ? component->behavior.active_color : color;
    if( hovered )
    {
        if( active && component->behavior.active_over_color != 0 )
            color = component->behavior.active_over_color;
        else if( !active && component->behavior.over_color != 0 )
            color = component->behavior.over_color;
    }
    return color;
}

void
game_runescape_process_input(
    struct GameRunescape* game,
    struct LibToriRS_Input* input)
{
    int const vw = game->view_port ? game->view_port->width : game->world_view_port.width;
    int const vh = game->view_port ? game->view_port->height : game->world_view_port.height;

    game->mouse_x = input->curr.mouse_x;
    game->mouse_y = input->curr.mouse_y;
    game->mouse_in_viewport =
        game->mouse_x >= 0 && game->mouse_x < vw && game->mouse_y >= 0 && game->mouse_y < vh;

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) && game->ui_tree && game->ui_tree_ready &&
        game->vm )
    {
        int32_t clicked = game_runescape_ui_hit_test(
            game, input->last_click_x[TORIRSM_LEFT], input->last_click_y[TORIRSM_LEFT]);
        if( clicked >= 0 )
        {
            struct StaticUIComponent* hovered = &game->ui_tree->components[clicked];
            if( hovered->behavior.button_type != 0 || hovered->behavior.client_code > 0 )
                ToriAuxLibVM_ApplyButtonClickOptimistic(game->vm, hovered);
        }
    }

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

    struct World_EntityPool* pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( !p || p->element_id < 0 || !p->launched )
            continue;

        ToriDraw_SceneElementSetPositionPitchYaw(
            game->scene, p->element_id, (int)p->x, (int)p->y, (int)p->z, p->pitch, p->yaw);
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

static void
game_runescape_ui_advance(
    struct GameRunescape* game,
    int32_t stepped_index)
{
    struct UITree* tree = game->ui_tree;
    struct StaticUIComponent* c = &tree->components[stepped_index];

    if( c->first_child >= 0 && game_runescape_ui_node_visible(game, c, stepped_index) )
    {
        if( game->frame.ui_stack_top + 1 < RUNESCAPE_UI_TRAVERSAL_STACK_MAX )
        {
            game->frame.ui_stack[++game->frame.ui_stack_top] =
                (struct GameRunescape_UITraversalFrame){ .parent_index = stepped_index };
            game->frame.ui_current = c->first_child;
            return;
        }
    }

    if( c->next_sibling >= 0 )
    {
        game->frame.ui_current = c->next_sibling;
        return;
    }

    while( game->frame.ui_stack_top >= 0 )
    {
        struct GameRunescape_UITraversalFrame frame =
            game->frame.ui_stack[game->frame.ui_stack_top--];
        struct StaticUIComponent* parent = &tree->components[frame.parent_index];
        if( parent->next_sibling >= 0 )
        {
            game->frame.ui_current = parent->next_sibling;
            return;
        }
    }
    game->frame.ui_current = -1;
}

static void
game_runescape_emit_sprite_command(
    struct LibToriRS_RenderCommand* command,
    int scene_id,
    int atlas_index,
    int x,
    int y,
    int w,
    int h)
{
    command->kind = TORIRSRC_SPRITE;
    command->u.sprite.element_id = scene_id;
    command->u.sprite.atlas_index = atlas_index;
    command->u.sprite.x = x;
    command->u.sprite.y = y;
    command->u.sprite.w = w;
    command->u.sprite.h = h;
    command->u.sprite.alpha = 255;
    command->u.sprite.rotated = 0;
    command->u.sprite.rotation = 0;
    command->u.sprite.dst_anchor_x = 0;
    command->u.sprite.dst_anchor_y = 0;
    command->u.sprite.src_anchor_x = 0;
    command->u.sprite.src_anchor_y = 0;
    command->u.sprite.scissor_x = 0;
    command->u.sprite.scissor_y = 0;
    command->u.sprite.scissor_w = 0;
    command->u.sprite.scissor_h = 0;
    command->u.sprite.mask_element_id = -1;
    command->u.sprite.mask_atlas_index = 0;
}

static bool
game_runescape_emit_ui_component(
    struct GameRunescape* game,
    struct StaticUIComponent* component,
    struct LibToriRS_RenderCommand* command)
{
    assert(!(!component || !command));

    switch( component->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_RS_GRAPHIC:
    {
        int scene_id = component->u.sprite.scene_id;
        int atlas_index = component->u.sprite.atlas_index;
        if( component->type == UIELEM_RS_GRAPHIC )
        {
            scene_id = component->u.rs_graphic.scene_id;
            atlas_index = component->u.rs_graphic.atlas_index;
            if( game->vm && component->behavior.scripts_count > 0 &&
                ToriAuxLibVM_IsActive(game->vm, component) )
            {
                if( component->u.rs_graphic.scene_id_active >= 0 )
                {
                    scene_id = component->u.rs_graphic.scene_id_active;
                    atlas_index = component->u.rs_graphic.atlas_index_active;
                }
            }
        }
        if( scene_id < 0 )
            return false;
        game_runescape_emit_sprite_command(
            command,
            scene_id,
            atlas_index,
            component->position.x,
            component->position.y,
            component->position.width,
            component->position.height);
        return true;
    }
    case UIELEM_BUILTIN_COMPASS:
    {
        int scene_id = component->u.sprite.scene_id;
        int atlas_index = component->u.sprite.atlas_index;
        if( scene_id < 0 )
            return false;
        game_runescape_emit_sprite_command(
            command,
            scene_id,
            atlas_index,
            component->position.x,
            component->position.y,
            component->position.width,
            component->position.height);
        command->u.sprite.rotated = 1;
        command->u.sprite.rotation =
            game->camera ? ToriDraw_NormalizeAngle(game->camera->yaw) : 0;
        command->u.sprite.dst_anchor_x = component->position.anchor_x;
        command->u.sprite.dst_anchor_y = component->position.anchor_y;
        {
            int sprite_count = 0;
            struct ToriDraw_Sprite** sprites =
                game->scene ? ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count) : NULL;
            struct ToriDraw_Sprite* sp =
                (sprites && atlas_index >= 0 && atlas_index < sprite_count) ? sprites[atlas_index]
                                                                            : NULL;
            int sw = sp ? (sp->crop_width > 0 ? sp->crop_width : sp->width)
                        : component->position.width;
            int sh = sp ? (sp->crop_height > 0 ? sp->crop_height : sp->height)
                        : component->position.height;
            command->u.sprite.src_anchor_x = sw >> 1;
            command->u.sprite.src_anchor_y = sh >> 1;
        }
        command->u.sprite.scissor_x = component->position.x;
        command->u.sprite.scissor_y = component->position.y;
        command->u.sprite.scissor_w = component->position.width;
        command->u.sprite.scissor_h = component->position.height;
        return true;
    }
    case UIELEM_BUILTIN_REDSTONE_TAB:
    {
        int scene_id = component->u.redstone_tab.scene_id;
        int atlas_index = component->u.redstone_tab.atlas_index;
        if( scene_id < 0 )
            return false;
        game_runescape_emit_sprite_command(
            command,
            scene_id,
            atlas_index,
            component->position.x,
            component->position.y,
            component->position.width,
            component->position.height);
        return true;
    }
    case UIELEM_BUILTIN_MINIMAP:
    {
        int scene_id = component->u.minimap.scene_id;
        if( scene_id < 0 )
            return false;
        game_runescape_emit_sprite_command(
            command,
            scene_id,
            0,
            component->position.x,
            component->position.y,
            component->position.width,
            component->position.height);
        command->u.sprite.rotated = 1;
        command->u.sprite.rotation =
            game->camera ? ToriDraw_NormalizeAngle(game->camera->yaw) : 0;
        command->u.sprite.dst_anchor_x = component->position.anchor_x;
        command->u.sprite.dst_anchor_y = component->position.anchor_y;
        if( game->camera_position && game->world_map_w > 0 && game->world_map_h > 0 )
        {
            int camera_tile_x = game->camera_position->x / 128;
            int camera_tile_z = game->camera_position->z / 128;
            command->u.sprite.src_anchor_x = camera_tile_x * 4;
            command->u.sprite.src_anchor_y = game->world_map_h - camera_tile_z * 4;
        }
        command->u.sprite.scissor_x = component->position.x;
        command->u.sprite.scissor_y = component->position.y;
        command->u.sprite.scissor_w = component->position.width;
        command->u.sprite.scissor_h = component->position.height;
        return true;
    }
    case UIELEM_RS_TEXT:
        if( !component->u.rs_text.text )
            return false;
        command->kind = TORIRSRC_FONT;
        command->u.font.font_id = component->u.rs_text.font_id;
        command->u.font.x = component->position.x;
        command->u.font.y = component->position.y;
        command->u.font.color = component->u.rs_text.color;
        command->u.font.center = component->u.rs_text.center;
        command->u.font.shadowed = component->u.rs_text.shadowed;
        command->u.font.width = component->position.width;
        command->u.font.height = component->position.height;
        command->u.font.text = game_runescape_expand_ui_text(game, component);
        return true;
    case UIELEM_RS_RECT:
    {
        int color = game_runescape_ui_rect_color(game, component);
        if( color == 0 && !component->u.rs_rect.filled )
            return false;
        command->kind = TORIRSRC_FILL_RECT;
        command->u.fill_rect.x = component->position.x;
        command->u.fill_rect.y = component->position.y;
        command->u.fill_rect.w = component->position.width;
        command->u.fill_rect.h = component->position.height;
        command->u.fill_rect.argb = color;
        return true;
    }
    default:
        return false;
    }
}

static enum RsPhaseResult
rs_phase_gc_events(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    struct ToriDraw_EventQueue* eq = game->scene ? ToriDraw_SceneEvents(game->scene) : NULL;
    if( eq )
    {
        while( game->frame.event_index < eq->count )
        {
            const struct ToriDraw_Event* ev = &eq->events[game->frame.event_index++];
            if( game_runescape_translate_gc_event(ev, command) )
                return RS_PHASE_YIELD;
        }
    }
    game->frame.phase = RS_FRAME_PHASE_BEGIN_3D;
    game->frame.event_index = 0;
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_begin_3d(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    if( game->frame.world_emitted )
    {
        game->frame.phase = RS_FRAME_PHASE_END_3D;
        return RS_PHASE_ADVANCE;
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
    return RS_PHASE_YIELD;
}

static bool
rs_resolve_painter_command(
    struct GameRunescape* game,
    struct World* world,
    const struct PaintersElementCommand* cmd,
    int* element_id,
    enum WorldPickType* pick_type,
    int* tile_x,
    int* tile_z,
    int* tile_level)
{
    *element_id = -1;
    *pick_type = WORLD_PICK_SCENERY;
    *tile_x = -1;
    *tile_z = -1;
    *tile_level = -1;

    switch( cmd->_bf_kind )
    {
    case PNTR_CMD_ELEMENT:
        *element_id = (int)cmd->_entity._bf_entity;
        if( ToriDraw_SceneElementIsLive(game->scene, *element_id) )
        {
            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(game->scene, *element_id);
            *pick_type = (element && element->dynamic) ? WORLD_PICK_PROJECTILE : WORLD_PICK_SCENERY;
        }
        break;
    case PNTR_CMD_TERRAIN:
        *tile_x = (int)cmd->_terrain._bf_terrain_x;
        *tile_z = (int)cmd->_terrain._bf_terrain_z;
        *tile_level = (int)cmd->_terrain._bf_terrain_y;
        *element_id = world_terrain_element_at(world, *tile_x, *tile_z, *tile_level);
        *pick_type = WORLD_PICK_TERRAIN;
        break;
    default:
        break;
    }

    return *element_id >= 0;
}

static enum RsPhaseResult
rs_phase_models(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
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
            int element_id;
            enum WorldPickType pick_type;
            int tile_x;
            int tile_z;
            int tile_level;

            if( !rs_resolve_painter_command(
                    game, world, cmd, &element_id, &pick_type, &tile_x, &tile_z, &tile_level) )
                continue;
            if( game_runescape_emit_draw_element(
                    game, element_id, pick_type, tile_x, tile_z, tile_level, command) )
                return RS_PHASE_YIELD;
        }

        game->frame.phase = RS_FRAME_PHASE_END_3D;
        return RS_PHASE_ADVANCE;
    }

    int slot_count = ToriDraw_SceneElementSlotCount(game->scene);
    while( game->frame.element_index < slot_count )
    {
        int element_id = game->frame.element_index++;
        enum WorldPickType pick_type = WORLD_PICK_SCENERY;
        if( ToriDraw_SceneElementIsLive(game->scene, element_id) )
        {
            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(game->scene, element_id);
            if( element && element->dynamic )
                pick_type = WORLD_PICK_PROJECTILE;
        }
        if( game_runescape_emit_draw_element(game, element_id, pick_type, -1, -1, -1, command) )
            return RS_PHASE_YIELD;
    }

    game->frame.phase = RS_FRAME_PHASE_END_3D;
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_end_3d(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_END_3D;
    game->frame.phase = game->ui_tree_ready ? RS_FRAME_PHASE_UI_2D_BEGIN : RS_FRAME_PHASE_DONE;
    return RS_PHASE_YIELD;
}

static enum RsPhaseResult
rs_phase_ui_begin(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_BEGIN_2D;
    game->frame.ui_2d_begun = true;
    game->frame.ui_current =
        game->ui_tree && game->ui_tree->root_index >= 0 ? game->ui_tree->root_index : -1;
    game->frame.ui_stack_top = -1;
    game->frame.phase = RS_FRAME_PHASE_UI_2D;
    return RS_PHASE_YIELD;
}

static enum RsPhaseResult
rs_phase_ui_step(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    if( !game->ui_tree || game->frame.ui_current < 0 )
    {
        game->frame.phase = RS_FRAME_PHASE_UI_2D_END;
        return RS_PHASE_ADVANCE;
    }

    struct StaticUIComponent* component = &game->ui_tree->components[game->frame.ui_current];
    if( !game_runescape_ui_node_visible(game, component, game->frame.ui_current) )
    {
        game_runescape_ui_advance(game, game->frame.ui_current);
        return RS_PHASE_ADVANCE;
    }

    if( component->is_dirty && game_runescape_emit_ui_component(game, component, command) )
    {
        int32_t cur = game->frame.ui_current;
        game_runescape_ui_advance(game, cur);
        return RS_PHASE_YIELD;
    }
    game_runescape_ui_advance(game, game->frame.ui_current);
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_ui_end(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_END_2D;
    game->frame.phase = RS_FRAME_PHASE_DONE;
    return RS_PHASE_YIELD;
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
    game->frame.ui_2d_begun = false;
    game->frame.ui_current = -1;
    game->frame.ui_stack_top = -1;
    game->ui_hovered_node = -1;
    if( game->ui_tree && game->ui_tree_ready )
        game->ui_hovered_node = game_runescape_ui_hit_test(game, game->mouse_x, game->mouse_y);
    world_pickset_reset(&game->pickset);
    for( int i = 0; i < cycles_elapsed; i++ )
        game_runescape_tick_animations(game);
    if( game->world )
        world_cycle(game->world, cycles_elapsed);
    game_runescape_drain_world_events(game);
    game_runescape_sync_projectiles_to_scene(game);
    game_runescape_update_world_viewport(game);
    if( game->ui_tree && game->ui_tree->component_count > 0 )
    {
        uitree_mark_all_dirty(game->ui_tree);
        game->ui_tree_ready = true;
    }
}

bool
game_runescape_frame_next_command(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        enum RsPhaseResult r;
        switch( game->frame.phase )
        {
        case RS_FRAME_PHASE_GC_EVENTS:
            r = rs_phase_gc_events(game, command);
            break;
        case RS_FRAME_PHASE_BEGIN_3D:
            r = rs_phase_begin_3d(game, command);
            break;
        case RS_FRAME_PHASE_MODELS:
            r = rs_phase_models(game, command);
            break;
        case RS_FRAME_PHASE_END_3D:
            r = rs_phase_end_3d(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D_BEGIN:
            r = rs_phase_ui_begin(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D:
            r = rs_phase_ui_step(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D_END:
            r = rs_phase_ui_end(game, command);
            break;
        default:
            return false;
        }
        if( r == RS_PHASE_YIELD )
            return true;
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
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level,
    int startheight,
    int endheight,
    int delay,
    int angle,
    int length,
    int offset,
    int step)
{
    if( !game || !game->world || !game->scene || !game->td )
        return -1;

    level = clamp_terrain_level(level);

    int const range_x = dst_sx - src_sx;
    int const range_z = dst_sz - src_sz;
    int const range = abs(range_x) > abs(range_z) ? abs(range_x) : abs(range_z);
    int const flight = length + range * step;
    int const t1 = delay;
    int const t2 = delay + flight;

    int const src_x = src_sx * 128 + 64;
    int const src_z = src_sz * 128 + 64;
    int const dst_x = dst_sx * 128 + 64;
    int const dst_z = dst_sz * 128 + 64;

    int h1 = 0;
    if( game->world->heightmap )
        h1 = heightmap_get_interpolated(game->world->heightmap, src_x, src_z, level) -
             startheight * 4;

    struct ToriDraw_ModelHandle cached = ToriAuxLibTD_Model(game->td, model_id);
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
    assert(element_id >= 0);

    ToriDraw_SceneElementSetModel(game->scene, element_id, hnd);

    if( seq_id != -1 )
    {
        ToriDraw_SceneElementSetAnimationSeq(game->scene, element_id, seq_id);
        struct ToriDraw_Animation* anim = ToriAuxLibTD_SequenceAnimation(game->td, seq_id);
        ToriDraw_SceneElementSetAnimation(game->scene, element_id, anim, true);
    }

    int projectile_idx = world_projectile_spawn(
        game->world,
        element_id,
        level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        h1,
        endheight * 4,
        t1,
        t2,
        angle,
        offset);
    assert(projectile_idx >= 0);

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    assert(element);
    element->dynamic = true;

    return projectile_idx;
}

struct ToriRunescape_Task_AddProjectile
{
    struct pt thread;
    struct GameRunescape* game;
    int model_id;
    int seq_id;
    int src_sx;
    int src_sz;
    int dst_sx;
    int dst_sz;
    int level;
};

struct ToriRunescape_Task_AddProjectile*
ToriRunescape_Task_AddProjectile_New(
    struct GameRunescape* game,
    int model_id,
    int seq_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level)
{
    struct ToriRunescape_Task_AddProjectile* task =
        calloc(1, sizeof(struct ToriRunescape_Task_AddProjectile));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->model_id = model_id;
    task->seq_id = seq_id;
    task->src_sx = src_sx;
    task->src_sz = src_sz;
    task->dst_sx = dst_sx;
    task->dst_sz = dst_sz;
    task->level = level;
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
    struct ToriRunescape_Task_AddProjectile* task =
        (struct ToriRunescape_Task_AddProjectile*)task_state;
    struct ToriAuxLibTD* td = NULL;
    struct RSCacheDat2A_Model* model;
    int decoded_model_id;
    int idx;

    PT_BEGIN(&task->thread);

    td = task->game ? task->game->td : NULL;
    assert(!(!task->game || !td || !task->game->world));

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
        task->src_sx,
        task->src_sz,
        task->dst_sx,
        task->dst_sz,
        task->level,
        RUNESCAPE_PROJECTILE_STARTHEIGHT,
        RUNESCAPE_PROJECTILE_ENDHEIGHT,
        RUNESCAPE_PROJECTILE_DELAY,
        RUNESCAPE_PROJECTILE_ANGLE,
        RUNESCAPE_PROJECTILE_LENGTH,
        RUNESCAPE_PROJECTILE_OFFSET,
        RUNESCAPE_PROJECTILE_STEP);
    (void)idx;

    ToriRunescape_Task_AddProjectile_Free(task);
}
return PT_ENDED;
}
