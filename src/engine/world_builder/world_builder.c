#include "world_builder.h"

#include "blendmap.h"
#include "collision_map.h"
#include "contour_ground_queue.u.c"
#include "decor_buildmap.h"
#include "engine/cache_provider.h"
#include "engine/torirs_model_inst_cache.h"
#include "painters/painters.h"
#include "painters/scene_occluders.h"
#include "flag_map.h"
#include "heightmap.h"
#include "lightmap.h"
#include "minimap.h"
#include "occluder_buildmap.h"
#include "overlaymap.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "terrain_shapemap.h"
#include "toridraw_scene.h"
#include <rscache.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* TORIRS_REBUILD_TIMING=1: per-phase wall-clock of a scene rebuild on stderr.
 * The helpers live above the .u.c includes so the scenery pass can charge its
 * model-build time to the same accumulators. */
static double
wb_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int
wb_timing_on(void)
{
    static int v = -1;
    if( v < 0 )
    {
        const char* e = getenv("TORIRS_REBUILD_TIMING");
        v = (e && *e && *e != '0') ? 1 : 0;
    }
    return v;
}

/* Scenery model-build accumulators (reset in Begin, reported at End). */
static double g_wb_t_model_convert_ms; /* ModelFromToriRS + merge */
static double g_wb_t_model_transform_ms; /* apply_transforms + SD strip + bounds */
static int g_wb_n_model_builds;
static int g_wb_n_model_srcs;

/* Cached env-flag probes for the per-model / per-tile debug hooks below.
 * getenv() in those loops was the single hottest symbol of a whole rebuild
 * (__findenv_locked walks the environment list on every call — ~8k models and
 * ~43k tiles per scene paid it each time). The debug knobs stay usable; they
 * are simply read once per process like TORIRS_ZBUFFER_LOCS already was. */
static int
wb_env_on(
    const char* name,
    int* cache)
{
    if( *cache < 0 )
        *cache = getenv(name) != NULL;
    return *cache;
}

static int g_wb_env_scenery_dbg = -1;
static int g_wb_env_strip_tex = -1;

#define WB_ENV_SCENERY_DEBUG() wb_env_on("TORIRS_SCENERY_DEBUG", &g_wb_env_scenery_dbg)
#define WB_ENV_STRIP_TEXTURES() wb_env_on("TORIRS_STRIP_TEXTURES", &g_wb_env_strip_tex)

// clang-format off
#include "world_terrain.u.c"
#include "world_collision.u.c"
#include "world_scenery.u.c"
#include "world_sharelight.u.c"
// clang-format on

static void
world_builder_free_transient_maps(struct WorldBuilder* builder)
{
    assert(builder);
    if( builder->blendmap )
        blendmap_free(builder->blendmap);
    if( builder->overlaymap )
        overlaymap_free(builder->overlaymap);
    if( builder->terrain_shapemap )
        terrain_shape_map_free(builder->terrain_shapemap);
    if( builder->decor_buildmap )
        decor_buildmap_free(builder->decor_buildmap);
    if( builder->lightmap )
        lightmap_free(builder->lightmap);
    if( builder->sharelight_map )
        sharelight_map_free(builder->sharelight_map);
    if( builder->shademap )
        shademap2_free(builder->shademap);
    if( builder->flag_map )
        flag_map_free(builder->flag_map);
    if( builder->occluder_buildmap )
        occluder_buildmap_free(builder->occluder_buildmap);
    contour_ground_q_free(&builder->contour_ground_queue);
    builder->blendmap = NULL;
    builder->overlaymap = NULL;
    builder->terrain_shapemap = NULL;
    builder->decor_buildmap = NULL;
    builder->lightmap = NULL;
    builder->sharelight_map = NULL;
    builder->shademap = NULL;
    builder->flag_map = NULL;
    builder->occluder_buildmap = NULL;
}

/* After ClearPool(STATIC), only DYNAMIC elements should remain. Orphans —
 * DYNAMIC elements whose world entity was released without a drained
 * EntityRemoved — survive ClearPool, stay off the free list, and force the
 * high-water mark up on every denser rebuild until the uint16 entity id
 * space is exhausted. Reclaim them before the new static build allocates. */
static void
world_builder_mark_element_keep(
    uint8_t* keep,
    int element_id)
{
    if( element_id >= 0 && element_id < TORIDRAW_SCENE_MAX_ELEMENTS )
        keep[element_id] = 1;
}

/*
 * Claim `element_id` for one entity, and break the claim if somebody already
 * holds it.
 *
 * A scene element has exactly one owner. Element ids are RECYCLED, and three
 * subsystems act on the owner's id every frame: the model
 * (AppEntitySpotanim), the deferred animation bind (AppSeqBindPending) and the
 * POSITION, which is written straight from the owning entity. So two entities
 * holding one id do not merely confuse a lookup -- they fight over the element,
 * and the loser is dragged around wearing the winner's model. That is what
 * "I called my familiar and the Queen's model followed me, and her head moved
 * with me" is: one element, two claimants.
 *
 * Resolved in favour of the FIRST entity seen, because it is the one the
 * element's current contents were built for; the later claimant is reset to
 * "no element", which every consumer already treats as "draw and update
 * nothing" rather than as a wrong target. An entity pointing at an id that is
 * not live is cleared for the same reason -- it can only become a false claim
 * the moment that id is handed out again.
 *
 * Deliberately a repair rather than an assert: this runs on the map rebuild,
 * which is when the client can least afford to die, and a silently wrong owner
 * is far worse than one entity missing its model for a frame. It reports every
 * repair, because a repair here means some producer let the two references
 * diverge and that producer is still the real defect.
 */
