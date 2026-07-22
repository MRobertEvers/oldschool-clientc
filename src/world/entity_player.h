#ifndef WORLD_ENTITY_PLAYER_H
#define WORLD_ENTITY_PLAYER_H

#include "entity_facets.h"

struct WorldEntity_Player
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_Pathing pathing;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
    struct WorldEntityFacet_Facing facing;
    struct WorldEntityFacet_Combat combat;
    struct WorldEntityFacet_Appearance appearance;
    struct WorldEntityFacet_ExactMove exact_move;
    struct WorldEntityFacet_EntitySpotanim spotanim;
    char name[32];
    int combat_level;
    int gender;
    int headicon;
    /** Server player slot (pid) this entity mirrors; -1 = local/unsynced. */
    int server_pid;
};

#endif
