#ifndef ENTITY_SCENEBUILD_H
#define ENTITY_SCENEBUILD_H

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

#endif