static void
world_builder_claim_element(
    struct ToriDraw_Scene* scene,
    int* claimed_by,
    uint8_t* keep,
    int* element_id_field,
    int owner_tag)
{
    int const id = *element_id_field;

    if( id < 0 )
        return;
    if( id >= TORIDRAW_SCENE_MAX_ELEMENTS || !ToriDraw_SceneElementIsLive(scene, id) )
    {
        fprintf(
            stderr, "world_builder: entity %#x referenced dead element %d - cleared\n",
            owner_tag, id);
        *element_id_field = -1;
        return;
    }
    if( claimed_by[id] >= 0 )
    {
        fprintf(
            stderr,
            "world_builder: element %d claimed by entities %#x and %#x - the second is "
            "cleared (they would fight over its model, animation and position)\n",
            id, claimed_by[id], owner_tag);
        *element_id_field = -1;
        return;
    }
    claimed_by[id] = owner_tag;
    world_builder_mark_element_keep(keep, id);
}

static void
world_builder_reconcile_dynamic_elements(struct WorldBuilder* builder)
{
    struct World* world;
    struct ToriDraw_Scene* scene;
    uint8_t* keep;
    int* claimed_by;
    struct World_EntityPool* pool;
    int id;
    int next;

    assert(builder);
    world = builder->world;
    scene = builder->scene;
    assert(world && scene);

    keep = (uint8_t*)calloc((size_t)TORIDRAW_SCENE_MAX_ELEMENTS, 1);
    assert(keep && "world_builder_reconcile_dynamic_elements: keep bitmap");
    claimed_by = (int*)malloc((size_t)TORIDRAW_SCENE_MAX_ELEMENTS * sizeof(int));
    assert(claimed_by && "world_builder_reconcile_dynamic_elements: claim map");
    for( int ci = 0; ci < TORIDRAW_SCENE_MAX_ELEMENTS; ci++ )
        claimed_by[ci] = -1;

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* p = World_EntityPoolGet(pool, i);
        if( p )
            world_builder_claim_element(scene, claimed_by, keep, &p->element_id, 0x10000 | i);
    }
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* n = World_EntityPoolGet(pool, i);
        if( n )
            world_builder_claim_element(scene, claimed_by, keep, &n->element_id, 0x20000 | i);
    }
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* s = World_EntityPoolGet(pool, i);
        if( s )
            world_builder_claim_element(scene, claimed_by, keep, &s->element_id, 0x30000 | i);
    }
    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( p )
            world_builder_claim_element(scene, claimed_by, keep, &p->element_id, 0x40000 | i);
    }
    pool = &world->entities.spotanim;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, i);
        if( s )
            world_builder_claim_element(scene, claimed_by, keep, &s->element_id, 0x50000 | i);
    }

    for( id = scene->elements.head; id != TORIDRAW_INTRUSIVE_NIL; id = next )
    {
        struct ToriDraw_SceneElement* el;

        next = scene->elements.nodes[id].next;
        el = ToriDraw_SceneElementGet(scene, id);
        if( !el || el->pool != (uint8_t)builder->dynamic_pool )
            continue;
        if( !keep[id] )
            ToriDraw_SceneElementRemove(scene, id);
    }
    free(claimed_by);
    free(keep);
}

struct WorldBuilder*
WorldBuilder_New(
    struct World* world,
    struct CacheProvider* cache,
    struct ToriDraw_Scene* scene,
    struct VarPManager* varp)
{
    struct WorldBuilder* builder = calloc(1, sizeof(struct WorldBuilder));
    assert(builder && "Failed to allocate world builder");
    builder->world = world;
    builder->cache = cache;
    builder->scene = scene;
    builder->varp = varp;
    /* The root view's pair until told otherwise (WorldBuilder_SetSceneView) —
     * the pools a single-world client has always used. */
    builder->static_pool = TORIDRAW_SCENE_POOL_STATIC;
    builder->dynamic_pool = TORIDRAW_SCENE_POOL_DYNAMIC;
    /* calloc leaves the debug ring at 0, which is a valid element id. */
    for( int i = 0; i < (int)(sizeof(builder->scenery_dbg_element) /
                              sizeof(builder->scenery_dbg_element[0]));
         i++ )
        builder->scenery_dbg_element[i] = -1;
    builder->scenery_model_cache = calloc(1, sizeof(*builder->scenery_model_cache));
    assert(builder->scenery_model_cache);
    {
        bool inited = TorirsModelInstCache_Init(builder->scenery_model_cache);
        assert(inited && "WorldBuilder_New: scenery model cache init");
        (void)inited;
    }
    return builder;
}

void
WorldBuilder_Free(struct WorldBuilder* builder)
{
    if( !builder )
        return;
    if( builder->scenery_model_cache )
    {
        TorirsModelInstCache_Free(builder->scenery_model_cache);
        free(builder->scenery_model_cache);
    }
    world_builder_free_transient_maps(builder);
    free(builder);
}

void
WorldBuilder_SetSceneView(
    struct WorldBuilder* builder,
    int view_id)
{
    assert(builder);
    assert(view_id >= 0);
    assert(view_id < TORIDRAW_SCENE_POOL_VIEW_MAX);
    builder->static_pool = TORIDRAW_SCENE_POOL_STATIC_VIEW(view_id);
    builder->dynamic_pool = TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(view_id);
}

/*
 * Batching is a retained-arena upload for view 0's static geometry, and only
 * view 0's clear drops that arena (ToriDraw_SceneClearPool). A boat deck lives
 * in its own pool and is unloaded element by element, so batching it would
 * leave its geometry in the arena after the elements are gone — stale hulls
 * that only a mainland rebuild could clear. Off the batch path, a deck's
 * elements emit MODEL_LOAD and draw through the per-element route entities
 * already use, which is what a few hundred tiles wants anyway.
 */
static void
world_builder_batch_begin(struct WorldBuilder* builder)
{
    assert(builder);
    if( builder->static_pool != TORIDRAW_SCENE_POOL_STATIC )
        return;
    ToriDraw_SceneBatchBegin(builder->scene);
}

