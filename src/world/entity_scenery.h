#ifndef WORLD_ENTITY_SCENERY_H
#define WORLD_ENTITY_SCENERY_H

#include "entity_facets.h"

struct WorldEntity_Scenery
{
    int element_id;
    int loc_id;
    struct WorldEntityFacet_GridPosition grid_position;
    int size_x;
    int size_z;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_AnimationStep animation;
    char name[32];
    struct WorldEntityFacet_Action actions[5];
};

#endif
