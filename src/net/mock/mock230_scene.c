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
#include "features/features.h"

#include <rscache.h>

#include <datatypes/maps.h>
#include <xtea_config.h>

#include <assert.h>
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

/* Era feature table: approach model + op-click nearest fallback. Resolved once
 * at boot; defaults to OSRS when nothing has been set yet (this server boots
 * against an OldSchool cache). */
static struct ToriRS_FeatureTable const* g_features;

/* Which scene columns have a bridge deck (LINK_BELOW at raw level 1), captured
 * during apply_terrain and read by apply_loc_collision for place-time level
 * shift (Client-TS / LostCity). Map-square terrain buffers are freed per
 * square, so this has to be persisted for locs in the same build pass. */
static unsigned char g_link_below[SCENE_TILES][SCENE_TILES];

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

int
mock230_loc_resolve_transform(
    struct Mock230Player* player,
    int base_loc_id)
{
    const struct RSCache_Dat2ConfigLoc* cfg;
    int index = -1;
    int last;

    assert(player);
    cfg = loc_config(base_loc_id);
    if( !cfg || cfg->transform_count <= 0 || !cfg->transforms )
        return base_loc_id;

    if( cfg->transform_varbit != -1 )
        index = mock230_varbit_get(player, cfg->transform_varbit);
    else if( cfg->transform_varp != -1 )
    {
        if( cfg->transform_varp >= 0 && cfg->transform_varp < MOCK230_VARP_COUNT )
            index = (int)player->varps[cfg->transform_varp];
    }

    /* Same rule as VarPManager_ResolveTransform / OpenRS2 getMultiLoc: a valid
     * in-range non-hide slot wins; otherwise the last entry (often -1). */
    if( index >= 0 && index < cfg->transform_count - 1 && cfg->transforms[index] != -1 )
        return cfg->transforms[index];

    last = cfg->transforms[cfg->transform_count - 1];
    return last;
}

/* ------------------------------------------------------------------ */
/* Building collision                                                  */
/* ------------------------------------------------------------------ */

/*
 * Which collision primitive a loc contributes, by shape. Mirrors
 * src/engine/world_builder/world_collision.u.c — the client's own switch — and
 * has to keep mirroring it: the whole value of this file is that both ends
 * block the same tiles.
 *
 * LINK_BELOW: stamp onto collision level-1 (Client-TS ClientBuild /
 * LostCity place-time shift). Geometry / loc->level stay at the raw cache
 * level. See docs/COLLISION_MAP.md.
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
    int coll_level;

    if( !config || loc->level < 0 || loc->level >= SCENE_LEVELS )
        return;

    scene_x = loc->x - g_base_x;
    scene_z = loc->z - g_base_z;
    if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 || scene_z >= SCENE_TILES )
        return;

    coll_level = loc->level;
    if( g_link_below[scene_x][scene_z] )
        coll_level--;
    if( coll_level < 0 )
        return;

    map = g_collision[coll_level];
    if( !map )
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
            /*
             * The UN-rotated extents, deliberately.
             *
             * `collision_map_loc_apply` (collision_map.c:120-125) already swaps
             * size_x/size_z itself for COLL_ANGLE_NORTH (1) and _SOUTH (3) —
             * which is exactly `angle & 1`. `record_loc` pre-rotates into
             * `loc->size_x`/`loc->size_z` for the same angles, so passing those
             * applied the swap twice and it cancelled: every non-square loc at
             * angle 1 or 3 (rotated tables, benches, 1x2 stairs, stalls) blocked
             * its un-rotated rectangle, i.e. the wrong tiles.
             *
             * Do NOT "fix" this by removing the pre-rotation in `record_loc` —
             * `mock230_scene_find_loc` and `interaction_target` both need the
             * rotated footprint. The two consumers want different things and
             * this is the one that wants the raw config.
             */
            if( add )
                collision_map_add_loc(map, scene_x, scene_z, config->size_x, config->size_z,
                                      angle, blockrange);
            else
                collision_map_del_loc(map, scene_x, scene_z, config->size_x, config->size_z,
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
    /* From the map square, which is what makes its slot a tombstone rather than
     * reusable once something removes it — see mock230_scene_add_loc. */
    loc->is_static = 1;

    apply_loc_collision(loc, 1);
}