static void
world_builder_batch_end(struct WorldBuilder* builder)
{
    assert(builder);
    if( builder->static_pool != TORIDRAW_SCENE_POOL_STATIC )
        return;
    ToriDraw_SceneBatchEnd(builder->scene);
}

void
WorldBuilder_RebuildCenterzoneBegin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    struct World* world = builder->world;
    assert(world && "WorldBuilder_RebuildCenterzoneBegin: world is NULL");

    g_wb_t_model_convert_ms = 0.0;
    g_wb_t_model_transform_ms = 0.0;
    g_wb_n_model_builds = 0;
    g_wb_n_model_srcs = 0;

    /* Loc configs may have been reloaded (varbit morphs re-resolve per place;
     * the map editor re-seeds the provider) — a prototype baked from the old
     * config must not survive into this build. */
    TorirsModelInstCache_Clear(builder->scenery_model_cache);

    world_builder_free_transient_maps(builder);
    World_ResetScene(world, zone_center_x, zone_center_z, scene_size);

    /* Static pool only: entity elements (players/npcs/objs) keep their ids
     * across a rebuild — the REBUILD_NORMAL shift relocates them instead. */
    ToriDraw_SceneClearPool(builder->scene, builder->static_pool);
    world_builder_reconcile_dynamic_elements(builder);

    builder->blendmap = blendmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->overlaymap = overlaymap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->terrain_shapemap =
        terrain_shape_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->decor_buildmap = decor_buildmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->lightmap = lightmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->sharelight_map = sharelight_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->shademap = shademap2_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->flag_map = flag_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    /* +1 so wall marks at [x+1]/[z+1] stay in range (Client-TS mapo is
     * maxTileX+1 × maxTileZ+1). */
    builder->occluder_buildmap =
        occluder_buildmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
}

static inline bool
scene_in_bounds(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z)
{
    int scene_size = builder->world->_scene_size;
    return scene_x >= 0 && scene_x < scene_size && scene_z >= 0 && scene_z < scene_size;
}

void
WorldBuilder_RebuildCenterzoneChunkScenery(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;
    int map_id = CacheProvider_MapId(mapx, mapz);
    struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(builder->cache, map_id);
    /* Absent square (void/unreleased, or xtea-locked): the loader warned and
     * skipped it, and the terrain pass leaves it flat void. Skip scenery too
     * rather than aborting — an empty square renders as nothing, like the
     * reference client. */
    if( !map_locs )
        return;

    world_builder_minimap_add_chunk_walls(builder, mapx, mapz);
    world_builder_minimap_add_chunk_mapfunctions(builder, mapx, mapz);

    /* Counters for TORIRS_SCENERY_DEBUG. Each `continue` below silently drops a
     * loc instance, so a square that renders bare terrain looks identical whether
     * the configs are missing, the morph resolved to nothing, or every instance
     * landed out of bounds. Count them apart. */
    int dbg_total = 0;
    int dbg_no_config = 0;
    int dbg_no_resolve = 0;
    int dbg_oob = 0;
    int dbg_added = 0;
    int dbg_level[8] = { 0 };
    int dbg_shape[32] = { 0 };
    g_scenery_dbg_elements = 0;

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct ToriRS_MapLoc* map_loc = &map_locs->locs[i];
        struct ToriRS_Location* config_loc = CacheProvider_LocationGet(builder->cache, map_loc->loc_id);
        dbg_total++;
        if( !config_loc )
        {
            dbg_no_config++;
            continue;
        }

        struct ToriRS_Location resolved_loc;
        config_loc = world_builder_resolve_loc_for_place(builder, config_loc, &resolved_loc);
        if( !config_loc )
        {
            dbg_no_resolve++;
            continue;
        }

        int scene_x = World_ToSceneX(world, mapx, map_loc->chunk_pos_x);
        int scene_z = World_ToSceneZ(world, mapz, map_loc->chunk_pos_z);

        if( !scene_in_bounds(builder, scene_x, scene_z) )
        {
            dbg_oob++;
            continue;
        }
        dbg_added++;
        if( map_loc->chunk_pos_level >= 0 && map_loc->chunk_pos_level < 8 )
            dbg_level[map_loc->chunk_pos_level]++;
        if( map_loc->shape_select >= 0 && map_loc->shape_select < 32 )
            dbg_shape[map_loc->shape_select]++;

        builder->scenery_mapx = mapx;
        builder->scenery_mapz = mapz;
        builder->scenery_base_loc_id = map_loc->loc_id;

        world_collision_add_loc(builder, map_loc, config_loc, scene_x, scene_z);
        scenery_add(builder, map_loc, config_loc, scene_x, scene_z);
    }

    if( WB_ENV_SCENERY_DEBUG() )
        fprintf(
            stderr,
            "scenery: map=%d,%d instances=%d no_config=%d no_resolve=%d oob=%d added=%d "
            "scene_elements=%d\n",
            mapx,
            mapz,
            dbg_total,
            dbg_no_config,
            dbg_no_resolve,
            dbg_oob,
            dbg_added,
            g_scenery_dbg_elements);

    if( WB_ENV_SCENERY_DEBUG() )
    {
        fprintf(stderr, "  levels:");
        for( int lv = 0; lv < 4; lv++ )
            fprintf(stderr, " %d:%d", lv, dbg_level[lv]);
        fprintf(stderr, "\n  shapes:");
        for( int sh = 0; sh < 32; sh++ )
            if( dbg_shape[sh] )
                fprintf(stderr, " %d:%d", sh, dbg_shape[sh]);
        fprintf(stderr, "\n");
    }
}

