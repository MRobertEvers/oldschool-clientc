/*
 * The server's copy of the scene: map squares in, collision and locs out.
 *
 * The collision rules are not restated here. `collision_map.c` is the client's
 * own implementation, ported from Client-TS, and it is *linked* — so the tiles
 * the server will let you walk on and the tiles the client would have let you
 * walk on come from one body of code. What this file does is the part the
 * client does inside its world builder and the server has never had: read the
 * lX_Z archives for the squares under the scene, decode terrain and locs, and
 * feed both to that map.
 *
 * Two things worth knowing before changing anything here:
 *
 * - **Map archives are XTEA-encrypted at rev 230** (the gate is revision 237).
 *   The keys ship beside the cache in xteas.json, the same file the client
 *   reads. No keys means no locs, which means an open field — accuracy lost,
 *   nothing broken.
 * - **A loc's footprint rotates with it.** An angle of 1 or 3 swaps size_x and
 *   size_z. Feeding the unrotated size to collision_map_add_loc blocks the
 *   wrong tiles for every non-square loc, which is invisible until someone
 *   walks through the long side of a table.
 */

#include "mock230_scene.h"

#include "mock230.h"

#include "engine/world_builder/collision_map.h"

#include <rscache.h>

#include <datatypes/maps.h>
#include <xtea_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The scene is the same 104x104 window the entity streams cover. */
enum
{
    SCENE_TILES = COLLISION_SIZE,
    SCENE_LEVELS = COLLISION_LEVELS,
};

static struct CollisionMap* g_collision[SCENE_LEVELS];
static int g_base_x = -1;
static int g_base_z = -1;

static struct Mock230SceneLoc* g_locs;
static int g_loc_count;
static int g_loc_capacity;

/* Loc configs, decoded once and kept: a door swap needs the *new* loc's
 * footprint, which is not in the map square. */
static struct RSCache_Dat2ConfigLoc** g_loc_configs;
static int g_loc_config_count;

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

struct CollisionMap*
mock230_scene_collision(int level)
{
    if( level < 0 || level >= SCENE_LEVELS )
        return NULL;
    return g_collision[level];
}

int
mock230_scene_base_x(void)
{
    return g_base_x;
}

int
mock230_scene_base_z(void)
{
    return g_base_z;
}

int
mock230_scene_contains(
    int x,
    int z)
{
    if( g_base_x < 0 )
        return 0;
    return x >= g_base_x && x < g_base_x + SCENE_TILES && z >= g_base_z &&
           z < g_base_z + SCENE_TILES;
}

int
mock230_scene_walk_blocked(
    int level,
    int x,
    int z)
{
    struct CollisionMap* collision;

    if( !mock230_scene_contains(x, z) )
        return 1;
    collision = mock230_scene_collision(level);
    if( !collision )
        return 1;
    return (collision_map_tile(collision, x - g_base_x, z - g_base_z) &
            COLL_FLAG_WALK_BLOCKED)
               ? 1
               : 0;
}

struct Mock230SceneLoc*
mock230_scene_loc(int slot)
{
    if( slot < 0 || slot >= g_loc_count )
        return NULL;
    return &g_locs[slot];
}

/* ------------------------------------------------------------------ */
/* Loc configs                                                         */
/* ------------------------------------------------------------------ */

static const struct RSCache_Dat2ConfigLoc*
loc_config(int loc_id)
{
    if( !g_loc_configs || loc_id < 0 || loc_id >= g_loc_config_count )
        return NULL;
    return g_loc_configs[loc_id];
}

static void
free_loc_configs(void)
{
    for( int i = 0; i < g_loc_config_count; i++ )
    {
        if( g_loc_configs[i] )
            RSCache_Dat2ConfigLocFree(g_loc_configs[i]);
    }
    free(g_loc_configs);
    g_loc_configs = NULL;
    g_loc_config_count = 0;
}

