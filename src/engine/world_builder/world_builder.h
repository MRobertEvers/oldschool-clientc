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

/** Longest placement op label a hidden record keeps, NUL included -- the
 *  width of a zone LOC_ADD_CHANGE_V2 op label (Task_AppSpawn loc_ops). */
#define WORLD_BUILDER_HIDDEN_LOC_OP_LEN 32

/*
 * A placement a multiloc hid (-1), remembered so a varp change can bring it back.
 *
 * The reference never drops such a placement. A loc with a transform table is a
 * DynamicObject that stays in the scene with its BASE id, and its model is
 * re-resolved on every draw: `method4108` walks the transform table and answers
 * null for a -1 rung, `method4109` then draws nothing -- and draws the child
 * again the frame the varbit changes (Deobfuscator/src_osrs239/deob/
 * class123.java, runelite's DynamicObject). This port resolves a multiloc ONCE,
 * when it places it, so the static build, the instance build and
 * WorldBuilder_ApplyLocChange write the skipped placement down here instead,
 * keyed on the tile, the level and the loc LAYER (World_SceneryFindAt's key).
 *
 * `op_flags`/`ops` are the placement's OWN menu, the same pair a zone
 * LOC_ADD_CHANGE_V2 carries (App_WorldLocChangeOps): the scene loc owns them in
 * the reference, so they survive the hide with it. A map placement has none
 * (0x1f, every label ""); a zone change that lands while its multiloc is -1
 * stamps its pair on the record (WorldBuilder_HiddenLocSetOps), and the
 * re-placement hands it back to the loc-change path.
 */
struct WorldBuilderHiddenLoc
{
    int scene_x;
    int scene_z;
    int level;
    int loc_id;
    int shape;
    int angle;
    /** 5-bit shown mask, bit 0 = op1. */
    int op_flags;
    /** Replacement label per slot; "" keeps the loctype's. */
    char ops[5][WORLD_BUILDER_HIDDEN_LOC_OP_LEN];
};

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

    /**
     * Scene element pools this builder owns (toridraw_scene.h): the STATIC
     * half holds its terrain and scenery, the DYNAMIC half its world's
     * entities. Every world view shares one ToriDraw_Scene, so the pools are
     * what keep a boat's rebuild from freeing the mainland's elements — and
     * the mainland's rebuild from sweeping the boat's. WorldBuilder_New binds
     * the root pair; WorldBuilder_SetSceneView moves a builder onto a view's.
     */
    int static_pool;
    int dynamic_pool;

    /** Placements a multiloc currently hides (struct WorldBuilderHiddenLoc).
     *  A rebuild begin forgets them all, a loc change on the same tile and
     *  layer forgets that one, WorldBuilder_Free releases the array. */
    struct WorldBuilderHiddenLoc* hidden_locs;
    int hidden_loc_count;
    int hidden_loc_capacity;
};

/** How many placements a multiloc hides in this builder's scene right now. */
int
WorldBuilder_HiddenLocCount(struct WorldBuilder const* builder);

/** The `index`th hidden placement, 0 <= index < WorldBuilder_HiddenLocCount.
 *  Valid until the next builder call that places, changes or rebuilds. */
struct WorldBuilderHiddenLoc const*
WorldBuilder_HiddenLocGet(
    struct WorldBuilder const* builder,
    int index);

/**
 * Stamp a placement menu on the hidden record at this tile, level and loc
 * layer. Returns 1 when a record took it, 0 when nothing is hidden there --
 * the ordinary answer for a change whose loc is shown or was refused.
 */
int
WorldBuilder_HiddenLocSetOps(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    int op_flags,
    char const ops[5][WORLD_BUILDER_HIDDEN_LOC_OP_LEN]);

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

/*
 * The collision a loc change will leave once WorldBuilder_ApplyLocChange lands
 * it, written into `maps` (COLLISION_LEVELS entries, each NULL or a copy of
 * the world's map for that level) instead of into the world's own: whatever
 * stands in the shape's layer on the tile now is undone, then the new loc is
 * stamped through its multiloc rung. The world is not touched, so a preview
 * can never unbalance the add/del pairs the landing will make.
 *
 * For a reader that must see the change on its packet's tick while the scene
 * waits on the new loc's models (the quest driver's route/plan collision,
 * torirs_plugin_drive_ui.c). One difference from the landing, deliberate: the
 * new loc is stamped whether or not its models exist, as the server and the
 * scriptrun lane stamp it -- ApplyLocChange stamps only a loc that spawned.
 *
 * Returns 0, having undone the old loc only, when the new loc's config is not
 * resident: the caller says so rather than read the gap as open ground.
 */
int
WorldBuilder_LocChangeCollisionInto(
    struct WorldBuilder* builder,
    struct CollisionMap* const* maps,
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

/**
 * Bind this builder's scene elements to world view `view_id` (worldview.h) —
 * SAILING_PLAN C2. Every view builds into the ONE shared ToriDraw_Scene, so
 * the element pools are what separate them: after this call the builder
 * allocates its terrain/scenery in the view's static pool, frees only that
 * pool on a rebuild, and sweeps only the view's dynamic pool for orphaned
 * entity elements. WorldBuilder_New leaves a builder on WORLDVIEW_ROOT's pair
 * (the historic STATIC/DYNAMIC), which is why nothing single-world changes.
 *
 * Call it before the first rebuild: elements already placed keep the pool they
 * were allocated in, and the old pool's clear would no longer reach them.
 */
void
WorldBuilder_SetSceneView(
    struct WorldBuilder* builder,
    int view_id);

/*
 * TORIRS_REBUILD_TIMING=1 -- the rebuild's own wall clock, shared with the
 * load task that wraps it (task_world_load.c) so one env var times both the
 * asset span and the synchronous rebuild it ends in.
 */
int
WorldBuilder_TimingOn(void);

double
WorldBuilder_TimingNowMs(void);

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
 *     r=0  (dx, dz)        r=1  (7-dz, dx)
 *     r=2  (7-dx, 7-dz)    r=3  (dz, 7-dx)
 *
 * r is quarter-turns clockwise, the reference's sense: its instanced loader
 * puts source (x, z) at (z, 7-x) for r=1, and a loc's angle gains +r in the
 * same direction. The two must turn the same way or a turned zone's walls face
 * out of it.
 *
 * This is `ToriRSServer_MapInstanceRotateToSrc`'s twin, and the two must agree or
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
        *out_sx = 7 - dz;
        *out_sz = dx;
        break;
    case 2:
        *out_sx = 7 - dx;
        *out_sz = 7 - dz;
        break;
    case 3:
        *out_sx = dz;
        *out_sz = 7 - dx;
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
        *out_dx = sz;
        *out_dz = 7 - sx - (size_x - 1);
        break;
    case 2:
        *out_dx = 7 - sx - (size_x - 1);
        *out_dz = 7 - sz - (size_z - 1);
        break;
    case 3:
        *out_dx = 7 - sz - (size_z - 1);
        *out_dz = sx;
        break;
    default:
        *out_dx = sx;
        *out_dz = sz;
        break;
    }
}

#endif