/*
 * Terrain: a tile whose settings carry BLOCK is not walkable.
 *
 * LINK_BELOW uses Client-TS / LostCity place-time trueLevel-- (stamp onto the
 * playable collision level). Loc collision does the same shift in
 * apply_loc_collision. A post-hoc whole-word column overwrite used to split
 * dual-tile wall stamps — docs/COLLISION_MAP.md.
 *
 * Roof bits stay on the raw cache level (LostCity changeRoofCollision).
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
            if( link_below )
                g_link_below[scene_x][scene_z] = 1;

            for( int level = 0; level < SCENE_LEVELS; level++ )
            {
                uint8_t settings =
                    terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(local_x, local_z, level)]
                        .settings;
                int true_level = level;

                if( (settings & RSCACHE_FLOFLAG_BLOCK) != 0 )
                {
                    if( link_below )
                        true_level--;
                    if( true_level >= 0 && true_level < SCENE_LEVELS &&
                        g_collision[true_level] )
                        collision_map_add_floor(g_collision[true_level], scene_x, scene_z);
                }

                /* Roofed-ness, which is what moverestrict=indoors/outdoors
                 * reads. REMOVE_ROOF marks the tiles the client hides roofs
                 * over, i.e. the ones that are indoors, and it is the same bit
                 * the reference stamps ROOF from (LostCity GameMap). Raw cache
                 * level, no bridge shift. */
                if( (settings & RSCACHE_FLOFLAG_REMOVE_ROOF) != 0 && g_collision[level] )
                    collision_map_change_roof(g_collision[level], scene_x, scene_z, 1);
            }
        }
    }
}

/*
 * Formerly a whole-flag-word column overwrite. Loc/terrain collision now use
 * place-time LinkBelow shift; this remains so build's call site is stable.
 */
static void
apply_bridges(void)
{
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
        if( !g_collision[level] )
            continue;
        collision_map_reset(g_collision[level]);
        /* The router's window is the era's, not the scene's — see
         * ToriRS_FeatureTable.route_window_tiles and
         * docs/OSRS_PATHING_LOS.md §5.1. */
        collision_map_set_route_window(g_collision[level],
                                       mock230_scene_features()->route_window_tiles);
    }
    memset(g_link_below, 0, sizeof(g_link_below));

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

    apply_bridges();

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
mock230_scene_find_loc_exact(
    int x,
    int z,
    int level,
    int shape)
{
    for( int i = 0; i < g_loc_count; i++ )
    {
        struct Mock230SceneLoc* loc = &g_locs[i];

        /* The loc's own corner and its own shape, and *not* its footprint: this
         * is the mutation lookup, and a mutation addresses the loc the wire
         * names rather than whatever a click landed on. An inactive slot still
         * matches when it is a static tombstone, which is how a `loc_del` is
         * undone. */
        if( loc->x != x || loc->z != z || loc->level != level || loc->shape != shape )
            continue;
        if( !loc->active && !loc->is_static )
            continue;
        return i;
    }
    return -1;
}

int
mock230_scene_replace_loc(
    int slot,
    int loc_id,
    int angle)
{
    struct Mock230SceneLoc* loc = mock230_scene_loc(slot);
    const struct RSCache_Dat2ConfigLoc* config;

    if( !loc )
        return 0;
    config = loc_config(loc_id);
    if( !config )
        return 0;

    /* An *inactive* slot is revived rather than refused, which is how a
     * `loc_del` on a map-square loc is undone: the tombstone stays in the array
     * (see mock230_scene_add_loc) precisely so the tile can be put back without
     * the caller having to know whether it is currently there. Its collision is
     * already gone, so there is nothing to take away first. */
    if( loc->active )
        apply_loc_collision(loc, 0);
    loc->active = 1;
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
    apply_loc_collision(loc, 1);
    return 1;
}