static int
load_loc_configs(
    struct RSCache_Dat2Disk* disk,
    struct RSCache* profile)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    struct RSCache_FileList* files;
    int highest = -1;

    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    RSCache_ProfileSetGroupRevision(profile, RSCACHE_TYPE_LOC, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    g_loc_config_count = highest + 1;
    g_loc_configs = calloc((size_t)(g_loc_config_count > 0 ? g_loc_config_count : 1),
                           sizeof(*g_loc_configs));
    if( !g_loc_configs )
    {
        g_loc_config_count = 0;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];

        if( id < 0 || id >= g_loc_config_count || files->file_sizes[i] <= 0 )
            continue;
        g_loc_configs[id] = RSCache_Dat2ConfigLocNewDecodeProfile(
            profile, files->files[i], files->file_sizes[i]);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return g_loc_config_count;
}

const char*
mock230_scene_loc_op(
    int loc_id,
    int op_num)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(loc_id);

    if( !config || op_num < 1 || op_num > 5 )
        return NULL;
    return config->actions[op_num - 1];
}

/* ------------------------------------------------------------------ */
/* Building collision                                                  */
/* ------------------------------------------------------------------ */

/*
 * Which collision primitive a loc contributes, by shape. Mirrors
 * src/engine/world_builder/world_collision.u.c — the client's own switch — and
 * has to keep mirroring it: the whole value of this file is that both ends
 * block the same tiles.
 */
static void
apply_loc_collision(
    struct Mock230SceneLoc* loc,
    int add)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(loc->loc_id);
    struct CollisionMap* map;
    enum CollisionLocAngle angle;
    int blockrange;
    int scene_x, scene_z;

    if( !config || loc->level < 0 || loc->level >= SCENE_LEVELS )
        return;
    map = g_collision[loc->level];
    if( !map )
        return;

    scene_x = loc->x - g_base_x;
    scene_z = loc->z - g_base_z;
    if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 || scene_z >= SCENE_TILES )
        return;

    angle = (enum CollisionLocAngle)(loc->angle & 3);
    blockrange = config->blocks_projectiles ? 1 : 0;

    switch( loc->shape )
    {
    case RSCACHE_LOC_SHAPE_FLOOR_DECORATION:
        /* Inactive ground decor never blocks — the reference gates on
         * blockwalk == 1 *and* interactive. */
        if( config->blocks_walk == 1 && config->is_interactive )
        {
            if( add )
                collision_map_add_floor(map, scene_x, scene_z);
            else
                collision_map_del_floor(map, scene_x, scene_z);
        }
        break;
    case RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE:
    case RSCACHE_LOC_SHAPE_WALL_TRI_CORNER:
    case RSCACHE_LOC_SHAPE_WALL_TWO_SIDES:
    case RSCACHE_LOC_SHAPE_WALL_RECT_CORNER:
        if( config->blocks_walk != 0 )
        {
            if( add )
                collision_map_add_wall(map, scene_x, scene_z, loc->shape, angle, blockrange);
            else
                collision_map_del_wall(map, scene_x, scene_z, loc->shape, angle, blockrange);
        }
        break;
    case RSCACHE_LOC_SHAPE_WALL_DIAGONAL:
    case RSCACHE_LOC_SHAPE_SCENERY:
    case RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_FLAT:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        if( config->blocks_walk != 0 )
        {
            if( add )
                collision_map_add_loc(map, scene_x, scene_z, loc->size_x, loc->size_z,
                                      angle, blockrange);
            else
                collision_map_del_loc(map, scene_x, scene_z, loc->size_x, loc->size_z,
                                      angle, blockrange);
        }
        break;
    default:
        /* Wall decorations (4..8) are ornaments hung on a wall that already
         * blocks. They contribute nothing, which is not the same as being
         * unhandled — hence the explicit case rather than a silent fallthrough.
         */
        break;
    }
}

