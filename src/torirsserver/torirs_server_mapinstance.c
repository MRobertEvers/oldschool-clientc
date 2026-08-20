/*
 * The map-instance registry. The design, the pool policy and the two rotation
 * directions are all written down in torirs_server_mapinstance.h — read that first.
 *
 * What is here: a fixed table of reservations, a lazily-built bitmap of which
 * map squares the cache ships, and the coordinate arithmetic. No wire, no
 * collision, no scripts; those call in.
 */

#include "torirs_server_mapinstance.h"

#include "torirs_server.h"

#include <rscache.h>

#include <datatypes/maps.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Where the free-square sweep starts, in map squares.
 *
 * Not a coordinate this engine invents: Kronos's instance pool is exactly this
 * band (`DynamicMap.load` keeps a region only when `region.baseX >= 6400`, and
 * 6400 >> 6 is 100), and 2009scape's `findZoneBorders` scans past the real map
 * for the same reason — an instance next door to live map is an instance whose
 * edge a player can see into.
 *
 * It is a scan *start*, not an allocation base: `mapinstance_square_free`
 * consults the cache, so a cache that does ship squares up here simply loses
 * them from the pool instead of handing out occupied map. Measured on
 * cache.osrs239: 2,934 squares, all within x 15..98, so the whole band is free.
 */
enum
{
    MAPINSTANCE_SCAN_X0 = 100,
    /*
     * ...and one square in from the SOUTH edge, for a reason the x band does
     * not have.
     *
     * The build area is 13x13 zones centred on the player, so its south-west
     * corner is `(zone - 6) * 8`. An instance handed out at map square z = 0
     * with the player anywhere in the square's lower six zones produces a
     * NEGATIVE base -- the client is told its scene starts below the map. It
     * accepts the rebuild, allocates all 104x104 tiles, places locs and npcs
     * from the packets that follow, and draws no terrain and no minimap,
     * because the squares the descriptors reference never resolve. A working
     * instance and a broken one differ only by which square the pool handed
     * out, so the same content renders on one entry and not the next.
     *
     * One square is enough: it makes the smallest possible base 64 - 48 = 16.
     */
    MAPINSTANCE_SCAN_Z0 = 1,
    MAPINSTANCE_SQUARES = 256,
};

struct ToriRSServerMapInstance
{
    int active;
    /** Script-visible uid of the player who allocated this reservation. */
    int owner_uid;
    /** Content-owned, session-local switches shared by everyone in the map. */
    uint32_t flags;
    /** Content-owned integer registers shared by everyone in the map. */
    int vars[TORIRSSERVER_MAPINSTANCE_VARS];
    /** Absolute south-west tile. */
    int base_x, base_z;
    int zone_w, zone_h;
    /** Reserved footprint in map squares, which is what the pool hands out. */
    int square_x, square_z, square_w, square_h;
    int built;
    /** Bumped on every build (see ToriRSServer_MapInstanceGeneration). */
    int generation;
    struct ToriRSServerMapInstanceZone
        zones[TORIRSSERVER_MAPINSTANCE_LEVELS][TORIRSSERVER_MAPINSTANCE_ZONES][TORIRSSERVER_MAPINSTANCE_ZONES];
};

static struct ToriRSServerMapInstance g_instances[TORIRSSERVER_MAPINSTANCE_MAX];

/*
 * Monotonic across the whole pool, never reset while the server runs.
 *
 * Per-instance would not answer the question that matters: the allocator hands
 * the first free slot the first free block, so freeing an instance and
 * allocating another gives back the SAME handle at the SAME square. A scene
 * identity built from (handle, square) therefore cannot distinguish "still the
 * instance you are standing in" from "a fresh one built on its grave", and the
 * second Inferno run played against the first run's crumbled arena
 * (docs/ORANGE_WEDGE.md §18). A global counter makes every build a distinct
 * scene.
 */
static int g_instance_generation;

/* Which map squares the cache ships. Swept once per process: the reference
 * table does not change under a running server, and the sweep is 65,536 lookups
 * into a table with 2,934 entries. */
static uint8_t g_square_used[MAPINSTANCE_SQUARES][MAPINSTANCE_SQUARES];
static int g_pool_scanned;

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

