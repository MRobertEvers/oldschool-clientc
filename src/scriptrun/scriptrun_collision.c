/*
 * A script-run bot's collision, built the way the client builds its own.
 *
 * On a REBUILD the client loads the map squares the scene covers, writes each
 * tile's terrain settings into a flag map, places every map loc through its
 * multiloc rung, and stamps collision with engine/world_builder/
 * world_collision.u.c -- terrain BLOCK, LINK_BELOW's level shift, walls, locs
 * and blocking ground decor (world_builder.c RebuildCenterzone*). The bot does
 * the same, with the client's own collision unit and flag map compiled in
 * below, the client's adaptors from cache records to its own loc and map
 * types, the map files read from the cache directory, and the loc configs
 * decoded from their CLIENT records (ToriRSServer_SceneLocConfigDecode). The
 * renderer half of the builder (models, lighting, painter) is not run.
 *
 * Only REBUILD_NORMAL is built. An instanced scene (REBUILD_REGION's template
 * zones) is left without collision for now, and says so.
 */
#include "scriptrun_core.h"

#include "engine/torirs_location_from_rscache.h"
#include "engine/torirs_map_from_rscache.h"
#include "engine/torirs_types.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_scene.h"
#include "world/world.h"

/* The client's collision unit and the flag map it reads, compiled here. */
#include "engine/world_builder/flag_map.u.c"
#include "engine/world_builder/world_collision.u.c"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SCRIPTRUN_SCENE_SIZE = 104,
};

/* Decoded loc configs, shared by every bot: configs are global, read-only
 * data. Indexed by loc id, filled on first use. */
static struct ToriRS_Location** g_locs;
static int g_locs_count;

static struct ToriRS_Location*
loc_config(int loc_id)
{
    struct RSCache_Dat2ConfigLoc* raw;

    if( loc_id < 0 )
        return NULL;
    if( loc_id >= g_locs_count )
    {
        int grow = loc_id + 1024;
        g_locs = realloc(g_locs, (size_t)grow * sizeof(*g_locs));
        assert(g_locs);
        memset(g_locs + g_locs_count, 0, (size_t)(grow - g_locs_count) * sizeof(*g_locs));
        g_locs_count = grow;
    }
    if( g_locs[loc_id] )
        return g_locs[loc_id];
    raw = ToriRSServer_SceneLocConfigDecode(loc_id);
    if( !raw )
        return NULL;
    raw->_id = loc_id;
    g_locs[loc_id] = ToriRS_LocationFromRSCacheDat2(loc_id, raw);
    RSCache_Dat2ConfigLocFree(raw);
    return g_locs[loc_id];
}

/* VarPManager_ResolveTransform over the bot's own varps, walked as
 * world_builder_resolve_loc does; NULL when the rung selects no loc. */
static struct ToriRS_Location*
resolve_loc(
    struct ScriptrunCore const* core,
    struct ToriRS_Location* base)
{
    struct ToriRS_Location* resolved = base;

    for( int depth = 0; depth < 16; ++depth )
    {
        int index = -1;
        int id;

        if( resolved->transform_count <= 0 || !resolved->transforms )
            return resolved;
        if( resolved->transform_varbit != -1 )
        {
            if( !ScriptrunCore_Varbit(core, resolved->transform_varbit, &index) )
                index = 0;
        }
        else if( resolved->transform_varp != -1 && resolved->transform_varp < SCRIPTRUN_VARPS )
            index = core->varps[resolved->transform_varp];
        if( index >= 0 && index < resolved->transform_count - 1 )
            id = resolved->transforms[index];
        else
            id = resolved->transforms[resolved->transform_count - 1];
        if( id < 0 )
            return NULL;
        if( id == resolved->id )
            return resolved;
        resolved = loc_config(id);
        if( !resolved )
            return NULL;
    }
    return NULL;
}

static void
loc_append(
    struct ScriptrunCore* core,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int from_map)
{
    struct ScriptrunLoc* l;

