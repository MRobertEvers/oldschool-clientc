#ifndef WORLD_ENTITY_SPOTANIM_H
#define WORLD_ENTITY_SPOTANIM_H

#include "entity_facets.h"

#include <stdbool.h>

struct WorldEntity_Spotanim
{
    int element_id;
    int level;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_Orientation orientation;
    int idle_cycles;
    int active_cycle;
    int lifetime;
    bool active;
};

#endif