static void
mapinstance_scan_pool(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    char resolved[512];
    int shipped = 0;

    if( g_pool_scanned )
        return;
    /* Latched before the early returns below: a cache that cannot be opened has
     * an empty pool, and re-sweeping it every alloc would re-print the warning
     * every time. */
    g_pool_scanned = 1;

    snprintf(resolved, sizeof(resolved), "%s", cache_dir);
    disk = RSCache_Dat2DiskNewFromDirectory(resolved);
    if( !disk )
    {
        /* Same ../ fallback as every other cache loader here: run from src/ or
         * from the repo root. */
        snprintf(resolved, sizeof(resolved), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(resolved);
    }
    if( !disk )
    {
        fprintf(stderr,
                "torirsserver: no cache at %s — map instances unavailable\n",
                cache_dir);
        /* Every square reads as used, so alloc fails loudly rather than handing
         * out coordinates over map it cannot see. */
        memset(g_square_used, 1, sizeof(g_square_used));
        return;
    }

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;
    RSCache_Dat2DiskSetProfile(disk, &profile);

    for( int x = 0; x < MAPINSTANCE_SQUARES; x++ )
    {
        for( int z = 0; z < MAPINSTANCE_SQUARES; z++ )
        {
            if( !RSCache_MapSquareExists(disk, x, z) )
                continue;
            g_square_used[x][z] = 1;
            shipped++;
        }
    }

    RSCache_Dat2DiskFree(disk);
    if( getenv("TORIRSSERVER_VERBOSE") )
        fprintf(stderr,
                "torirsserver: map-instance pool — %d squares shipped by the cache, sweep starts at "
                "map x %d\n",
                shipped, MAPINSTANCE_SCAN_X0);
}

static int
mapinstance_square_free(
    int x,
    int z)
{
    if( x < 0 || z < 0 || x >= MAPINSTANCE_SQUARES || z >= MAPINSTANCE_SQUARES )
        return 0;
    return !g_square_used[x][z];
}

static int
mapinstance_block_free(
    int x,
    int z,
    int w,
    int h)
{
    for( int i = 0; i < w; i++ )
    {
        for( int j = 0; j < h; j++ )
        {
            if( !mapinstance_square_free(x + i, z + j) )
                return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Rotation                                                            */
/* ------------------------------------------------------------------ */

void
ToriRSServer_MapInstanceRotateToSrc(
    int rotation,
    int dx,
    int dz,
    int* out_sx,
    int* out_sz)
{
    assert(out_sx && out_sz);
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

void
ToriRSServer_MapInstanceRotateToDst(
    int rotation,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int* out_dx,
    int* out_dz)
{
    assert(out_dx && out_dz);
    if( size_x < 1 )
        size_x = 1;
    if( size_z < 1 )
        size_z = 1;

    /*
     * The inverse of _to_src, plus the extent correction.
     *
     * Inverting the point map alone would place a multi-tile loc by the corner
     * that *used* to be its south-west one, which after a quarter-turn is its
     * south-east or north-west. Subtracting the extent that now runs backwards
     * along the axis is what keeps the footprint over the same tiles — which is
     * the difference between a 1x2 table sitting where it was drawn and sitting
     * one tile off it.
     */
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

/* ------------------------------------------------------------------ */
/* Reservations                                                        */
/* ------------------------------------------------------------------ */

void
ToriRSServer_MapInstanceReset(void)
{
    memset(g_instances, 0, sizeof(g_instances));
}

/*
 * Handles are 1-based, and 0 is the only "no instance" value the whole surface
 * uses — alloc failure, find miss and a dead handle all read as 0.
 *
 * That is for content's sake rather than C's: a handle lives in a varp between
 * the tick that allocates a house and the tick that enters it, varps start at 0,
 * and a 0-based handle would make "slot 0" and "never allocated" the same int.
 */
static struct ToriRSServerMapInstance*
mapinstance_get(int handle)
{
    if( handle < 1 || handle > TORIRSSERVER_MAPINSTANCE_MAX )
        return NULL;
    if( !g_instances[handle - 1].active )
        return NULL;
    return &g_instances[handle - 1];
}

/** Does a live instance already hold this square? The cache bitmap cannot say —
 *  it describes the cache, and a reservation is a fact about this session. */
static int
mapinstance_square_reserved(
    int x,
    int z)
{
    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        struct ToriRSServerMapInstance* inst = &g_instances[i];

        if( !inst->active )
            continue;
        if( x >= inst->square_x && x < inst->square_x + inst->square_w &&
            z >= inst->square_z && z < inst->square_z + inst->square_h )
            return 1;
    }
    return 0;
}

static int
mapinstance_block_available(
    int x,
    int z,
    int w,
    int h)
{
    if( !mapinstance_block_free(x, z, w, h) )
        return 0;
    for( int i = 0; i < w; i++ )
    {
        for( int j = 0; j < h; j++ )
        {
            if( mapinstance_square_reserved(x + i, z + j) )
                return 0;
        }
    }
    return 1;
}

int
ToriRSServer_MapInstanceAlloc(
    const char* cache_dir,
    int zone_w,
    int zone_h)
{
    int slot = -1;
    int square_w;
    int square_h;

    assert(cache_dir);
    if( zone_w < 1 || zone_h < 1 || zone_w > TORIRSSERVER_MAPINSTANCE_ZONES ||
        zone_h > TORIRSSERVER_MAPINSTANCE_ZONES )
        return 0;

    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX && slot < 0; i++ )
    {
        if( !g_instances[i].active )
            slot = i;
    }
    if( slot < 0 )
        return 0;

    mapinstance_scan_pool(cache_dir);

    square_w = (zone_w + TORIRSSERVER_MAPINSTANCE_SQUARE_ZONES - 1) / TORIRSSERVER_MAPINSTANCE_SQUARE_ZONES;
    square_h = (zone_h + TORIRSSERVER_MAPINSTANCE_SQUARE_ZONES - 1) / TORIRSSERVER_MAPINSTANCE_SQUARE_ZONES;

    for( int x = MAPINSTANCE_SCAN_X0; x + square_w <= MAPINSTANCE_SQUARES; x++ )
    {
        for( int z = MAPINSTANCE_SCAN_Z0; z + square_h <= MAPINSTANCE_SQUARES; z++ )
        {
            struct ToriRSServerMapInstance* inst;

            if( !mapinstance_block_available(x, z, square_w, square_h) )
                continue;

            inst = &g_instances[slot];
            memset(inst, 0, sizeof(*inst));
            inst->active = 1;
            inst->square_x = x;
            inst->square_z = z;
            inst->square_w = square_w;
            inst->square_h = square_h;
            inst->base_x = x * 64;
            inst->base_z = z * 64;
            inst->zone_w = zone_w;
            inst->zone_h = zone_h;
            if( getenv("TORIRSSERVER_VERBOSE") )
                fprintf(stderr,
                        "torirsserver: map instance %d reserved %dx%d zones at %d,%d "
                        "(map square %d,%d)\n",
                        slot + 1, zone_w, zone_h, inst->base_x, inst->base_z, x, z);
            return slot + 1;
        }
    }
    return 0;
}

int
ToriRSServer_MapInstanceSetOwner(
    int handle,
    int player_uid)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst || player_uid < 0 )
        return 0;
    inst->owner_uid = player_uid;
    return 1;
}

int
ToriRSServer_MapInstanceOwner(int handle)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    return inst ? inst->owner_uid : 0;
}

int
ToriRSServer_MapInstanceFindOwner(
    int player_uid,
    int required_flags)
{
    uint32_t required = (uint32_t)required_flags;

    if( player_uid <= 0 || required_flags < 0 )
        return 0;
    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        const struct ToriRSServerMapInstance* inst = &g_instances[i];

        if( inst->active && inst->owner_uid == player_uid &&
            (inst->flags & required) == required )
            return i + 1;
    }
    return 0;
}

