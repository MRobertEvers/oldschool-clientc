#include "world_builder.h"

#include "blendmap.h"
#include "collision_map.h"
#include "contour_ground_queue.u.c"
#include "decor_buildmap.h"
#include "engine/cache_provider.h"
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

static void
world_builder_reconcile_dynamic_elements(struct WorldBuilder* builder)
{
    struct World* world;
    struct ToriDraw_Scene* scene;
    uint8_t* keep;
    struct World_EntityPool* pool;
    int id;
    int next;

    assert(builder);
    world = builder->world;
    scene = builder->scene;
    assert(world && scene);

    keep = (uint8_t*)calloc((size_t)TORIDRAW_SCENE_MAX_ELEMENTS, 1);
    assert(keep && "world_builder_reconcile_dynamic_elements: keep bitmap");

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* p = World_EntityPoolGet(pool, i);
        if( p )
            world_builder_mark_element_keep(keep, p->element_id);
    }
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* n = World_EntityPoolGet(pool, i);
        if( n )
            world_builder_mark_element_keep(keep, n->element_id);
    }
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* s = World_EntityPoolGet(pool, i);
        if( s )
            world_builder_mark_element_keep(keep, s->element_id);
    }
    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( p )
            world_builder_mark_element_keep(keep, p->element_id);
    }
    pool = &world->entities.spotanim;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, i);
        if( s )
            world_builder_mark_element_keep(keep, s->element_id);
    }

    for( id = scene->elements.head; id != TORIDRAW_INTRUSIVE_NIL; id = next )
    {
        struct ToriDraw_SceneElement* el;

        next = scene->elements.nodes[id].next;
        el = ToriDraw_SceneElementGet(scene, id);
        if( !el || el->pool != (uint8_t)TORIDRAW_SCENE_POOL_DYNAMIC )
            continue;
        if( !keep[id] )
            ToriDraw_SceneElementRemove(scene, id);
    }
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
    /* calloc leaves the debug ring at 0, which is a valid element id. */
    for( int i = 0; i < (int)(sizeof(builder->scenery_dbg_element) /
                              sizeof(builder->scenery_dbg_element[0]));
         i++ )
        builder->scenery_dbg_element[i] = -1;
    return builder;
}

void
WorldBuilder_Free(struct WorldBuilder* builder)
{
    if( !builder )
        return;
    world_builder_free_transient_maps(builder);
    free(builder);
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

    world_builder_free_transient_maps(builder);
    World_ResetScene(world, zone_center_x, zone_center_z, scene_size);

    /* Static pool only: entity elements (players/npcs/objs) keep their ids
     * across a rebuild — the REBUILD_NORMAL shift relocates them instead. */
    ToriDraw_SceneClearPool(builder->scene, TORIDRAW_SCENE_POOL_STATIC);
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

    if( getenv("TORIRS_SCENERY_DEBUG") )
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

    if( getenv("TORIRS_SCENERY_DEBUG") )
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

    /* Terrain first (place-time LinkBelow shift for BLOCK flags), then End's
     * no-op bridge hook — loc collision already shifted at place time. Geometry
     * push-down below is separate. See docs/COLLISION_MAP.md. */
    world_collision_apply_terrain(builder);
    world_collision_apply_bridges(builder);
    world_contour_ground(builder);
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

    world_build_scene_terrain(builder);
    world_build_lighting(builder);

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
    WorldBuilder_RebuildCenterzoneBegin(builder, zone_center_x, zone_center_z, scene_size);

    ToriDraw_SceneBatchBegin(builder->scene);

    struct World* world = builder->world;
    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            WorldBuilder_RebuildCenterzoneChunkTerrain(builder, mapx, mapz);
    }

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            WorldBuilder_RebuildCenterzoneChunkScenery(builder, mapx, mapz);
    }

    WorldBuilder_RebuildCenterzoneEnd(builder);

    ToriDraw_SceneBatchEnd(builder->scene);
}

void
WorldBuilder_RebuildChunklistBegin(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count)
{
    struct World* world = builder->world;
    assert(world && "WorldBuilder_RebuildChunklistBegin: world is NULL");

    world_builder_free_transient_maps(builder);
    World_ResetSceneChunkList(world, chunks_xz, count);

    ToriDraw_SceneClearPool(builder->scene, TORIDRAW_SCENE_POOL_STATIC);
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

    ToriDraw_SceneBatchBegin(builder->scene);

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

    ToriDraw_SceneBatchEnd(builder->scene);
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
            /* A removed WALL loc must also release its exclusive painter tile
             * slot (wall_a/wall_b): the dead static element would otherwise
             * keep the slot claimed, blocking the replacement wall's per-frame
             * registration and leaving a stale reference. */
            if( old->shape >= RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE &&
                old->shape <= RSCACHE_LOC_SHAPE_WALL_RECT_CORNER && world->painter )
                painter_release_wall(world->painter, scene_x, scene_z, level, old->element_id);
        }
        World_SceneryRemove(world, idx);
    }

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
            ToriDraw_SceneBatchBegin(builder->scene);
            builder->scenery_runtime_spawn = 1;
            /* Reuse the build path for correct per-shape model/orientation/size,
             * but suppress its single-slot painter registration (it would assert
             * on the baked static slot and be truncated next frame anyway) — the
             * spawned loc is drawn via world_cycle's per-frame scenery pass. The
             * shade/decor/sharelight accumulators are build-only (freed at build
             * end) and NULL-guarded, so those calls are safe no-ops here. */
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
            ToriDraw_SceneBatchEnd(builder->scene);
        }
    }
}
