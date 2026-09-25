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
    /* ObjType name + ground ops, snapshotted at add time so the right-click
     * builder never needs the cache provider (world/ stays a leaf module —
     * same arrangement as WorldEntity_Scenery).
     * 64, matching ToriRS_Objtype.name (TORIRS_NAME_MAX) -- col-tagged names
     * don't fit in 32. */
    char name[64];
    struct WorldEntityFacet_Action actions[5];
    /*
     * OBJ_ADD's ownership half, for the cache's ground-items overlay.
     *
     * The two clocks are DEADLINES in RS_CS2Host::client_clock units (one per
     * 20 ms logic tick), not the packet's remaining-ticks: a remaining count
     * would have to be decremented by something every tick, and nothing walks
     * this pool per tick. -1 is "the server never said", which is what every
     * pre-ownership revision sends and what a locally spawned pile has.
     *
     * `owner` is OBJ_ADD's ownershipType as-is (0/1 public-or-mine, 2 someone
     * else's, 3 a group ironman's), and it is what OBJ_OWNER answers.
     */
    int public_clock;
    int despawn_clock;
    int owner;
    int never_becomes_public;
};

#endif