/*
 * The first live reservation carrying every requested flag, whoever owns it.
 *
 * Sibling of `ToriRSServer_MapInstanceFindOwner` above and deliberately NOT the
 * same question. That one refuses `player_uid <= 0` because "an instance I
 * own" needs an owner; this one exists for the player who wants to join an
 * instance somebody ELSE owns and has no way to name them. A zero mask is
 * refused rather than matching everything: "any instance at all" is not a
 * question content should be able to ask, because the answer would include
 * every unrelated house, raid and minigame in the pool.
 *
 * Lowest handle first, so the answer is stable across calls in one tick.
 */
int
ToriRSServer_MapInstanceFindFlagged(int required_flags)
{
    uint32_t required = (uint32_t)required_flags;

    if( required_flags <= 0 )
        return 0;
    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        const struct ToriRSServerMapInstance* inst = &g_instances[i];

        if( inst->active && (inst->flags & required) == required )
            return i + 1;
    }
    return 0;
}

int
ToriRSServer_MapInstanceFlagGet(
    int handle,
    int mask)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst || mask <= 0 )
        return 0;
    return (inst->flags & (uint32_t)mask) != 0;
}

int
ToriRSServer_MapInstanceFlagSet(
    int handle,
    int mask,
    int enabled)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst || mask <= 0 )
        return 0;
    if( enabled )
        inst->flags |= (uint32_t)mask;
    else
        inst->flags &= ~(uint32_t)mask;
    return 1;
}