    if( core->loc_count == core->loc_capacity )
    {
        core->loc_capacity = core->loc_capacity ? core->loc_capacity * 2 : 4096;
        core->locs = realloc(core->locs, (size_t)core->loc_capacity * sizeof(*core->locs));
        assert(core->locs);
    }
    l = &core->locs[core->loc_count++];
    l->x = x;
    l->z = z;
    l->level = level;
    l->shape = shape;
    l->angle = angle;
    l->loc_id = loc_id;
    l->map_loc_id = from_map ? loc_id : -1;
}

/* The record a placed loc stamps collision with: its rung, its base footprint
 * (world_builder_resolve_loc_for_place). NULL when the rung is no loc. */
static struct ToriRS_Location*
placed_config(
    struct ScriptrunCore const* core,
    int loc_id,
    struct ToriRS_Location* storage)
{
    struct ToriRS_Location* base = loc_config(loc_id);
    struct ToriRS_Location* resolved;

    if( !base )
        return NULL;
    resolved = resolve_loc(core, base);
    if( !resolved )
        return NULL;
    *storage = *resolved;
    storage->size_x = base->size_x;
    storage->size_z = base->size_z;
    return storage;
}

void
ScriptrunCore_FreeCollision(struct ScriptrunCore* core)
{
    struct WorldBuilder* builder = core->builder;

    if( builder )
    {
        if( builder->flag_map )
            flag_map_free(builder->flag_map);
        free(builder);
        core->builder = NULL;
    }
    free(core->locs);
    core->locs = NULL;
    core->loc_count = 0;
    core->loc_capacity = 0;
}

void
ScriptrunCore_BuildCollision(struct ScriptrunCore* core)
{
    struct World* world;
    struct WorldBuilder* builder;
    int base_x, base_z;
    int placed = 0, squares = 0;

    assert(core);
    if( !core->cache_disk )
        return;
    ScriptrunCore_FreeCollision(core);
    world = core->world;
    base_x = world->_base_tile_x;
    base_z = world->_base_tile_z;
    world->_scene_size = SCRIPTRUN_SCENE_SIZE;
    for( int level = 0; level < COLLISION_LEVELS; level++ )
    {
        if( world->collision_maps[level] )
            collision_map_free(world->collision_maps[level]);
        world->collision_maps[level] = collision_map_new(SCRIPTRUN_SCENE_SIZE, SCRIPTRUN_SCENE_SIZE);
    }

    builder = calloc(1, sizeof(*builder));
    assert(builder);
    core->builder = builder;
    builder->world = world;
    builder->flag_map =
        flag_map_new(SCRIPTRUN_SCENE_SIZE, SCRIPTRUN_SCENE_SIZE, WORLD_MAP_TERRAIN_LEVELS);
    assert(builder->flag_map);

    /* 1. Terrain settings into the flag map (world_terrain_apply_tile). */
    for( int mx = base_x >> 6; mx <= (base_x + SCRIPTRUN_SCENE_SIZE - 1) >> 6; mx++ )
    {
        for( int mz = base_z >> 6; mz <= (base_z + SCRIPTRUN_SCENE_SIZE - 1) >> 6; mz++ )
        {
            struct RSCache_MapTerrain* raw = RSCache_MapTerrainNewFromCache(core->cache_disk, mx, mz);
            struct ToriRS_MapTerrain* terrain;
            if( !raw )
                continue;
            terrain = ToriRS_MapTerrainFromRSCache(mx, mz, raw);
            RSCache_MapTerrainFree(raw);
            if( !terrain )
                continue;
            squares++;
            for( int lx = 0; lx < 64; lx++ )
            {
                for( int lz = 0; lz < 64; lz++ )
                {
                    int sx = mx * 64 + lx - base_x;
                    int sz = mz * 64 + lz - base_z;
                    if( sx < 0 || sz < 0 || sx >= SCRIPTRUN_SCENE_SIZE || sz >= SCRIPTRUN_SCENE_SIZE )
                        continue;
                    for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
                        flag_map_set(builder->flag_map, sx, sz, level,
                                     terrain->tiles_xyz[World_MapTileCoord(lx, lz, level)].settings);
                }
            }
            ToriRS_MapTerrainFree(terrain);
        }
    }

    /* 2. Map locs through their rung, collision stamped by the client's unit
     *    (WorldBuilder_RebuildCenterzoneChunkScenery's loop, renderer half
     *    left out). */
    for( int mx = base_x >> 6; mx <= (base_x + SCRIPTRUN_SCENE_SIZE - 1) >> 6; mx++ )
    {
        for( int mz = base_z >> 6; mz <= (base_z + SCRIPTRUN_SCENE_SIZE - 1) >> 6; mz++ )
        {
            struct RSCache_MapLocs* raw = RSCache_MapLocsNewFromCache(core->cache_disk, mx, mz);
            struct ToriRS_MapLocs* locs;
            if( !raw )
                continue;
            locs = ToriRS_MapLocsFromRSCache(raw);
            RSCache_MapLocsFree(raw);
            if( !locs )
                continue;
            for( int i = 0; i < locs->locs_count; i++ )
            {
                struct ToriRS_MapLoc* map_loc = &locs->locs[i];
                struct ToriRS_Location storage;
                struct ToriRS_Location* config;
                int sx = mx * 64 + map_loc->chunk_pos_x - base_x;
                int sz = mz * 64 + map_loc->chunk_pos_z - base_z;

                if( sx < 0 || sz < 0 || sx >= SCRIPTRUN_SCENE_SIZE || sz >= SCRIPTRUN_SCENE_SIZE )
                    continue;
                /* Recorded even when its rung is no loc: a varp change or a
                 * zone change still addresses it. */
                loc_append(core, base_x + sx, base_z + sz, map_loc->chunk_pos_level,
                           map_loc->shape_select, map_loc->orientation, map_loc->loc_id, 1);
                config = placed_config(core, map_loc->loc_id, &storage);
                if( !config )
                    continue;
                world_collision_add_loc(builder, map_loc, config, sx, sz);
                placed++;
            }
            ToriRS_MapLocsFree(locs);
        }
    }

    /* 3. Terrain BLOCK onto the playable level (world_builder.c:1011). */
    world_collision_apply_terrain(builder);
    world_collision_apply_bridges(builder);
    core->have_collision = 1;
    core->collision_squares = squares;
    core->collision_locs = placed;
}

