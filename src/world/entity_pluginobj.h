#ifndef WORLD_ENTITY_PLUGINOBJ_H
#define WORLD_ENTITY_PLUGINOBJ_H

#include "entity_facets.h"

#include <stdbool.h>

/*
 * A model a plugin owns, standing in the scene among the locs and the entities.
 *
 * Its own pool, and not either of the two things it superficially resembles:
 *
 *   - a spotanim is retired by its own lifetime clock, which is exactly what a
 *     plugin object must NOT have -- a loot beam burns until the item is gone,
 *     however long that is, and giving it a lifetime would mean picking a
 *     number that is wrong in both directions.
 *   - a runtime scenery entity is offered to the pick classifier, which would
 *     put a plugin's decoration on the right-click menu as though the server
 *     had placed a loc there.
 *
 * Position is scene-local, in the same 1/128-of-a-tile units the rest of the
 * world speaks. The ABSOLUTE tile a plugin named lives on the app side, which
 * is what re-places these across a scene rebuild.
 */
struct WorldEntity_PluginObject
{
    int element_id;
    int level;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_Orientation orientation;
    /** Painter footprint in tiles, 1 or more. */
    int size_x;
    int size_z;
    /** False leaves the object in the pool but out of the painter, so a plugin
     *  can blink one off without paying for the model rebuild. */
    bool active;
};

#endif