int
ToriRSServer_MapInstanceVarGet(
    int handle,
    int slot)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst || slot < 0 || slot >= TORIRSSERVER_MAPINSTANCE_VARS )
        return 0;
    return inst->vars[slot];
}

int
ToriRSServer_MapInstanceVarSet(
    int handle,
    int slot,
    int value)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst || slot < 0 || slot >= TORIRSSERVER_MAPINSTANCE_VARS )
        return 0;
    inst->vars[slot] = value;
    return 1;
}

int
ToriRSServer_MapInstanceClearOwner(int player_uid)
{
    int cleared = 0;

    if( player_uid <= 0 )
        return 0;
    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        struct ToriRSServerMapInstance* inst = &g_instances[i];

        if( inst->active && inst->owner_uid == player_uid )
        {
            inst->owner_uid = 0;
            cleared++;
        }
    }
    return cleared;
}

int
ToriRSServer_MapInstanceSetchunk(
    int handle,
    int level,
    int zone_x,
    int zone_z,
    int src_x,
    int src_z,
    int src_level,
    int rotation)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);
    struct ToriRSServerMapInstanceZone* zone;

    if( !inst )
        return 0;
    if( level < 0 || level >= TORIRSSERVER_MAPINSTANCE_LEVELS )
        return 0;
    if( zone_x < 0 || zone_x >= inst->zone_w || zone_z < 0 || zone_z >= inst->zone_h )
        return 0;
    if( src_level < 0 || src_level >= TORIRSSERVER_MAPINSTANCE_LEVELS )
        return 0;
    if( src_x < 0 || src_z < 0 )
        return 0;

    zone = &inst->zones[level][zone_x][zone_z];
    zone->set = 1;
    /* Floored to the zone: content names a tile it can point at (a landmark,
     * `movecoord`'d), not a zone index it would have to compute. */
    zone->src_zone_x = src_x >> 3;
    zone->src_zone_z = src_z >> 3;
    zone->src_level = src_level;
    zone->rotation = rotation & 3;
    return 1;
}

int
ToriRSServer_MapInstanceBuild(int handle)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst )
        return 0;
    inst->built = 1;
    inst->generation = ++g_instance_generation;
    return 1;
}

int
ToriRSServer_MapInstanceGeneration(int handle)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    return inst ? inst->generation : 0;
}

int
ToriRSServer_MapInstanceBase(
    int handle,
    int* out_x,
    int* out_z)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    assert(out_x && out_z);
    if( !inst )
        return 0;
    *out_x = inst->base_x;
    *out_z = inst->base_z;
    return 1;
}

int
ToriRSServer_MapInstanceBounds(
    int handle,
    int* out_x,
    int* out_z,
    int* out_width,
    int* out_height)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    assert(out_x && out_z && out_width && out_height);
    if( !inst )
        return 0;
    *out_x = inst->base_x;
    *out_z = inst->base_z;
    *out_width = inst->square_w * 64;
    *out_height = inst->square_h * 64;
    return 1;
}

int
ToriRSServer_MapInstanceFree(int handle)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);

    if( !inst )
        return 0;
    /* Logged for the same reason the alloc is: a reservation that is never
     * released is invisible from inside the game — the pool simply stops
     * answering after TORIRSSERVER_MAPINSTANCE_MAX entries, several sessions later
     * and in whatever content asked next. The pair of lines is what makes a
     * leak a thing you can read off one run. */
    if( getenv("TORIRSSERVER_VERBOSE") )
        fprintf(stderr,
                "torirsserver: map instance %d released (map square %d,%d)\n",
                handle, inst->square_x, inst->square_z);
    memset(inst, 0, sizeof(*inst));
    return 1;
}