static void
record_loc(
    const struct RSCache_MapLoc* map_loc,
    int map_x,
    int map_z)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(map_loc->loc_id);
    struct Mock230SceneLoc* loc;
    int abs_x = map_x * 64 + map_loc->chunk_pos_x;
    int abs_z = map_z * 64 + map_loc->chunk_pos_z;

    if( !config )
        return;
    if( !mock230_scene_contains(abs_x, abs_z) )
        return;

    if( g_loc_count == g_loc_capacity )
    {
        int capacity = g_loc_capacity ? g_loc_capacity * 2 : 1024;
        struct Mock230SceneLoc* grown = realloc(g_locs, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return;
        g_locs = grown;
        g_loc_capacity = capacity;
    }

    loc = &g_locs[g_loc_count++];
    loc->loc_id = map_loc->loc_id;
    loc->shape = map_loc->shape_select;
    loc->angle = map_loc->orientation;
    loc->x = abs_x;
    loc->z = abs_z;
    loc->level = map_loc->chunk_pos_level;
    /* Rotated footprint. An odd angle turns the loc a quarter turn, which
     * swaps its extents; getting this wrong is silent for every square loc and
     * wrong for every other one. */
    if( (loc->angle & 1) != 0 )
    {
        loc->size_x = config->size_z;
        loc->size_z = config->size_x;
    }
    else
    {
        loc->size_x = config->size_x;
        loc->size_z = config->size_z;
    }
    loc->active = 1;
    loc->changed = 0;

    apply_loc_collision(loc, 1);
}

/*
 * Terrain: a tile whose settings carry BLOCK is not walkable.
 *
 * The LINK_BELOW rule is the bridge case, and it is inverted from what the flag
 * name suggests: a level-1 tile marked LINK_BELOW means the level *below* it is
 * a bridge deck, so the blocked-ness at level 0 comes from level 1's flags. The
 * client does the same in world_collision_apply_bridges; without it every
 * bridge in Lumbridge is an impassable strip of water.
 */
static void
apply_terrain(
    const struct RSCache_MapTerrain* terrain,
    int map_x,
    int map_z)
{
    for( int local_x = 0; local_x < 64; local_x++ )
    {
        for( int local_z = 0; local_z < 64; local_z++ )
        {
            int abs_x = map_x * 64 + local_x;
            int abs_z = map_z * 64 + local_z;
            int scene_x = abs_x - g_base_x;
            int scene_z = abs_z - g_base_z;
            int link_below;

            if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 ||
                scene_z >= SCENE_TILES )
                continue;

            link_below =
                (terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(local_x, local_z, 1)].settings &
                 RSCACHE_FLOFLAG_LINK_BELOW) != 0;

            for( int level = 0; level < SCENE_LEVELS; level++ )
            {
                uint8_t settings =
                    terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(local_x, local_z, level)]
                        .settings;
                /* A bridge deck's blocking flag lives one level up. */
                int source_level = (level == 0 && link_below) ? 1 : level;

                settings = terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(local_x, local_z,
                                                                     source_level)]
                               .settings;
                if( (settings & RSCACHE_FLOFLAG_BLOCK) != 0 && g_collision[level] )
                    collision_map_add_floor(g_collision[level], scene_x, scene_z);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

static int
open_disk(
    const char* cache_dir,
    struct RSCache_Dat2Disk** out_disk,
    char* resolved,
    size_t resolved_size)
{
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);

    snprintf(resolved, resolved_size, "%s", cache_dir);
    if( !disk )
    {
        /* Same ../ fallback as the other cache loaders: run from src/ or from
         * the repo root. */
        snprintf(resolved, resolved_size, "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(resolved);
    }
    *out_disk = disk;
    return disk != NULL;
}