int
mock230_scene_add_loc(
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(loc_id);
    struct Mock230SceneLoc* loc;

    if( !config )
        return -1;
    if( !mock230_scene_contains(x, z) )
        return -1;

    /*
     * A slot a `loc_del` freed is reused before the array grows. Slots are
     * never compacted — a script can hold one across a suspend — so without
     * this a world that adds and removes a loc on a timer grows a slot per
     * cycle for as long as it runs.
     */
    loc = NULL;
    for( int i = 0; i < g_loc_count; i++ )
    {
        /* A *static* loc's slot is never reused, even when it is inactive: it
         * is a tombstone. The map square still has a loc on that tile, so
         * "removed" is a fact about the world that outlives the removal, and
         * handing the slot to something else would lose it. */
        if( !g_locs[i].active && !g_locs[i].is_static )
        {
            loc = &g_locs[i];
            break;
        }
    }
    if( !loc )
    {
        if( g_loc_count == g_loc_capacity )
        {
            int capacity = g_loc_capacity ? g_loc_capacity * 2 : 1024;
            struct Mock230SceneLoc* grown = realloc(g_locs, (size_t)capacity * sizeof(*grown));

            if( !grown )
                return -1;
            g_locs = grown;
            g_loc_capacity = capacity;
        }
        loc = &g_locs[g_loc_count++];
    }

    loc->loc_id = loc_id;
    loc->shape = shape;
    loc->angle = angle;
    loc->x = x;
    loc->z = z;
    loc->level = level;
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
    loc->active = 1;
    /* Not from the map square, so its slot is free to be handed out again once
     * something removes it. */
    loc->is_static = 0;
    apply_loc_collision(loc, 1);
    return (int)(loc - g_locs);
}

int
mock230_scene_remove_loc(int slot)
{
    struct Mock230SceneLoc* loc = mock230_scene_loc(slot);

    if( !loc || !loc->active )
        return 0;
    apply_loc_collision(loc, 0);
    loc->active = 0;
    return 1;
}

/*
 * `mock230_scene_next_changed_loc` was here, and with it the `changed` flag on
 * every scene loc.
 *
 * It had one caller — phase 10, re-sending opened doors after a REBUILD_NORMAL —
 * and it could never have worked: `mock230_scene_build` calls
 * `mock230_scene_free` first, so by the time the rebuild's re-send ran, the
 * array carrying the flags had been freed and re-read from the cache. The
 * durable record of a loc mutation is `struct Mock230ZoneLoc` in the ZoneMap
 * now, which is also what puts collision back (`mock230_world_locs_reapply`).
 */

/* ------------------------------------------------------------------ */
/* Routing                                                             */
/* ------------------------------------------------------------------ */

/* Direction index -> tile delta, in the client's World_CoordStep numbering:
 * 0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE. */
static const int k_step_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int k_step_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

void
mock230_scene_set_features(struct ToriRS_FeatureTable const* features)
{
    g_features = features;
}

struct ToriRS_FeatureTable const*
mock230_scene_features(void)
{
    if( !g_features )
        g_features = ToriRS_Features_OSRS();
    return g_features;
}

int
mock230_scene_can_step(
    int level,
    int x,
    int z,
    int dir)
{
    return mock230_scene_can_step_extra(level, x, z, dir, 0);
}

int
mock230_scene_can_step_extra(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag)
{
    return mock230_scene_can_step_typed(level, x, z, dir, extra_flag, COLL_TYPE_NORMAL);
}

int
mock230_scene_can_step_typed(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag,
    int coll_type)
{
    if( dir < 0 || dir > 7 )
        return 0;
    return mock230_scene_can_travel_typed(
        level, x, z, k_step_dx[dir], k_step_dz[dir], 1, extra_flag, coll_type);
}

int
mock230_scene_can_travel(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag)
{
    return mock230_scene_can_travel_typed(
        level, x, z, offset_x, offset_z, size, extra_flag, COLL_TYPE_NORMAL);
}

