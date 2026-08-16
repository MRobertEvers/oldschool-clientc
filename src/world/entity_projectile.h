#ifndef WORLD_ENTITY_PROJECTILE_H
#define WORLD_ENTITY_PROJECTILE_H

#include "entity_facets.h"

#include <stdbool.h>

/** No target entity — the projectile flies to its fixed destination tile
 * (reference ClientProj.target == 0). */
#define WORLD_PROJECTILE_TARGET_NONE 0

struct WorldEntity_Projectile
{
    int element_id;
    int level;
    int dst_level;

    /* Immutable params (world units / ticks). */
    int src_x;
    int src_z;
    int h1;
    int end_height;
    int t1;
    int t2;
    int angle;
    int startpos;

    /** Wire target-entity id (reference MAP_PROJANIM targetEntity, `g2b`):
     * `slot + 1` for an NPC, `-(slot) - 1` for a player (the local player
     * included, via its server pid), WORLD_PROJECTILE_TARGET_NONE for none.
     * Kept in the wire encoding rather than split into kind+slot so the
     * despawn/re-sync id space stays the server's. */
    int target;

    /* Dynamic state. dst_x/dst_z are re-aimed every cycle at the target
     * entity's live draw position while it stays synced (reference
     * addProjectiles); with no target they hold the spawn destination. */
    int dst_x;
    int dst_z;
    int cycle;
    bool launched;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double velocity;
    double ay;
    struct WorldEntityFacet_OrientationPYR orientation;
};

#endif