/*
 * One 8x8 zone of an instanced scene's scenery.
 *
 * Walks the *source*'s locs and asks where each goes — the opposite direction
 * from the terrain pass, because a loc is a sparse list entry rather than a grid
 * cell. Three things change per loc and nothing else does: the tile it lands on,
 * its angle (turned by the same quarter-turns), and its level (the destination
 * plane, which need not be the source's).
 *
 * The map-loc is copied rather than mutated. The cache hands out one shared
 * ToriRS_MapLocs per square, and a house can point three zones at that same
 * square with three different rotations — writing the turned angle back would
 * corrupt the second and third.
 *
 * The minimap wall/mapfunction passes the square path runs are still not called
 * here: both take a whole map square and paint it at its own scene offset, which
 * for a copied zone is the wrong place. What IS called is the per-loc half,
 * `world_builder_minimap_add_loc`, from inside the loop below where the copied
 * zone's real scene position and rotated angle are already in hand. Before that
 * split an instanced minimap had terrain colours and nothing else — no wall
 * outlines, no mapscene icons — which is what the Inferno's arena looked like.
 */
void
WorldBuilder_RebuildInstanceZoneScenery(
    struct WorldBuilder* builder,
    int dst_zone_x,
    int dst_zone_z,
    int dst_level,
    int src_zone_x,
    int src_zone_z,
    int src_level,
    int rotation)
{
    int mapx = src_zone_x >> 3;
    int mapz = src_zone_z >> 3;
    int map_id = CacheProvider_MapId(mapx, mapz);
    struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(builder->cache, map_id);
    int src_tile_x = (src_zone_x & 7) * 8;
    int src_tile_z = (src_zone_z & 7) * 8;

    if( !map_locs )
        return;

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct ToriRS_MapLoc placed = map_locs->locs[i];
        struct ToriRS_Location* config_loc;
        struct ToriRS_Location resolved_loc;
        int sx = placed.chunk_pos_x - src_tile_x;
        int sz = placed.chunk_pos_z - src_tile_z;
        int size_x;
        int size_z;
        int dx;
        int dz;
        int scene_x;
        int scene_z;

        if( placed.chunk_pos_level != src_level )
            continue;
        if( sx < 0 || sx > 7 || sz < 0 || sz > 7 )
            continue;

        config_loc = CacheProvider_LocationGet(builder->cache, placed.loc_id);
        if( !config_loc )
            continue;
        config_loc = world_builder_resolve_loc_for_place(builder, config_loc, &resolved_loc);
        if( !config_loc )
            continue;

        /* Footprint as placed in the source, which is what the corner correction
         * needs — an odd angle has already swapped the config's own extents. */
        size_x = config_loc->size_x > 0 ? config_loc->size_x : 1;
        size_z = config_loc->size_z > 0 ? config_loc->size_z : 1;
        if( (placed.orientation & 1) != 0 )
        {
            int tmp = size_x;
            size_x = size_z;
            size_z = tmp;
        }

        world_instance_rotate_to_dst(rotation, sx, sz, size_x, size_z, &dx, &dz);
        scene_x = dst_zone_x * 8 + dx;
        scene_z = dst_zone_z * 8 + dz;
        if( !scene_in_bounds(builder, scene_x, scene_z) )
            continue;

        placed.orientation = (placed.orientation + rotation) & 3;
        placed.chunk_pos_level = dst_level;

        builder->scenery_mapx = mapx;
        builder->scenery_mapz = mapz;
        builder->scenery_base_loc_id = placed.loc_id;

        world_collision_add_loc(builder, &placed, config_loc, scene_x, scene_z);
        scenery_add(builder, &placed, config_loc, scene_x, scene_z);
        /* `placed` already carries the rotated angle and the destination level,
         * and `scene_x/scene_z` is the copied zone's real position — everything
         * the minimap registration needs, which is why it belongs here and not
         * in the square-offset walkers the ordinary path uses. */
        world_builder_minimap_add_loc(builder, &placed, config_loc, scene_x, scene_z);
        world_builder_minimap_add_loc_mapfunction(builder, &placed, config_loc, scene_x, scene_z);
        world_builder_add_loc_area_sound(builder, &placed, config_loc, scene_x, scene_z);
    }
}

/*
 * The instanced rebuild, in the same three movements as the ordinary one:
 * everything's terrain, then everything's scenery, then End.
 *
 * The two passes cannot be interleaved per zone even though it would read better:
 * a loc's placement samples the heightmap under its whole footprint, and a
 * footprint can cross into the next zone. Terrain first for the whole scene is
 * what makes a table at a zone boundary sit on the floor.
 */
void
WorldBuilder_RebuildInstance(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones)
{
    int zone_count = scene_size / 8;
    double t0 = wb_timing_on() ? wb_now_ms() : 0.0;

    assert(zones && "WorldBuilder_RebuildInstance: no descriptor grid");
    assert(zone_count <= WORLD_INSTANCE_ZONES);

    WorldBuilder_RebuildCenterzoneBegin(builder, zone_center_x, zone_center_z, scene_size);

    world_builder_batch_begin(builder);

    for( int pass = 0; pass < 2; pass++ )
    {
        for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
        {
            for( int zx = 0; zx < zone_count; zx++ )
            {
                for( int zz = 0; zz < zone_count; zz++ )
                {
                    int32_t v =
                        zones[level * WORLD_INSTANCE_ZONES * WORLD_INSTANCE_ZONES +
                              zx * WORLD_INSTANCE_ZONES + zz];
                    int rotation;
                    int src_zone_z;
                    int src_zone_x;
                    int src_level;

                    if( v == 0 )
                        continue;
                    rotation = (v >> 1) & 0x3;
                    src_zone_z = (v >> 3) & 0x7ff;
                    src_zone_x = (v >> 14) & 0x3ff;
                    src_level = (v >> 24) & 0x3;

                    if( pass == 0 )
                        WorldBuilder_RebuildInstanceZoneTerrain(
                            builder, zx, zz, level, src_zone_x, src_zone_z, src_level,
                            rotation);
                    else
                        WorldBuilder_RebuildInstanceZoneScenery(
                            builder, zx, zz, level, src_zone_x, src_zone_z, src_level,
                            rotation);
                }
            }
        }
    }

    WorldBuilder_RebuildCenterzoneEnd(builder);

    world_builder_batch_end(builder);

    if( wb_timing_on() )
        fprintf(stderr, "rebuild_timing: instance total=%.1fms\n", wb_now_ms() - t0);
}

