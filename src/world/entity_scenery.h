#ifndef WORLD_ENTITY_SCENERY_H
#define WORLD_ENTITY_SCENERY_H

#include "entity_facets.h"

#include <stdbool.h>
#include <stdlib.h>

/**
 * Placement provenance for one loc instance — diagnostic only.
 *
 * A loc that renders on the wrong square looks identical whether the map
 * stream decoded the wrong chunk coords, the chunk->scene mapping is off by
 * the square origin, or the element was positioned off its own slot. Nothing
 * downstream distinguishes those, because by the time anything can be
 * inspected only `grid_position` survives. These fields keep every input the
 * slot was derived from on the entity so the hover readout can show them side
 * by side (see WorldEntity_SceneryDebugEnabled).
 *
 * Filled at build time. Nothing but the debug readout reads them.
 */
struct WorldEntity_SceneryDebug
{
    /** Map square (l<x>_<z>) this instance was decoded from. -1,-1 for a
     *  runtime LOC_ADD_CHANGE spawn, which has no source square. */
    int map_square_x;
    int map_square_z;
    /** Chunk-local tile inside that square (0..63) as the loc stream stored
     *  it, so `map_square * 64 + chunk` is the absolute tile the cache asked
     *  for. On a runtime spawn these are already scene coords (the zone packet
     *  names a scene tile), flagged by `runtime`. */
    int chunk_x;
    int chunk_z;
    /** Level the loc stream stored, before any bridge push-down. */
    int chunk_level;
    /** world->_base_tile_x/z at build: absolute tile of scene tile 0, so
     *  `base + grid_position` is the absolute tile the slot resolved to. */
    int base_tile_x;
    int base_tile_z;
    /** LocType.width/length, NOT angle-swapped (entity size_x/size_z is the
     *  angle-swapped route footprint, which for ground decor is neither the
     *  render nor the draw footprint). */
    int config_size_x;
    int config_size_z;
    /** Footprint the geometry was actually positioned and painter-registered
     *  over — 1x1 for every shape except centrepieces. */
    int draw_size_x;
    int draw_size_z;
    /** Scene element world position after placement (1/128 tile units), i.e.
     *  where the model really draws. Placement centres the model over its draw
     *  footprint, so the anchor tile is `(draw_x - 64 * draw_size_x) >> 7`, not
     *  `draw_x >> 7` (which for an even footprint lands one tile north-east).
     *  Captured at scenery_element_position_init, so the wall-decor offsets
     *  applied in the later decor pass are not reflected (they shift decor off
     *  the tile centre by design). */
    int draw_x;
    int draw_y;
    int draw_z;
    int draw_yaw;
    /** 1 = spawned by a zone LOC change rather than the map stream. */
    int runtime;
};

/**
 * TORIRS_LOC_DEBUG: surface loc placement provenance in the minimenu, and let
 * every loc be picked while doing it — including the inactive ones the pick
 * classifier normally drops (LocType.active), which is most of the scenery a
 * misplacement is visible on (bushes, walls, gravel).
 *
 * Read once per translation unit; flipping the env var mid-run does nothing.
 */
static inline bool
WorldEntity_SceneryDebugEnabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_LOC_DEBUG") != NULL;
    return enabled != 0;
}

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
    /** Placement provenance for the TORIRS_LOC_DEBUG hover readout. */
    struct WorldEntity_SceneryDebug debug;
};

#endif
