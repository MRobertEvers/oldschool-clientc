#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "world/world.h"
#include "contour_ground_queue.h"

#include <stdbool.h>

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

#endif