int
mock230_scene_build(
    const char* cache_dir,
    int zone_x,
    int zone_z)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    char resolved[512];
    char keys[600];
    int square_x0, square_x1, square_z0, square_z1;
    int locs_before;

    mock230_scene_free();

    g_base_x = (zone_x - 6) * 8;
    g_base_z = (zone_z - 6) * 8;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;

    if( !open_disk(cache_dir, &disk, resolved, sizeof(resolved)) )
    {
        fprintf(stderr,
                "mock230: no cache at %s — collision disabled (walk through walls)\n",
                cache_dir);
        g_base_x = g_base_z = -1;
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    /* The map archives are encrypted at this revision; the keys sit beside the
     * cache, in the file the client reads. Loading them is global state in
     * rscache, which is fine — the mock opens one cache. */
    snprintf(keys, sizeof(keys), "%s/xteas.json", resolved);
    if( !RSCache_XteaConfigLoadKeys(keys) )
        fprintf(stderr, "mock230: no %s — encrypted map squares will not decode\n", keys);

    for( int level = 0; level < SCENE_LEVELS; level++ )
    {
        g_collision[level] = collision_map_new(SCENE_TILES, SCENE_TILES);
        if( g_collision[level] )
            collision_map_reset(g_collision[level]);
    }

    load_loc_configs(disk, &profile);

    square_x0 = g_base_x >> 6;
    square_x1 = (g_base_x + SCENE_TILES - 1) >> 6;
    square_z0 = g_base_z >> 6;
    square_z1 = (g_base_z + SCENE_TILES - 1) >> 6;

    for( int map_x = square_x0; map_x <= square_x1; map_x++ )
    {
        for( int map_z = square_z0; map_z <= square_z1; map_z++ )
        {
            struct RSCache_MapTerrain* terrain =
                RSCache_MapTerrainNewFromCache(disk, map_x, map_z);
            struct RSCache_MapLocs* locs;

            if( terrain )
            {
                apply_terrain(terrain, map_x, map_z);
                RSCache_MapTerrainFree(terrain);
            }

            locs = RSCache_MapLocsNewFromCache(disk, map_x, map_z);
            if( !locs )
                continue;
            for( int i = 0; i < locs->locs_count; i++ )
                record_loc(&locs->locs[i], map_x, map_z);
            RSCache_MapLocsFree(locs);
        }
    }

    locs_before = g_loc_count;
    RSCache_Dat2DiskFree(disk);
    fprintf(stderr, "mock230: scene built at zone %d,%d (base %d,%d — %d locs)\n", zone_x,
            zone_z, g_base_x, g_base_z, locs_before);
    return 1;
}

void
mock230_scene_free(void)
{
    for( int level = 0; level < SCENE_LEVELS; level++ )
    {
        if( g_collision[level] )
            collision_map_free(g_collision[level]);
        g_collision[level] = NULL;
    }
    free(g_locs);
    g_locs = NULL;
    g_loc_count = 0;
    g_loc_capacity = 0;
    free_loc_configs();
    g_base_x = g_base_z = -1;
}

/* ------------------------------------------------------------------ */
/* Loc lookup and mutation                                             */
/* ------------------------------------------------------------------ */

int
mock230_scene_find_loc(
    int x,
    int z,
    int level,
    int loc_id)
{
    int fallback = -1;

    for( int i = 0; i < g_loc_count; i++ )
    {
        struct Mock230SceneLoc* loc = &g_locs[i];

        if( !loc->active || loc->level != level )
            continue;
        /* A loc's tile is its south-west corner, so a click on the far side of
         * a 2x2 staircase has to match the footprint, not the corner. */
        if( x < loc->x || x >= loc->x + loc->size_x || z < loc->z ||
            z >= loc->z + loc->size_z )
            continue;
        if( loc_id >= 0 && loc->loc_id == loc_id )
            return i;
        if( fallback < 0 )
            fallback = i;
    }
    /* An exact id match wins; otherwise the first loc on the tile. The client
     * names the loc it drew, and by the time an OPLOC lands somebody may have
     * already opened it — refusing the stale id would drop the second click on
     * every door. */
    return loc_id >= 0 ? fallback : fallback;
}

