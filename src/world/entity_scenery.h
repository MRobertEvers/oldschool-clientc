#ifndef WORLD_ENTITY_SCENERY_H
#define WORLD_ENTITY_SCENERY_H

#include "entity_facets.h"

struct WorldEntity_Scenery
{
    int element_id;
    int loc_id;
    struct WorldEntityFacet_GridPosition grid_position;
    /** ROUTE footprint: the loc's config size, angle-swapped, which is what the
     *  click-time approach test measures against. Not the render footprint —
     *  ground decor routes as its full config size but draws on one tile. */
    int size_x;
    int size_z;
    /** Loc shape (RSCACHE_LOC_SHAPE_*, the shape the MAP placed) and rotation
     *  (0..3 = W/N/E/S). Kept for the op-click approach test (reference
     *  interactWithLoc passes shape+angle for walls, size for centrepieces) —
     *  geometry already consumed them, but routing needs them at click time. */
    int shape;
    int angle;
    /** LocType.forceapproach (config opcode 69) already rotated into the placed
     *  frame: DirectionFlag bits naming sides the approach test must refuse.
     *  0 = any side. See ToriRS_Location.force_approach. */
    int force_approach;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_AnimationStep animation;
    char name[32];
    struct WorldEntityFacet_Action actions[5];
    /** LocType.active. The reference negates a non-active loc's scene
     *  typecode so Model.draw never records it as a pick hit; torirs filters
     *  in torirs_pick.c instead (walls/gravel/floor decor stay unclickable). */
    int interactive;
    /** Set for locs spawned at runtime by a zone LOC_ADD_CHANGE (e.g. an open
     *  door). The painter's static set is baked at build time and
     *  painter_reset_to_static truncates anything added later, so these must be
     *  re-registered with the painter every frame like dynamics
     *  (world_cycle World_CycleRegisterPainterDynamics). Build-time scenery
     *  leaves this 0 and lives in the baked static set. */
    int runtime_spawn;
    /** Painter wall registration for a runtime-spawned WALL loc: WALL_A/WALL_B
     *  (-1 = not a wall — re-registered as normal scenery) plus the wallside
     *  the build path passes to painter_add_wall. Recorded at spawn time
     *  (scenery_add_wall_*) so the per-frame painter re-registration draws the
     *  wall on the correct side of its tile instead of as centre scenery. */
    int painter_wall_ab;
    int painter_wall_side;
};

#endif