/* One source map square's terrain and locs, decoded once per region build. */
struct InstanceSquare
{
    int map_x, map_z;
    struct ToriRS_MapTerrain* terrain;
    struct ToriRS_MapLocs* locs;
};

static struct InstanceSquare*
instance_square(
    struct ScriptrunCore* core,
    struct InstanceSquare* squares,
    int* count,
    int cap,
    int map_x,
    int map_z)
{
    struct InstanceSquare* sq;
    struct RSCache_MapTerrain* raw_terrain;
    struct RSCache_MapLocs* raw_locs;

    (void)cap;
    for( int i = 0; i < *count; i++ )
        if( squares[i].map_x == map_x && squares[i].map_z == map_z )
            return &squares[i];
    assert(*count < cap);
    sq = &squares[(*count)++];
    sq->map_x = map_x;
    sq->map_z = map_z;
    sq->terrain = NULL;
    sq->locs = NULL;
    raw_terrain = RSCache_MapTerrainNewFromCache(core->cache_disk, map_x, map_z);
    if( raw_terrain )
    {
        sq->terrain = ToriRS_MapTerrainFromRSCache(map_x, map_z, raw_terrain);
        RSCache_MapTerrainFree(raw_terrain);
    }
    raw_locs = RSCache_MapLocsNewFromCache(core->cache_disk, map_x, map_z);
    if( raw_locs )
    {
        sq->locs = ToriRS_MapLocsFromRSCache(raw_locs);
        RSCache_MapLocsFree(raw_locs);
    }
    return sq;
}