int
mock230_scene_can_travel_typed(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map || size < 1 )
        return 0;
    if( !mock230_scene_contains(x, z) )
        return 0;
    if( !mock230_scene_contains(x + offset_x, z + offset_z) )
        return 0;
    return collision_map_can_travel_typed(
        map, x - g_base_x, z - g_base_z, offset_x, offset_z, size, extra_flag, coll_type);
}

int
mock230_scene_line_of_sight(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh,
    int extra)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map )
        return 0;
    if( !mock230_scene_contains(sx, sz) || !mock230_scene_contains(dx, dz) )
        return 0;
    return collision_map_line_of_sight(
        map, sx - g_base_x, sz - g_base_z, dx - g_base_x, dz - g_base_z, sw, sh, dw, dh,
        extra);
}

int
mock230_scene_line_of_walk(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh,
    int extra)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map )
        return 0;
    if( !mock230_scene_contains(sx, sz) || !mock230_scene_contains(dx, dz) )
        return 0;
    return collision_map_line_of_walk(
        map, sx - g_base_x, sz - g_base_z, dx - g_base_x, dz - g_base_z, sw, sh, dw, dh,
        extra);
}

int
mock230_scene_checkvis(
    int checkvis,
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z)
{
    /* HuntVis.OFF / LINEOFSIGHT / LINEOFWALK — LostCity HuntVis.ts. */
    if( checkvis == 1 )
        return mock230_scene_line_of_sight(level, from_x, from_z, to_x, to_z, 1, 1, 1, 1,
                                           0);
    if( checkvis == 2 )
        return mock230_scene_line_of_walk(level, from_x, from_z, to_x, to_z, 1, 1, 1, 1, 0);
    return 1;
}

int
mock230_scene_approached(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map )
        return 0;
    if( !mock230_scene_contains(sx, sz) || !mock230_scene_contains(dx, dz) )
        return 0;
    return collision_map_approached(
        map, sx - g_base_x, sz - g_base_z, dx - g_base_x, dz - g_base_z, sw, sh, dw, dh);
}

int
mock230_scene_approached_pvp(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map )
        return 0;
    if( !mock230_scene_contains(sx, sz) || !mock230_scene_contains(dx, dz) )
        return 0;
    if( !mock230_scene_features()->los_symmetric_pvp )
        return mock230_scene_approached(level, sx, sz, dx, dz, sw, sh, dw, dh);
    return collision_map_approached_symmetric(
        map, sx - g_base_x, sz - g_base_z, dx - g_base_x, dz - g_base_z, sw, sh, dw, dh);
}

void
mock230_scene_change_occupancy(
    int level,
    int x,
    int z,
    int size,
    int mask,
    int add)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map || size < 1 || mask == 0 )
        return;
    if( !mock230_scene_contains(x, z) )
        return;
    collision_map_change_square(map, x - g_base_x, z - g_base_z, size, mask, add);
}

int
mock230_scene_naive_path(
    int level,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag,
    unsigned* rng,
    int* out_x,
    int* out_z)
{
    struct CollisionMap* map = mock230_scene_collision(level);
    int sx;
    int sz;
    int ok;

    assert(out_x && out_z);
    if( !map )
        return 0;
    if( !mock230_scene_contains(src_x, src_z) || !mock230_scene_contains(dest_x, dest_z) )
        return 0;
    ok = collision_map_naive_path(
        map, src_x - g_base_x, src_z - g_base_z, dest_x - g_base_x, dest_z - g_base_z,
        src_width, src_height, dest_width, dest_height, extra_flag, rng, &sx, &sz);
    if( !ok )
        return 0;
    *out_x = sx + g_base_x;
    *out_z = sz + g_base_z;
    if( !mock230_scene_contains(*out_x, *out_z) )
        return 0;
    return 1;
}

/* Straight-line interpolation. Used ONLY when there is no collision map at
 * all (cache missing), so a caller never has to know. An out-of-scene endpoint
 * with a loaded map must refuse rather than walk through walls. */
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

