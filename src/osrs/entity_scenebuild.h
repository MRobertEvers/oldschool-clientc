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

/* Get head model for interface MODEL component (chat head). model_type 2=NPC, 3=player.
 * For player, slots and colors from entity appearance. Caller must dashmodel_free result. */
struct DashModel*
entity_scenebuild_head_model_for_component(
    struct GGame* game,
    int model_type,
    int model_id,
    int* slots,
    int* colors);

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