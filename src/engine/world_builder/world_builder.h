#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "world/world.h"
#include "contour_ground_queue.h"

#include <stdbool.h>
#include <stdint.h>

struct CacheProvider;
struct ToriDraw_Scene;
struct VarPManager;
struct Blendmap;
struct Overlaymap;
struct TerrainShapeMap;
struct DecorBuildMap;
struct Lightmap;
struct SharelightMap;
struct Shademap2;
struct FlagMap;
struct OccluderBuildmap;
struct SceneOccluders;

struct WorldBuilder
{
    struct World* world;
    struct CacheProvider* cache;
    struct ToriDraw_Scene* scene;
    struct VarPManager* varp;

    struct Blendmap* blendmap;
    struct Overlaymap* overlaymap;
    struct TerrainShapeMap* terrain_shapemap;
    struct DecorBuildMap* decor_buildmap;
    struct Lightmap* lightmap;
    struct SharelightMap* sharelight_map;
    struct Shademap2* shademap;
    struct FlagMap* flag_map;
    /** Build-only mapo bitfield; freed after the greedy merge emits occluders. */
    struct OccluderBuildmap* occluder_buildmap;
    struct ContourGroundQueue contour_ground_queue;

    /** Set during scenery chunk rebuild for scenery_load_model diagnostics. */
    int scenery_mapx;
    int scenery_mapz;
    int scenery_base_loc_id;

    /** Set while WorldBuilder_ApplyLocChange spawns a loc so scenery_load_model
     *  flags the resulting pool entry runtime_spawn (per-frame painter
     *  re-registration). 0 for the normal build path. */
    int scenery_runtime_spawn;

    /** element_id -> scenery pool index for the elements the shape helper
     *  currently has in flight, so the position pass can stamp the placement
     *  debug fields (WorldEntity_SceneryDebug.draw_*) without scanning the
     *  pool. A ring rather than one slot because the double wall-decor shape
     *  loads both its elements before positioning either. -1 = empty. */
    int scenery_dbg_element[4];
    int scenery_dbg_pool[4];
    int scenery_dbg_next;

    /** Quarter turns the shape helper is deferring to a draw-time element yaw
     *  instead of baking into the vertices (animated locs — see
     *  world_builder_prerotate_placement). Set immediately before the
     *  scenery_load_model call it applies to; that call consumes and clears it,
     *  so a helper that bakes its rotation never has to reset it. */
    int scenery_deferred_angle;
};

/**
 * Rewrite a loc's resize/offset so that applying them BEFORE `quarter_turns`
 * of rotation is equivalent to applying them AFTER.
 *
 * The reference orders a loc's transforms rotate -> resize -> translate. This
 * port bakes the rotation into the vertices and matches that order exactly —
 * except for animated locs, where it cannot: the reference rotates *after*
 * animating, so the rotation is deferred to a draw-time element yaw and the
 * resize/offset would otherwise be applied in the unrotated frame.
 *
 * Since R(T_o . S) = T_{Ro} . (R S R^-1) . R, applying T_{R^-1 o} and
 * R^-1 S R before R reproduces T_o . S . R. A quarter turn swaps the x/z scale
 * axes and maps an offset (x,z) -> (-z,x); the y axis is untouched.
 *
 * Exposed (rather than static) so the equivalence can be asserted in a test —
 * no loc in the shipped caches has both an animation and a non-uniform resize
 * or an x/z offset, so nothing else would catch a sign error here.
 */
void
world_builder_prerotate_placement(
    int quarter_turns,
    int* resize_x,
    int* resize_z,
    int* offset_x,
    int* offset_z);

/**
 * The DRAW footprint of a loc: its declared tile footprint grown to cover the
 * tiles its model actually lands on, by at most `margin` tiles per side.
 *
 * The painter gives a loc one slot in the command stream, at the ring of the
 * NEAREST tile of the footprint it is registered on — everything nearer is
 * drawn afterwards. A tile the MODEL covers but the FOOTPRINT does not, and
 * which is nearer than that, therefore has its ground painted on top of the
 * loc: a strip of terrain lying over a floor. See scenery_grow_draw_footprint
 * in world_scenery.u.c for why the margin is capped rather than taking the
 * whole model extent, and LARGE_LOCS_PAINTER.md for the scene that forced it.
 *
 * Pure geometry, so the cap and the clamping are unit-testable without a cache:
 * inputs are tile coordinates, `model_*` is the model's own tile span, and the
 * result is clamped into [0, scene_size). Exposed for that reason only.
 */
void
world_builder_draw_footprint(
    int sx,
    int sz,
    int size_x,
    int size_z,
    int model_min_x,
    int model_max_x,
    int model_min_z,
    int model_max_z,
    int margin,
    int scene_size,
    int* out_sx,
    int* out_sz,
    int* out_size_x,
    int* out_size_z);