void
ScriptrunCore_BuildInstanceCollision(
    struct ScriptrunCore* core,
    const int32_t* zones)
{
    enum
    {
        ZONES = WORLD_INSTANCE_ZONES,
        SQUARES_CAP = 64,
    };
    struct World* world;
    struct WorldBuilder* builder;
    struct InstanceSquare squares[SQUARES_CAP];
    int square_count = 0;
    int base_x, base_z;
    int placed = 0;
    int zone_count = SCRIPTRUN_SCENE_SIZE / 8;

    assert(core);
    assert(zones);
    if( !core->cache_disk )
        return;
    ScriptrunCore_FreeCollision(core);
    world = core->world;
    base_x = world->_base_tile_x;
    base_z = world->_base_tile_z;
    world->_scene_size = SCRIPTRUN_SCENE_SIZE;
    for( int level = 0; level < COLLISION_LEVELS; level++ )
    {
        if( world->collision_maps[level] )
            collision_map_free(world->collision_maps[level]);
        world->collision_maps[level] = collision_map_new(SCRIPTRUN_SCENE_SIZE, SCRIPTRUN_SCENE_SIZE);
    }
    builder = calloc(1, sizeof(*builder));
    assert(builder);
    core->builder = builder;
    builder->world = world;
    builder->flag_map =
        flag_map_new(SCRIPTRUN_SCENE_SIZE, SCRIPTRUN_SCENE_SIZE, WORLD_MAP_TERRAIN_LEVELS);
    assert(builder->flag_map);

    /* WorldBuilder_RebuildInstance's two passes, the renderer half left out:
     * every zone's terrain settings, then every zone's locs (a loc's footprint
     * can cross into the next zone).  Descriptor per zone: rotation bits 1-2,
     * source zone z bits 3-13, x bits 14-23, level bits 24-25. */
    for( int pass = 0; pass < 2; pass++ )
    {
        for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
        {
            for( int zx = 0; zx < zone_count; zx++ )
            {
                for( int zz = 0; zz < zone_count; zz++ )
                {
                    int32_t d = zones[level * ZONES * ZONES + zx * ZONES + zz];
                    int rotation, src_zone_x, src_zone_z, src_level;
                    int src_tile_x, src_tile_z;
                    struct InstanceSquare* sq;

                    if( d == 0 )
                        continue;
                    rotation = (d >> 1) & 0x3;
                    src_zone_z = (d >> 3) & 0x7ff;
                    src_zone_x = (d >> 14) & 0x3ff;
                    src_level = (d >> 24) & 0x3;
                    src_tile_x = (src_zone_x & 7) * 8;
                    src_tile_z = (src_zone_z & 7) * 8;
                    sq = instance_square(core, squares, &square_count, SQUARES_CAP,
                                         src_zone_x >> 3, src_zone_z >> 3);

                    if( pass == 0 )
                    {
                        /* WorldBuilder_RebuildInstanceZoneTerrain: walk the
                         * destination, read the source tile it came from. */
                        if( !sq->terrain )
                            continue;
                        for( int dx = 0; dx < 8; dx++ )
                        {
                            for( int dz = 0; dz < 8; dz++ )
                            {
                                int sx, sz;
                                world_instance_rotate_to_src(rotation, dx, dz, &sx, &sz);
                                flag_map_set(builder->flag_map, zx * 8 + dx, zz * 8 + dz, level,
                                             sq->terrain->tiles_xyz[World_MapTileCoord(
                                                 src_tile_x + sx, src_tile_z + sz, src_level)].settings);
                            }
                        }
                        continue;
                    }

                    /* WorldBuilder_RebuildInstanceZoneScenery: walk the
                     * source's locs in this zone, place each where the turned
                     * zone puts it, turned with it. */
                    if( !sq->locs )
                        continue;
                    for( int i = 0; i < sq->locs->locs_count; i++ )
                    {
                        struct ToriRS_MapLoc loc = sq->locs->locs[i];
                        struct ToriRS_Location storage;
                        struct ToriRS_Location* base;
                        struct ToriRS_Location* config;
                        int sx = loc.chunk_pos_x - src_tile_x;
                        int sz = loc.chunk_pos_z - src_tile_z;
                        int size_x, size_z, dx, dz, scene_x, scene_z;

                        if( loc.chunk_pos_level != src_level || sx < 0 || sx > 7 || sz < 0 || sz > 7 )
                            continue;
                        base = loc_config(loc.loc_id);
                        if( !base )
                            continue;
                        config = placed_config(core, loc.loc_id, &storage);
                        /* The footprint as placed in the source (an odd angle
                         * has swapped the extents); a hidden rung's is its
                         * base's. */
                        size_x = (config ? config : base)->size_x > 0 ? (config ? config : base)->size_x : 1;
                        size_z = (config ? config : base)->size_z > 0 ? (config ? config : base)->size_z : 1;
                        if( (loc.orientation & 1) != 0 )
                        {
                            int tmp = size_x;
                            size_x = size_z;
                            size_z = tmp;
                        }
                        world_instance_rotate_to_dst(rotation, sx, sz, size_x, size_z, &dx, &dz);
                        scene_x = zx * 8 + dx;
                        scene_z = zz * 8 + dz;
                        if( scene_x < 0 || scene_z < 0 || scene_x >= SCRIPTRUN_SCENE_SIZE ||
                            scene_z >= SCRIPTRUN_SCENE_SIZE )
                            continue;
                        loc.orientation = (loc.orientation + rotation) & 3;
                        loc.chunk_pos_level = level;
                        loc_append(core, base_x + scene_x, base_z + scene_z, level, loc.shape_select,
                                   loc.orientation, loc.loc_id, 1);
                        if( !config )
                            continue;
                        world_collision_add_loc(builder, &loc, config, scene_x, scene_z);
                        placed++;
                    }
                }
            }
        }
    }

    for( int i = 0; i < square_count; i++ )
    {
        if( squares[i].terrain )
            ToriRS_MapTerrainFree(squares[i].terrain);
        if( squares[i].locs )
            ToriRS_MapLocsFree(squares[i].locs);
    }
    world_collision_apply_terrain(builder);
    world_collision_apply_bridges(builder);
    core->have_collision = 1;
    core->collision_squares = square_count;
    core->collision_locs = placed;
}