/* LocType.forceapproach rotation into the placed frame — same formula the
 * client applies at scenery register time. The value is a 4-bit DirectionFlag
 * nibble; mask before shifting so high bits (seen on some OSRS records, e.g.
 * cooksquestrange=29) cannot leak into the rotated result via the right-shift. */
static int
rotate_force_approach(int force_approach, int angle)
{
    force_approach &= 0xf;
    angle &= 3;
    if( angle == 0 )
        return force_approach;
    return ((force_approach << angle) & 0xf) + (force_approach >> (4 - angle));
}

void
mock230_scene_loc_approach(
    int slot,
    struct CollisionApproach* out)
{
    struct Mock230SceneLoc* loc;
    struct ToriRS_FeatureTable const* features = mock230_scene_features();
    const struct RSCache_Dat2ConfigLoc* config;
    int shape;
    int sized;
    int force;

    assert(out);
    memset(out, 0, sizeof(*out));
    out->mover_size = 1;
    out->kind = COLL_APPROACH_EXACT;

    loc = mock230_scene_loc(slot);
    if( !loc || !loc->active )
        return;

    shape = loc->shape;
    sized = shape == RSCACHE_LOC_SHAPE_SCENERY || shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ||
            shape == RSCACHE_LOC_SHAPE_FLOOR_DECORATION;
    config = loc_config(loc->loc_id);
    force = config ? rotate_force_approach(config->force_approach, loc->angle) : 0;

    if( features->approach_model != TORIRS_APPROACH_RECT )
    {
        out->kind = COLL_APPROACH_LEGACY_SHAPE;
        if( sized )
        {
            out->loc_width = loc->size_x > 0 ? loc->size_x : 1;
            out->loc_length = loc->size_z > 0 ? loc->size_z : 1;
            out->forceapproach = force;
        }
        else
        {
            out->loc_angle = loc->angle;
            out->loc_shape = shape + 1;
        }
        return;
    }

    collision_approach_from_shape(
        shape, loc->angle, loc->size_x, loc->size_z, force, 1, out);
}

void
mock230_scene_npc_approach(
    int size,
    struct CollisionApproach* out)
{
    struct ToriRS_FeatureTable const* features = mock230_scene_features();
    int s;

    assert(out);
    s = features->npc_approach_uses_size && size > 0 ? size : 1;
    if( features->approach_model == TORIRS_APPROACH_RECT )
        collision_approach_from_shape(-2, 0, s, s, 0, 1, out);
    else
    {
        memset(out, 0, sizeof(*out));
        out->mover_size = 1;
        out->kind = COLL_APPROACH_LEGACY_SHAPE;
        out->loc_width = s;
        out->loc_length = s;
    }
}

void
mock230_scene_obj_approach(
    int retry_adjacent,
    struct CollisionApproach* out)
{
    struct ToriRS_FeatureTable const* features = mock230_scene_features();

    assert(out);
    memset(out, 0, sizeof(*out));
    out->mover_size = 1;
    if( !retry_adjacent )
    {
        out->kind = COLL_APPROACH_EXACT;
        return;
    }
    if( features->approach_model == TORIRS_APPROACH_RECT )
    {
        /* Adjacent pickup retry: exclusive 1x1 so standing on the pile is the
         * EXACT attempt, and this arm only accepts a cardinal neighbour. */
        collision_approach_from_shape(-2, 0, 1, 1, 0, 1, out);
    }
    else
    {
        out->kind = COLL_APPROACH_LEGACY_SHAPE;
        out->loc_width = 1;
        out->loc_length = 1;
    }
}

void
mock230_scene_op_nearest_opts(struct CollisionNearestOpts* out)
{
    struct ToriRS_FeatureTable const* features = mock230_scene_features();

    assert(out);
    out->range = features->op_click_nearest_range;
    out->max_dist = 100;
    out->rank_by_rect_distance = features->nearest_ranks_by_rect_distance;
}