int
mock230_scene_replace_loc(
    int slot,
    int loc_id,
    int angle)
{
    struct Mock230SceneLoc* loc = mock230_scene_loc(slot);
    const struct RSCache_Dat2ConfigLoc* config;

    if( !loc || !loc->active )
        return 0;
    config = loc_config(loc_id);
    if( !config )
        return 0;

    apply_loc_collision(loc, 0);
    loc->loc_id = loc_id;
    loc->angle = angle;
    if( (angle & 1) != 0 )
    {
        loc->size_x = config->size_z;
        loc->size_z = config->size_x;
    }
    else
    {
        loc->size_x = config->size_x;
        loc->size_z = config->size_z;
    }
    loc->changed = 1;
    apply_loc_collision(loc, 1);
    return 1;
}

int
mock230_scene_next_changed_loc(int from)
{
    for( int i = from < 0 ? 0 : from; i < g_loc_count; i++ )
    {
        if( g_locs[i].active && g_locs[i].changed )
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Routing                                                             */
/* ------------------------------------------------------------------ */

/* Direction index -> tile delta, in the client's World_CoordStep numbering:
 * 0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE. */
static const int k_step_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int k_step_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

int
mock230_scene_can_step(
    int level,
    int x,
    int z,
    int dir)
{
    struct CollisionMap* map = mock230_scene_collision(level);
    int scene_x = x - g_base_x;
    int scene_z = z - g_base_z;

    if( !map || dir < 0 || dir > 7 )
        return 1;
    if( !mock230_scene_contains(x, z) )
        return 1;
    if( !mock230_scene_contains(x + k_step_dx[dir], z + k_step_dz[dir]) )
        return 1;

    switch( dir )
    {
    case 0:
        return collision_map_can_step_diagonal_north_west(map, scene_x, scene_z);
    case 1:
        return collision_map_can_step_north(map, scene_x, scene_z);
    case 2:
        return collision_map_can_step_diagonal_north_east(map, scene_x, scene_z);
    case 3:
        return collision_map_can_step_west(map, scene_x, scene_z);
    case 4:
        return collision_map_can_step_east(map, scene_x, scene_z);
    case 5:
        return collision_map_can_step_diagonal_south_west(map, scene_x, scene_z);
    case 6:
        return collision_map_can_step_south(map, scene_x, scene_z);
    default:
        return collision_map_can_step_diagonal_south_east(map, scene_x, scene_z);
    }
}

/* Straight-line interpolation, the mock's pre-collision behaviour. Used when
 * there is no collision map at all, so a caller never has to know. */
static int
route_straight(
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    int* path_x,
    int* path_z,
    int max_steps)
{
    int x = from_x;
    int z = from_z;
    int count = 0;

    while( (x != to_x || z != to_z) && count < max_steps )
    {
        if( x < to_x )
            x++;
        else if( x > to_x )
            x--;
        if( z < to_z )
            z++;
        else if( z > to_z )
            z--;
        path_x[count] = x;
        path_z[count] = z;
        count++;
    }
    return count;
}

int
mock230_scene_route(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    int* path_x,
    int* path_z,
    int max_steps)
{
    struct CollisionMap* map = mock230_scene_collision(level);
    int steps;

    if( from_x == to_x && from_z == to_z )
        return 0;
    if( !map || !mock230_scene_contains(from_x, from_z) ||
        !mock230_scene_contains(to_x, to_z) )
        return route_straight(from_x, from_z, to_x, to_z, path_x, path_z, max_steps);

    steps = collision_map_bfs_path(map, from_x - g_base_x, from_z - g_base_z,
                                   to_x - g_base_x, to_z - g_base_z, path_x, path_z,
                                   max_steps);
    if( steps < 0 )
        return -1;
    for( int i = 0; i < steps; i++ )
    {
        path_x[i] += g_base_x;
        path_z[i] += g_base_z;
    }
    return steps;
}