int
ToriRSServer_MapInstanceLiveCount(void)
{
    int live = 0;

    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        if( g_instances[i].active )
            live++;
    }
    return live;
}

int
ToriRSServer_MapInstanceFind(
    int x,
    int z)
{
    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        struct ToriRSServerMapInstance* inst = &g_instances[i];

        if( !inst->active )
            continue;
        /* The *reserved* footprint, not the requested zone count: a 5-zone
         * instance still owns the whole map square, and a player who walks off
         * its assembled edge into the void is still inside the instance. Saying
         * otherwise would make `map_instance_find` answer -1 for someone who is
         * demonstrably not in the ordinary world. */
        if( x >= inst->base_x && x < inst->base_x + inst->square_w * 64 &&
            z >= inst->base_z && z < inst->base_z + inst->square_h * 64 )
            return i + 1;
    }
    return 0;
}

int
ToriRSServer_MapInstanceSourceTile(
    int handle,
    int level,
    int x,
    int z,
    int* out_x,
    int* out_z)
{
    struct ToriRSServerMapInstance* inst = mapinstance_get(handle);
    const struct ToriRSServerMapInstanceZone* zone;
    int zone_x;
    int zone_z;

    assert(out_x && out_z);
    if( !inst )
        return 0;
    /* Zone index within the instance, from the SW corner. `zone_w`/`zone_h` and
     * not the reserved square footprint: the reservation is rounded up to whole
     * squares and the rounding is not addressable, so a tile out there has no
     * source even though `_find` (rightly) still calls it inside. */
    zone_x = (x - inst->base_x) >> 3;
    zone_z = (z - inst->base_z) >> 3;
    if( zone_x < 0 || zone_x >= inst->zone_w || zone_z < 0 || zone_z >= inst->zone_h )
        return 0;
    if( level < 0 || level >= TORIRSSERVER_MAPINSTANCE_LEVELS )
        return 0;

    zone = &inst->zones[level][zone_x][zone_z];
    if( !zone->set )
        zone = &inst->zones[0][zone_x][zone_z];
    if( !zone->set )
        return 0;

    /* Back to absolute tiles. `src_zone_*` was floored to the zone by
     * `_setchunk`, so this is the source zone's south-west corner and not the
     * tile the destination one maps to — which is what a caller asking "where
     * in the world is this" wants, and is rotation-independent. Anyone needing
     * the exact tile wants `ToriRSServer_MapInstanceRotateToSrc`. */
    *out_x = zone->src_zone_x << 3;
    *out_z = zone->src_zone_z << 3;
    return 1;
}

int
ToriRSServer_MapInstanceWindow(
    int zone_x,
    int zone_z,
    struct ToriRSServerMapInstanceWindow* out)
{
    /* The scene's south-west zone. Same rule as ToriRSServer_SceneOrigin, in zones
     * rather than tiles, and it has to be the same or the collision build and
     * the encoder disagree about which descriptor is which. */
    int sw_zone_x = zone_x - 6;
    int sw_zone_z = zone_z - 6;

    assert(out);
    memset(out, 0, sizeof(*out));

    for( int i = 0; i < TORIRSSERVER_MAPINSTANCE_MAX; i++ )
    {
        struct ToriRSServerMapInstance* inst = &g_instances[i];
        int inst_zone_x;
        int inst_zone_z;

        if( !inst->active || !inst->built )
            continue;
        inst_zone_x = inst->base_x >> 3;
        inst_zone_z = inst->base_z >> 3;

        for( int level = 0; level < TORIRSSERVER_MAPINSTANCE_LEVELS; level++ )
        {
            for( int zx = 0; zx < inst->zone_w; zx++ )
            {
                for( int zz = 0; zz < inst->zone_h; zz++ )
                {
                    int wx = inst_zone_x + zx - sw_zone_x;
                    int wz = inst_zone_z + zz - sw_zone_z;

                    if( !inst->zones[level][zx][zz].set )
                        continue;
                    if( wx < 0 || wx >= TORIRSSERVER_MAPINSTANCE_SCENE_ZONES ||
                        wz < 0 || wz >= TORIRSSERVER_MAPINSTANCE_SCENE_ZONES )
                        continue;
                    out->zones[level][wx][wz] = inst->zones[level][zx][zz];
                    out->set_count++;
                }
            }
        }
    }
    return out->set_count;
}