/** Apply a zone LOC change at runtime (Client-TS locChangeUnchecked): remove the
 * existing loc in the shape's layer on the tile (scene element + collision) and,
 * when loc_id >= 0, spawn the replacement (scene + collision, re-registered with
 * the painter each frame). scene_x/scene_z are scene-local tile coords. */
void
WorldBuilder_ApplyLocChange(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle);

struct WorldBuilder*
WorldBuilder_New(
    struct World* world,
    struct CacheProvider* cache,
    struct ToriDraw_Scene* scene,
    struct VarPManager* varp);

void
WorldBuilder_Free(struct WorldBuilder* builder);

void
WorldBuilder_RebuildCenterzone(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
WorldBuilder_RebuildCenterzoneBegin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
WorldBuilder_RebuildCenterzoneChunkTerrain(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneChunkScenery(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneChunk(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneEnd(struct WorldBuilder* builder);

void
WorldBuilder_RebuildChunklist(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

void
WorldBuilder_RebuildChunklistBegin(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

/* ------------------------------------------------------------------ */
/* Instanced scenes                                                    */
/* ------------------------------------------------------------------ */

/** Zones per axis in a REBUILD_REGION descriptor grid. 13, because that is a
 *  104-tile scene in 8-tile zones — the same 13 as PKT_MAP_REBUILD_ZONES, which
 *  is the wire's count of the same grid. */
#define WORLD_INSTANCE_ZONES 13

/**
 * Rebuild the scene from a REBUILD_REGION descriptor grid rather than from the
 * map squares under it.
 *
 * `zones` is PKT_MAP_REBUILD_ZONES ints indexed
 * `[level * 13 * 13 + zone_x * 13 + zone_z]`, each 0 (void) or the client's
 * packed template-chunk form — see struct PktMapRebuild. The scene base is
 * `(zone_center - 6) * 8` exactly as in the ordinary rebuild, so everything
 * downstream (entity coords, the minimap, the shift on the next rebuild) is
 * unchanged; only where the tiles come from differs.
 */
void
WorldBuilder_RebuildInstance(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones);

/** One zone's terrain, copied from `src_zone_*` with `rotation` quarter-turns.
 *  Destination zone coords are scene-relative (0..12). */
void
WorldBuilder_RebuildInstanceZoneTerrain(
    struct WorldBuilder* builder,
    int dst_zone_x,
    int dst_zone_z,
    int dst_level,
    int src_zone_x,
    int src_zone_z,
    int src_level,
    int rotation);

/** One zone's scenery, same arguments. Must run after every zone's terrain: the
 *  loc placement reads heights the terrain pass writes. */
void
WorldBuilder_RebuildInstanceZoneScenery(
    struct WorldBuilder* builder,
    int dst_zone_x,
    int dst_zone_z,
    int dst_level,
    int src_zone_x,
    int src_zone_z,
    int src_level,
    int rotation);

/**
 * Where in a source zone the destination tile (dx, dz) comes from.
 *
 * The terrain copy's direction: it walks the destination and reads the source.
 * For rotation r and local coords in 0..7,
 *
 *     r=0  (dx, dz)        r=1  (dz, 7-dx)
 *     r=2  (7-dx, 7-dz)    r=3  (7-dz, dx)
 *
 * This is `mock230_mapinstance_rotate_to_src`'s twin, and the two must agree or
 * the server's collision and the client's geometry describe mirrored rooms.
 */
static inline void
world_instance_rotate_to_src(
    int rotation,
    int dx,
    int dz,
    int* out_sx,
    int* out_sz)
{
    switch( rotation & 3 )
    {
    case 1:
        *out_sx = dz;
        *out_sz = 7 - dx;
        break;
    case 2:
        *out_sx = 7 - dx;
        *out_sz = 7 - dz;
        break;
    case 3:
        *out_sx = 7 - dz;
        *out_sz = dx;
        break;
    default:
        *out_sx = dx;
        *out_sz = dz;
        break;
    }
}

/**
 * Where a source tile lands in the destination zone — the inverse, and the
 * scenery copy's direction (it walks the source's locs).
 *
 * `size_x`/`size_z` are the loc's footprint as placed, already swapped for its
 * own odd angle. The subtraction is why a 1x2 table stays over the tiles it was
 * drawn on: a quarter-turn moves its south-west corner to a different corner of
 * the rectangle, and the extent that now runs backwards has to come off.
 */
static inline void
world_instance_rotate_to_dst(
    int rotation,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int* out_dx,
    int* out_dz)
{
    if( size_x < 1 )
        size_x = 1;
    if( size_z < 1 )
        size_z = 1;

    switch( rotation & 3 )
    {
    case 1:
        *out_dx = 7 - sz - (size_z - 1);
        *out_dz = sx;
        break;
    case 2:
        *out_dx = 7 - sx - (size_x - 1);
        *out_dz = 7 - sz - (size_z - 1);
        break;
    case 3:
        *out_dx = sz;
        *out_dz = 7 - sx - (size_x - 1);
        break;
    default:
        *out_dx = sx;
        *out_dz = sz;
        break;
    }
}

#endif
