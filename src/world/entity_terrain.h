#ifndef WORLD_ENTITY_TERRAIN_H
#define WORLD_ENTITY_TERRAIN_H

#include "entity_facets.h"

struct WorldEntity_Terrain
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
};

#endif