/* Minimap sibling to the geometry push-down in RebuildCenterzoneEnd: for each
 * LinkBelow bridge column, shift the baked minimap tiles down a plane so the
 * deck (cache level 1) lands at paint level 0. Mirrors World.pushDown; the
 * land-settings (world->tile_flags) stay raw so the bake's VisBelow composite is
 * unchanged.
 *
 * MUST run AFTER world_build_scene_terrain has set the per-level minimap colours
 * (and after the scenery pass has set minimap walls) — otherwise it shuffles an
 * empty minimap and the deck colour, set later at cache level 1, never reaches
 * paint level 0, leaving the bridge showing the water underneath. Reads the
 * already-persisted world->tile_flags because builder->flag_map is freed earlier
 * in RebuildCenterzoneEnd. */
static void
world_builder_pushdown_minimap(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;
    int plane = scene_size * scene_size;

    if( !world->minimap || !world->tile_flags )
        return;

    for( int x = 0; x < scene_size; x++ )
        for( int z = 0; z < scene_size; z++ )
        {
            /* LinkBelow lives at cache level 1 (index += 1*plane). */
            int idx1 = (x + z * scene_size) + plane;
            if( (world->tile_flags[idx1] & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
                minimap_push_down_tiles(world->minimap, x, z);
        }
}

void
WorldBuilder_RebuildCenterzoneEnd(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;
    double te0 = wb_timing_on() ? wb_now_ms() : 0.0;

    /* Terrain first (place-time LinkBelow shift for BLOCK flags), then End's
     * no-op bridge hook — loc collision already shifted at place time. Geometry
     * push-down below is separate. See docs/COLLISION_MAP.md. */
    world_collision_apply_terrain(builder);
    world_collision_apply_bridges(builder);
    double te_collision = wb_timing_on() ? wb_now_ms() : 0.0;
    world_contour_ground(builder);
    double te_contour = wb_timing_on() ? wb_now_ms() : 0.0;
    world_builder_apply_wall_decor_offsets(builder);

    /* Bridge decks are LinkBelow: geometry / painter push-down below move a
     * bridge column from cache level 1 to paint level 0. Collision already
     * used place-time level shift when locs/terrain were stamped. Mapfunction
     * icons still carry their raw cache level, so pull each
     * icon on a LinkBelow column to its paint level before the spread — otherwise
     * the spread samples the wrong (pre-push-down) collision map and, at draw
     * time, the icon's level never matches the player's level (app.c minimap icon
     * loop) so bridge icons vanish. Same remap the painter uses: 1→0, 2→1, 3→2,
     * 0→3. */
    if( builder->flag_map )
    {
        for( int i = 0; i < world->mapfunc_count; i++ )
        {
            struct World_MapFunctionIcon* icon = &world->mapfuncs[i];
            if( (flag_map_get(builder->flag_map, icon->x, icon->z, 1) &
                 RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                continue;
            icon->level =
                (icon->level == 0) ? WORLD_MAP_TERRAIN_LEVELS - 1 : icon->level - 1;
        }

        /* Mapscene icons are baked per level too, so a bridge-deck mapscene must
         * follow the same 1→0 push-down as the tiles (minimap_push_down_tiles)
         * and the mapfunction icons above — otherwise it bakes at cache level 1
         * and never appears on the player's level-0 map. */
        for( int i = 0; i < world->mapscene_count; i++ )
        {
            struct World_MapSceneIcon* icon = &world->mapscenes[i];
            if( (flag_map_get(builder->flag_map, icon->x, icon->z, 1) &
                 RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                continue;
            icon->level =
                (icon->level == 0) ? WORLD_MAP_TERRAIN_LEVELS - 1 : icon->level - 1;
        }
    }

    world_builder_minimap_spread_mapfunctions(builder);

    if( builder->decor_buildmap )
    {
        decor_buildmap_free(builder->decor_buildmap);
        builder->decor_buildmap = NULL;
    }

    if( world->painter && builder->flag_map )
    {
        struct PaintersTile bridge_tile_tmp = { 0 };
        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int bridge_flags = flag_map_get(builder->flag_map, x, z, 1);
                if( (bridge_flags & RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                    continue;

                bridge_tile_tmp = *painter_tile_at(world->painter, x, z, 0);
                for( int level = 0; level < painter_max_levels(world->painter) - 1; level++ )
                    painter_tile_copyto(world->painter, x, z, level + 1, x, z, level);

                *painter_tile_at(world->painter, x, z, 3) = bridge_tile_tmp;
                painters_tile_set_paintgrid_level(painter_tile_at(world->painter, x, z, 3), 3);
                painter_tile_set_bridge(world->painter, x, z, 0, x, z, 3);
            }
        }

        /*
         * VIS_BELOW lowers a tile's DRAW LEVEL; it does not move geometry.
         *
         * Reference (rev-239 class112): buildScene calls method4195 with
         * Statics.method8418's result, which sets tile flag 0x40; method4161
         * (renderLevel) then answers 0 for such a tile, and the mark pass
         * (method4241: `renderLevel <= field1653`) is the only place the level
         * cull happens. Client-TS is the same shape: setLayer(level, x, z,
         * getVisBelowLevel(...)) stores a cull level on the Square, and
         * draw() gates on `tile.drawLevel <= maxLevel`. In both, the mesh
         * stays on its own plane and pops in its own traversal slot — after
         * the tile below fully retires, so the floor above paints over the
         * walls below it.
         *
         * An earlier session relocated the flagged mesh into the lower
         * level's terrain set instead ("hand the mesh down"). That drew the
         * borrowed floor before the lower tile's walls — the reverse of the
         * reference order — and its motivation (surviving the roof-hide
         * level mask) was already covered: tile_excluded_by_bridge_or_draw_mask
         * tests visible_gte_level, and bit 0 of the mask is always set.
         */
        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int link_l1 = (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0;
                for( int g = 0; g < painter_max_levels(world->painter); g++ )
                {
                    int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
                    uint8_t st = (uint8_t)flag_map_get(builder->flag_map, x, z, src);
                    int draw = RSCache_MapFloorVisBelowDrawLevel(st, src, link_l1);
                    painter_tile_set_draw_level(world->painter, x, z, g, draw);
                }
            }
        }
    }

    /* Persist the raw settings bytes (reference mapl) before the flag map
     * dies — the per-frame roof check (Client-TS roofCheck) needs
     * REMOVE_ROOF/LINK_BELOW at play time, not just at build time. */
    if( builder->flag_map && world->tile_flags )
    {
        for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
            for( int x = 0; x < scene_size; x++ )
                for( int z = 0; z < scene_size; z++ )
                    world->tile_flags
                        [x + z * scene_size + level * scene_size * scene_size] =
                        (uint8_t)flag_map_get(builder->flag_map, x, z, level);
    }

    if( builder->flag_map )
    {
        flag_map_free(builder->flag_map);
        builder->flag_map = NULL;
    }

    if( world->painter )
        painter_mark_static_count(world->painter);

    double te_mid = wb_timing_on() ? wb_now_ms() : 0.0;
    world_build_scene_terrain(builder);
    double te_terrain_mesh = wb_timing_on() ? wb_now_ms() : 0.0;
    world_build_lighting(builder);
    double te_lighting = wb_timing_on() ? wb_now_ms() : 0.0;

    /* A level with no terrain mesh emits nothing. The reference never queues a
     * content-less tile at all (class112.method3940 gates marking on the
     * has-content bit), whereas leaving the default set here emits a terrain
     * command for all four levels of every column and lets the frame drop the
     * dead ones at World_TerrainElementAt() < 0 — the 4x speculative emits of
     * ORANGE_WEDGE.md §9.7(c). Runs after world_build_scene_terrain, the first
     * point the mesh census exists. */
    if( world->painter )
    {
        for( int x = 0; x < scene_size; x++ )
            for( int z = 0; z < scene_size; z++ )
                for( int g = 0; g < painter_max_levels(world->painter); g++ )
                {
                    unsigned set = painter_tile_get_terrain_levels(world->painter, x, z, g);
                    unsigned kept = 0;
                    for( int ml = 0; ml < WORLD_MAP_TERRAIN_LEVELS; ml++ )
                        if( (set & (1u << ml)) != 0 &&
                            World_TerrainElementAt(world, x, z, ml) >= 0 )
                            kept |= 1u << ml;
                    if( kept != set )
                        painter_tile_set_terrain_levels(world->painter, x, z, g, kept);
                }
    }

    /* Bridge minimap push-down runs HERE, after world_build_scene_terrain has
     * set the per-level minimap colours — shuffling the planes any earlier moves
     * an empty minimap and leaves bridge decks showing the water below (reads the
     * persisted world->tile_flags; builder->flag_map is already freed above). */
    world_builder_pushdown_minimap(builder);

    /* Planar occluders: greedy-merge the mapo marks (walls/roofs marked during
     * scenery, flat floors marked during terrain) into SceneOccluder planes and
     * install them on the painter. Reference order is pushDown then merge; our
     * bridge push-down of painter tiles does not touch the mark bitfield. */
    if( builder->occluder_buildmap && world->painter )
    {
        struct SceneOccluders* occ = painter_get_occluders(world->painter);
        if( !occ || occ->width != world->_scene_size || occ->height != world->_scene_size ||
            occ->levels != WORLD_MAP_TERRAIN_LEVELS )
        {
            occ = scene_occluders_new(
                world->_scene_size, world->_scene_size, WORLD_MAP_TERRAIN_LEVELS);
            painter_set_occluders(world->painter, occ);
        }
        occluder_buildmap_build_occluders(
            builder->occluder_buildmap, world->heightmap, occ);
        occluder_buildmap_free(builder->occluder_buildmap);
        builder->occluder_buildmap = NULL;
    }
    else if( builder->occluder_buildmap )
    {
        occluder_buildmap_free(builder->occluder_buildmap);
        builder->occluder_buildmap = NULL;
    }

    if( builder->sharelight_map )
    {
        sharelight_map_free(builder->sharelight_map);
        builder->sharelight_map = NULL;
    }
    if( builder->shademap )
    {
        shademap2_free(builder->shademap);
        builder->shademap = NULL;
    }
    if( builder->blendmap )
    {
        blendmap_free(builder->blendmap);
        builder->blendmap = NULL;
    }
    if( builder->overlaymap )
    {
        overlaymap_free(builder->overlaymap);
        builder->overlaymap = NULL;
    }
    if( builder->terrain_shapemap )
    {
        terrain_shape_map_free(builder->terrain_shapemap);
        builder->terrain_shapemap = NULL;
    }
    if( builder->lightmap )
    {
        lightmap_free(builder->lightmap);
        builder->lightmap = NULL;
    }

    /* The prototype cache's whole value is within the build that just ran —
     * every instance of a repeated loc after the first copies its lit model.
     * Dropping it here keeps zero prototypes resident during play (a scene's
     * worth is several MB); a runtime loc spawn simply rebuilds its one model,
     * which is what it always did. */
    TorirsModelInstCache_Clear(builder->scenery_model_cache);

    if( wb_timing_on() )
    {
        double te1 = wb_now_ms();
        fprintf(
            stderr,
            "rebuild_timing: end=%.1fms collision=%.1f contour=%.1f minimap_bridge=%.1f "
            "terrain_mesh=%.1f lighting=%.1f occluders_free=%.1f | scenery models: n=%d "
            "srcs=%d convert=%.1fms transform=%.1fms\n",
            te1 - te0,
            te_collision - te0,
            te_contour - te_collision,
            te_mid - te_contour,
            te_terrain_mesh - te_mid,
            te_lighting - te_terrain_mesh,
            te1 - te_lighting,
            g_wb_n_model_builds,
            g_wb_n_model_srcs,
            g_wb_t_model_convert_ms,
            g_wb_t_model_transform_ms);
    }

    world->load_complete = true;
}

void
WorldBuilder_RebuildCenterzoneChunk(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    WorldBuilder_RebuildCenterzoneChunkTerrain(builder, mapx, mapz);
    WorldBuilder_RebuildCenterzoneChunkScenery(builder, mapx, mapz);
}

void
WorldBuilder_RebuildCenterzone(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    double t0 = wb_timing_on() ? wb_now_ms() : 0.0;

    WorldBuilder_RebuildCenterzoneBegin(builder, zone_center_x, zone_center_z, scene_size);

    double t_begin = wb_timing_on() ? wb_now_ms() : 0.0;

    world_builder_batch_begin(builder);

    struct World* world = builder->world;
    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            WorldBuilder_RebuildCenterzoneChunkTerrain(builder, mapx, mapz);
    }

    double t_terrain = wb_timing_on() ? wb_now_ms() : 0.0;

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            WorldBuilder_RebuildCenterzoneChunkScenery(builder, mapx, mapz);
    }

    double t_scenery = wb_timing_on() ? wb_now_ms() : 0.0;

    WorldBuilder_RebuildCenterzoneEnd(builder);

    double t_end = wb_timing_on() ? wb_now_ms() : 0.0;

    world_builder_batch_end(builder);

    if( wb_timing_on() )
    {
        double t_batch = wb_now_ms();
        fprintf(
            stderr,
            "rebuild_timing: total=%.1fms begin=%.1f terrain=%.1f scenery=%.1f end=%.1f "
            "batch_flush=%.1f\n",
            t_batch - t0,
            t_begin - t0,
            t_terrain - t_begin,
            t_scenery - t_terrain,
            t_end - t_scenery,
            t_batch - t_end);
    }
}

void
WorldBuilder_RebuildChunklistBegin(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count)
{
    struct World* world = builder->world;
    assert(world && "WorldBuilder_RebuildChunklistBegin: world is NULL");

    TorirsModelInstCache_Clear(builder->scenery_model_cache);

    world_builder_free_transient_maps(builder);
    World_ResetSceneChunkList(world, chunks_xz, count);

    ToriDraw_SceneClearPool(builder->scene, builder->static_pool);
    world_builder_reconcile_dynamic_elements(builder);

    int scene_size = world->_scene_size;
    builder->blendmap = blendmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->overlaymap = overlaymap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->terrain_shapemap =
        terrain_shape_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->decor_buildmap = decor_buildmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->lightmap = lightmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->sharelight_map = sharelight_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->shademap = shademap2_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->flag_map = flag_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    /* +1 so wall marks at [x+1]/[z+1] stay in range (Client-TS mapo is
     * maxTileX+1 × maxTileZ+1). */
    builder->occluder_buildmap =
        occluder_buildmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
}

void
WorldBuilder_RebuildChunklist(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count)
{
    WorldBuilder_RebuildChunklistBegin(builder, chunks_xz, count);

    world_builder_batch_begin(builder);

    for( int i = 0; i < count; i++ )
    {
        int mapx = chunks_xz[i * 2];
        int mapz = chunks_xz[i * 2 + 1];
        WorldBuilder_RebuildCenterzoneChunkTerrain(builder, mapx, mapz);
    }

    for( int i = 0; i < count; i++ )
    {
        int mapx = chunks_xz[i * 2];
        int mapz = chunks_xz[i * 2 + 1];
        WorldBuilder_RebuildCenterzoneChunkScenery(builder, mapx, mapz);
    }

    WorldBuilder_RebuildCenterzoneEnd(builder);

    world_builder_batch_end(builder);
}

/* Client-TS locChangeUnchecked (Client.ts:7733): a zone LOC_ADD_CHANGE/LOC_DEL
 * removes the loc in the target layer (scene + collision) and, for a real id,
 * spawns the replacement. torirs keeps every loc layer in one scenery pool, so
 * World_SceneryFindAt filters by the shape's layer; the removed scene element is
 * torn down via the entity-removed event (ToriDraw_SceneElementRemove), and the
 * new loc is flagged runtime_spawn so world_cycle re-registers it with the
 * painter each frame (the baked static set can't take late additions). */
void
WorldBuilder_ApplyLocChange(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    struct World* world = builder->world;
    int idx;

    assert(builder);

    /* 1. Remove the existing loc in this shape's layer (collision + scene).
     * Loop: an L-shaped wall (WALL_TWO_SIDES) and a double diagonal wall decor
     * register two pool entries (one per model half), and all halves must go.
     * The collision del runs once per entry but is an AND-NOT (idempotent). */
    while( (idx = World_SceneryFindAt(world, scene_x, scene_z, level, shape)) >= 0 )
    {
        struct WorldEntity_Scenery* old =
            World_EntityPoolGet(&world->entities.scenery, idx);
        if( old )
        {
            /* Resolve the same way the spawn below does, or the undo reads a
             * different loc than the one that was added: the pool stores the
             * BASE id, so a multiloc's collision/minimap footprint has to be
             * re-derived through the transform to match what was applied. */
            struct ToriRS_Location old_resolved;
            struct ToriRS_Location* old_cfg = world_builder_resolve_loc_for_place(
                builder, CacheProvider_LocationGet(builder->cache, old->loc_id), &old_resolved);
            if( old_cfg )
            {
                struct ToriRS_MapLoc old_ml = {
                    .loc_id = old->loc_id,
                    .shape_select = old->shape,
                    .orientation = old->angle,
                    .chunk_pos_x = scene_x,
                    .chunk_pos_z = scene_z,
                    .chunk_pos_level = level,
                };
                world_collision_del_loc(builder, &old_ml, old_cfg, scene_x, scene_z);

                /* Erase the old loc's minimap wall/door line (reference
                 * rebuilds the whole minimap buffer from the scene; torirs
                 * edits the baked tile bits and bumps minimap_seq so the app
                 * rebakes the map sprite). */
                if( world->minimap && old_cfg->map_scene_id == -1 )
                {
                    int mm_flags = scenery_minimap_wall_flags(
                        old->shape, old->angle, old_cfg->is_interactive);
                    if( mm_flags != 0 )
                    {
                        minimap_del_tile_wall(
                            world->minimap, scene_x, scene_z, level, mm_flags);
                        world->minimap_seq++;
                    }
                }
            }
            /* Both releases address the PAINT grid, so a bridge column has to
             * make the push-down trip first (World_LocPaintLevel): the deck's
             * baked slot was moved from cache level 1 to paint level 0 when the
             * scene was built, and releasing at the cache level would silently
             * find nothing there. */
            int paint_level = World_LocPaintLevel(world, scene_x, scene_z, level);
            /* A removed WALL loc must also release its exclusive painter tile
             * slot (wall_a/wall_b): the dead static element would otherwise
             * keep the slot claimed, blocking the replacement wall's per-frame
             * registration and leaving a stale reference. */
            if( old->shape >= RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE &&
                old->shape <= RSCACHE_LOC_SHAPE_WALL_RECT_CORNER && world->painter )
                painter_release_wall(
                    world->painter, scene_x, scene_z, paint_level, old->element_id);
            /* Same for a centrepiece/decoration: the baked static scenery element
             * has to leave its tile chains, or it keeps drawing whatever scene
             * element id it holds — and that id is handed straight back to the
             * replacement loc below, so the new model draws twice at two
             * different depths. See painter_release_scenery. */
            if( world->painter )
                painter_release_scenery(
                    world->painter, scene_x, scene_z, paint_level, old->element_id);
        }
        World_SceneryRemove(world, idx);
    }

    /*
     * 1b. Drop the old loc's ambient emitter, if it had one.
     *
     * The emitter list is otherwise only ever built by a full scene walk, so a
     * loc that stops existing keeps sounding until the player walks far enough
     * to force a rebuild -- a switched-off machine that hums until you leave the
     * room. Keyed on the tile rather than the loc id because the id here is the
     * base and the emitter was registered against the resolved multiloc.
     */
    World_RemoveAreaSoundAt(world, scene_x, scene_z, level);

    /* 2. Spawn the replacement loc (LOC_DEL passes loc_id < 0 and stops here). */
    if( loc_id >= 0 )
    {
        /* Resolve the multiloc transform, exactly as the static build does. A
         * zone LOC packet names the BASE id, and a multiloc base commonly
         * carries no model of its own — without this the spawn silently drops
         * (scenery_load_model finds no model ids) or draws the wrong state. */
        struct ToriRS_Location resolved_cfg;
        struct ToriRS_Location* cfg = world_builder_resolve_loc_for_place(
            builder, CacheProvider_LocationGet(builder->cache, loc_id), &resolved_cfg);
        if( cfg )
        {
            struct ToriRS_MapLoc ml = {
                .loc_id = loc_id,
                .shape_select = shape,
                .orientation = angle,
                .chunk_pos_x = scene_x,
                .chunk_pos_z = scene_z,
                .chunk_pos_level = level,
            };
            world_builder_batch_begin(builder);
            builder->scenery_runtime_spawn = 1;
            /* Reuse the build path for correct per-shape model/orientation/size,
             * but suppress its single-slot painter registration (it would assert
             * on the baked static slot and be truncated next frame anyway) — the
             * spawned loc is drawn via world_cycle's per-frame scenery pass. The
             * shade/occluder/decor/sharelight accumulators are build-only (freed
             * at build end); world_scenery.u.c's scenery_shade_/scenery_occluder_/
             * scenery_decor_ shims answer for their absence, so those calls are
             * safe no-ops here. */
            painter_set_suppress_slot_registration(builder->world->painter, 1);
            scenery_add(builder, &ml, cfg, scene_x, scene_z);
            painter_set_suppress_slot_registration(builder->world->painter, 0);
            builder->scenery_runtime_spawn = 0;
            /* Collision only when the spawn registered a pool entry: the entry
             * is what lets the NEXT change on this tile find and undo this
             * collision — adding collision for a loc that failed to spawn (model
             * missing / shape absent) would leave phantom, un-removable flags. */
            if( World_SceneryFindAt(world, scene_x, scene_z, level, shape) >= 0 )
            {
                world_collision_add_loc(builder, &ml, cfg, scene_x, scene_z);
                /* And the new loc's ambient emitter. `cfg` is the resolved
                 * multiloc, which is what decides the sound -- a lever's two
                 * states can name different ones. */
                world_builder_add_loc_area_sound(builder, &ml, cfg, scene_x, scene_z);
                /* Draw the new loc's minimap line (red when interactive — an
                 * open door keeps its red line at the new edge). */
                /* Mapscene locs draw a sprite, not lines (drawDetail skips
                 * the wall branch for them). */
                if( world->minimap && cfg->map_scene_id == -1 )
                {
                    int mm_flags =
                        scenery_minimap_wall_flags(shape, angle, cfg->is_interactive);
                    if( mm_flags != 0 )
                    {
                        minimap_add_tile_wall(
                            world->minimap, scene_x, scene_z, level, mm_flags);
                        world->minimap_seq++;
                    }
                }
            }
            world_builder_batch_end(builder);
        }
    }
}
