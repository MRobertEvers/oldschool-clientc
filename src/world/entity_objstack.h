#ifndef WORLD_ENTITY_OBJSTACK_H
#define WORLD_ENTITY_OBJSTACK_H

#include "entity_facets.h"

/* Ground item stack (zone OBJ_ADD/DEL/COUNT packets). The element draws the
 * TOP of the stack; obj_id/count mirror the server's authoritative stack. */
struct WorldEntity_ObjStack
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    int obj_id;
    int count;
};

#endif