static void
route_trace(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    int steps,
    int nearest,
    int arrive_x,
    int arrive_z)
{
    if( !getenv("MOCK230_VERBOSE") )
        return;
    fprintf(stderr,
            "mock230: route level=%d from=%d,%d to=%d,%d steps=%d nearest=%d arrive=%d,%d\n",
            level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
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
    int nearest = 0;
    int arrive_x = to_x;
    int arrive_z = to_z;

    if( from_x == to_x && from_z == to_z )
        return 0;

    /* No collision at all (cache missing): the announced open-field fallback. */
    if( !map )
        return route_straight(from_x, from_z, to_x, to_z, path_x, path_z, max_steps);

    /* An endpoint outside the built scene is unreachable — do not straight-line
     * through walls the map has not been asked about. */
    if( !mock230_scene_contains(from_x, from_z) || !mock230_scene_contains(to_x, to_z) )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }

    {
        /* Ground clicks get the 3x3 nearest ring — same as the client's
         * collision_map_try_route. */
        struct CollisionNearestOpts const ring = {
            .range = 1,
            .max_dist = 100,
            .rank_by_rect_distance = 0,
        };
        steps = collision_map_route_tiles(
            map, from_x - g_base_x, from_z - g_base_z, to_x - g_base_x, to_z - g_base_z, NULL,
            &ring, path_x, path_z, max_steps, &nearest, &arrive_x, &arrive_z);
    }
    if( steps < 0 )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }
    for( int i = 0; i < steps; i++ )
    {
        path_x[i] += g_base_x;
        path_z[i] += g_base_z;
    }
    arrive_x += g_base_x;
    arrive_z += g_base_z;
    route_trace(level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
    return steps;
}

int
mock230_scene_route_op(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    struct CollisionApproach const* approach,
    int* path_x,
    int* path_z,
    int max_steps,
    int* out_arrive_x,
    int* out_arrive_z)
{
    struct CollisionMap* map = mock230_scene_collision(level);
    struct CollisionNearestOpts nearest_opts;
    int steps;
    int nearest = 0;
    int arrive_x = to_x;
    int arrive_z = to_z;

    assert(approach);
    assert(path_x && path_z);

    if( from_x == to_x && from_z == to_z &&
        ( !map || collision_map_reached(
                      map, from_x - g_base_x, from_z - g_base_z, to_x - g_base_x, to_z - g_base_z,
                      approach) ) )
    {
        if( out_arrive_x )
            *out_arrive_x = from_x;
        if( out_arrive_z )
            *out_arrive_z = from_z;
        return 0;
    }

    if( !map )
        return route_straight(from_x, from_z, to_x, to_z, path_x, path_z, max_steps);

    if( !mock230_scene_contains(from_x, from_z) || !mock230_scene_contains(to_x, to_z) )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }

    mock230_scene_op_nearest_opts(&nearest_opts);
    steps = collision_map_route_tiles(
        map, from_x - g_base_x, from_z - g_base_z, to_x - g_base_x, to_z - g_base_z, approach,
        &nearest_opts, path_x, path_z, max_steps, &nearest, &arrive_x, &arrive_z);
    if( steps < 0 )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }
    for( int i = 0; i < steps; i++ )
    {
        path_x[i] += g_base_x;
        path_z[i] += g_base_z;
    }
    arrive_x += g_base_x;
    arrive_z += g_base_z;
    if( out_arrive_x )
        *out_arrive_x = arrive_x;
    if( out_arrive_z )
        *out_arrive_z = arrive_z;
    route_trace(level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
    return steps;
}

int
mock230_scene_reached(
    int level,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    struct CollisionMap* map = mock230_scene_collision(level);

    if( !map )
    {
        /* No collision: fall back to Chebyshev adjacent / exact for EXACT. */
        int dx = x - dst_x;
        int dz = z - dst_z;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        if( !approach || approach->kind == COLL_APPROACH_EXACT )
            return dx == 0 && dz == 0;
        return dx <= 1 && dz <= 1 && !(dx == 1 && dz == 1);
    }
    if( !mock230_scene_contains(x, z) || !mock230_scene_contains(dst_x, dst_z) )
        return 0;
    return collision_map_reached(
        map, x - g_base_x, z - g_base_z, dst_x - g_base_x, dst_z - g_base_z, approach);
}
