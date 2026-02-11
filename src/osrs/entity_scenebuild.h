#ifndef ENTITY_SCENEBUILD_H
#define ENTITY_SCENEBUILD_H

#include "graphics/dash.h"
#include "osrs/datatypes/player_appearance.h"
#include "osrs/game.h"

struct PlayerEntity*
entity_scenebuild_player_get(
    struct GGame* game,
    int player_id);

void
entity_scenebuild_player_change_appearance(
    struct GGame* game,
    int player_id,
    struct PlayerAppearance* appearance);

void
entity_scenebuild_player_release(
    struct GGame* game,
    int player_id);

struct NPCEntity*
entity_scenebuild_npc_get(
    struct GGame* game,
    int npc_id);

void
entity_scenebuild_npc_change_type(
    struct GGame* game,
    int npc_id,
    int npc_type);

void
entity_scenebuild_npc_release(
    struct GGame* game,
    int npc_id);

/* Get head model for interface MODEL component (chat head). model_type 2=NPC, 3=player.
 * For player, slots and colors from entity appearance. Caller must dashmodel_free result. */
struct DashModel*
entity_scenebuild_head_model_for_component(
    struct GGame* game,
    int model_type,
    int model_id,
    int* slots,
    int* colors);

#endif