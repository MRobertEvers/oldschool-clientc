#ifndef ENTITY_SCENEBUILD_H
#define ENTITY_SCENEBUILD_H

#include "graphics/dash.h"
#include "osrs/datatypes/player_appearance.h"
#include "osrs/game.h"

void
entity_scenebuild_player_change_appearance(
    struct GGame* game,
    int player_id,
    struct PlayerAppearance* appearance);

void
entity_scenebuild_npc_change_type(
    struct GGame* game,
    int npc_id,
    int npc_type);

/** Update or create SceneElement for top obj at (level, sx, sz). Call after obj_add, obj_del,
 * obj_count. */
void
entity_scenebuild_obj_stack_update_tile(
    struct GGame* game,
    int level,
    int sx,
    int sz);

struct GameCache;
struct World;

/** Per-frame: refresh obj-stack Scene2 meshes from cache (mirrors NPC reload before painter). */
void
entity_scenebuild_obj_stack_reload_scene2_for_active_entity(
    struct World* world,
    struct GameCache* gamecache,
    int obj_stack_entity_id);

/* Get head model for interface MODEL component (chat head). model_type 2=NPC, 3=player.
 * For player, slots and colors from entity appearance. Caller must dashmodel_free result. */
struct DashModel*
entity_scenebuild_head_model_for_component(
    struct GGame* game,
    int model_type,
    int model_id,
    int* slots,
    int* colors);

/** Same as entity_scenebuild_head_model_for_component but skips lighting (apply after animate). */
struct DashModel*
entity_scenebuild_head_model_for_component_lights(
    struct GGame* game,
    int model_type,
    int model_id,
    int* slots,
    int* colors,
    bool apply_default_light);

/** Client calculateNormals after interface MODEL animation — Tori _light_model_default(0,0). */
void
entity_scenebuild_interface_head_apply_default_light(struct DashModel* dash_model);

/** Apply a LOC_ADD_CHANGE to the scene: find the existing MapBuildLocEntity at (sx,sz) with
 * matching layer, swap its model and collision.  loc_id < 0 removes the loc visually. */
void
entity_scenebuild_loc_apply_change(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int angle,
    int loc_id);

/** Apply a LOC_DEL: equivalent to entity_scenebuild_loc_apply_change with loc_id = -1. */
void
entity_scenebuild_loc_apply_del(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int angle);

/** Apply a LOC_ANIM: find the existing MapBuildLocEntity at (sx,sz) with matching layer and
 * start the given animation sequence. */
void
entity_scenebuild_loc_apply_anim(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int seq_id);

#endif