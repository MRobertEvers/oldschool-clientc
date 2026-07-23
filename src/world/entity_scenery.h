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
    /** Loc shape (RSCACHE_LOC_SHAPE_*) and rotation (0..3 = W/N/E/S). Kept for
     *  the op-click approach test (reference interactWithLoc passes shape+angle
     *  for walls, size for centrepieces) — geometry already consumed them, but
     *  routing needs them at click time. */
    int shape;
    int angle;
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
};

#endif
