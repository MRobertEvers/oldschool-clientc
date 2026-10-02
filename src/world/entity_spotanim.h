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
    /** The spotanimtype this graphic draws (MAP_ANIM's id), -1 for one the
     *  client made up itself. World never reads it: it is kept so the quest
     *  driver can say WHICH graphic sits on a tile (DriveUi_Spotanims) -- a
     *  raid hazard (Xarpus acid, Maiden blood) is told apart by this id and
     *  nothing else. Set by the spawner after World_SpotanimSpawn. */
    int spotanim_id;
};

#endif