int
ScriptrunCore_CollisionFlags(
    struct ScriptrunCore const* core,
    int level,
    int abs_x,
    int abs_z)
{
    struct World const* world;
    int sx, sz;

    assert(core);
    world = core->world;
    if( !core->have_collision || level < 0 || level >= COLLISION_LEVELS ||
        !world->collision_maps[level] )
        return -1;
    sx = abs_x - world->_base_tile_x;
    sz = abs_z - world->_base_tile_z;
    if( sx < 0 || sz < 0 || sx >= SCRIPTRUN_SCENE_SIZE || sz >= SCRIPTRUN_SCENE_SIZE )
        return -1;
    return (int)collision_map_tile(world->collision_maps[level], sx, sz);
}

/* zone_loc_tile_at: a zone names the walked plane; a loc on a bridge column
 * sits one cache level above it (World_LocCacheLevel). Scene tiles. */
static int
loc_cache_level(
    struct ScriptrunCore const* core,
    int sx,
    int sz,
    int zone_level)
{
    struct WorldBuilder const* builder = core->builder;

    if( builder && sx >= 0 && sz >= 0 && sx < SCRIPTRUN_SCENE_SIZE && sz < SCRIPTRUN_SCENE_SIZE &&
        zone_level >= 0 && zone_level < WORLD_MAP_TERRAIN_LEVELS - 1 &&
        (flag_map_get(builder->flag_map, sx, sz, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
        return zone_level + 1;
    return zone_level;
}

/* Stamp (add) or undo one row's collision through the rung it resolves to
 * now, as the client's ApplyLocChange does for the loc it removes or adds. */
static void
loc_row_collision(
    struct ScriptrunCore* core,
    struct ScriptrunLoc const* l,
    int add)
{
    struct WorldBuilder* builder = core->builder;
    struct ToriRS_Location storage;
    struct ToriRS_Location* config;
    int sx = l->x - core->world->_base_tile_x;
    int sz = l->z - core->world->_base_tile_z;

    if( !builder || sx < 0 || sz < 0 || sx >= SCRIPTRUN_SCENE_SIZE || sz >= SCRIPTRUN_SCENE_SIZE )
        return;
    config = placed_config(core, l->loc_id, &storage);
    if( !config )
        return;
    {
        struct ToriRS_MapLoc ml = {
            .loc_id = l->loc_id,
            .shape_select = l->shape,
            .orientation = l->angle,
            .chunk_pos_x = sx,
            .chunk_pos_z = sz,
            .chunk_pos_level = l->level,
        };
        if( add )
            world_collision_add_loc(builder, &ml, config, sx, sz);
        else
            world_collision_del_loc(builder, &ml, config, sx, sz);
    }
}

void
ScriptrunCore_ZoneResetLocs(
    struct ScriptrunCore* core,
    int abs_x0,
    int abs_z0,
    int zone_level)
{
    int base_x, base_z;

    assert(core);
    base_x = core->world->_base_tile_x;
    base_z = core->world->_base_tile_z;
    /* Every change row goes first, then every deleted map row comes back:
     * the client's per-key restore lands the map's loc after removing what
     * stood there, so its stamp is the last word on a shared tile. */
    for( int pass = 0; pass < 2; pass++ )
    {
        for( int i = 0; i < core->loc_count; i++ )
        {
            struct ScriptrunLoc* l = &core->locs[i];

            if( l->x < abs_x0 || l->x >= abs_x0 + 8 || l->z < abs_z0 || l->z >= abs_z0 + 8 )
                continue;
            if( l->level != loc_cache_level(core, l->x - base_x, l->z - base_z, zone_level) )
                continue;
            if( pass == 0 && l->map_loc_id < 0 && l->loc_id >= 0 )
            {
                loc_row_collision(core, l, 0);
                l->loc_id = -1;
            }
            else if( pass == 1 && l->map_loc_id >= 0 && l->loc_id < 0 )
            {
                l->loc_id = l->map_loc_id;
                loc_row_collision(core, l, 1);
            }
        }
    }
}

void
ScriptrunCore_LocChange(
    struct ScriptrunCore* core,
    int abs_x,
    int abs_z,
    int zone_level,
    int shape,
    int angle,
    int loc_id)
{
    struct WorldBuilder* builder = core->builder;
    int sx, sz, level;
    int layer = World_LocShapeToLayer(shape);
    struct ToriRS_Location storage;
    struct ToriRS_Location* config;

    assert(core);
    sx = abs_x - core->world->_base_tile_x;
    sz = abs_z - core->world->_base_tile_z;
    level = loc_cache_level(core, sx, sz, zone_level);

    /* 1. Whatever stood in this layer goes, collision undone through the same
     *    rung it was stamped with. */
    for( int i = 0; i < core->loc_count; i++ )
    {
        struct ScriptrunLoc* l = &core->locs[i];
        if( l->loc_id < 0 || l->x != abs_x || l->z != abs_z || l->level != level ||
            World_LocShapeToLayer(l->shape) != layer )
            continue;
        if( builder && (config = placed_config(core, l->loc_id, &storage)) != NULL )
        {
            struct ToriRS_MapLoc old_ml = {
                .loc_id = l->loc_id,
                .shape_select = l->shape,
                .orientation = l->angle,
                .chunk_pos_x = sx,
                .chunk_pos_z = sz,
                .chunk_pos_level = level,
            };
            world_collision_del_loc(builder, &old_ml, config, sx, sz);
        }
        l->loc_id = -1;
    }
    /* 2. The new loc, stamped. */
    if( loc_id < 0 )
        return;
    loc_append(core, abs_x, abs_z, level, shape, angle, loc_id, 0);
    if( builder && sx >= 0 && sz >= 0 && sx < SCRIPTRUN_SCENE_SIZE && sz < SCRIPTRUN_SCENE_SIZE &&
        (config = placed_config(core, loc_id, &storage)) != NULL )
    {
        struct ToriRS_MapLoc ml = {
            .loc_id = loc_id,
            .shape_select = shape,
            .orientation = angle,
            .chunk_pos_x = sx,
            .chunk_pos_z = sz,
            .chunk_pos_level = level,
        };
        world_collision_add_loc(builder, &ml, config, sx, sz);
    }
}

char const*
ScriptrunCore_LocOpName(
    struct ScriptrunCore const* core,
    int loc_id,
    int op)
{
    struct ToriRS_Location* base;
    struct ToriRS_Location* resolved;

    assert(core);
    assert(op >= 1);
    assert(op <= TORIRS_MENU_ACTION_SLOTS);
    base = loc_config(loc_id);
    if( !base )
        return NULL;
    /* The menu is the rung the bot's varps select (the client builds a
     * multiloc's rows from its resolved record). */
    resolved = resolve_loc(core, base);
    if( !resolved || !resolved->actions[op - 1][0] )
        return NULL;
    return resolved->actions[op - 1];
}
