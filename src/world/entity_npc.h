#ifndef WORLD_ENTITY_NPC_H
#define WORLD_ENTITY_NPC_H

#include "entity_facets.h"

struct WorldEntity_NPC
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_Pathing pathing;
    int npc_id;
    int size;
    int combat_level;
    char name[32];
    struct WorldEntityFacet_Action actions[5];
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
    struct WorldEntityFacet_Facing facing;
    struct WorldEntityFacet_Combat combat;
};

#endif
