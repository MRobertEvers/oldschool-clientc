#ifndef WORLD_SCENEBUILD_H
#define WORLD_SCENEBUILD_H

#include "osrs/world.h"

#include <stdint.h>

void
world_scenebuild_player_entity_set_appearance(
    struct World* world,
    int player_entity_id,
    uint16_t* appearances,
    uint16_t* colors);

void
world_scenebuild_npc_entity_set_npc_type(
    struct World* world,
    int npc_entity_id,
    int npc_type);

#endif