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

#include "torirs_server_scene.h"

#include "torirs_server.h"
#include "torirs_server_mapinstance.h"

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

/* Era feature table: approach model + op-click nearest fallback. Resolved once
 * at boot; defaults to OSRS when nothing has been set yet (this server boots
 * against an OldSchool cache). */
static struct ToriRS_FeatureTable const* g_features;

/* Loc placement tracing, armed by TORIRS_TRACE_LOC (a loc id) in the
 * environment; resolved lazily at the first scene build. */
static int g_trace_loc = -1;

/*
 * One 104x104 window per watcher.
 *
 * This used to be a single set of file-scope globals — one scene for the whole
 * world — which put a hard ceiling on multiplayer: two players more than ~70
 * tiles apart fought over where the one window sat. Now the world keeps a pool
 * of windows: index 0 is the ROOT window (world logic with no player in hand —
 * npc wander, hunts, timers — and the anchor WorldInit builds at), and index
 * 1+pid is that player's own window, centred by the per-player rebuild.
 *
 * Windows that overlap are kept COHERENT: they are built from the same cache,
 * entity occupancy broadcasts into every built window covering the tile
 * (ToriRSServer_SceneChangeOccupancy), and loc mutations are replayed into
 * every covering window (ToriRSServer_WorldLocSetOps / WorldLocsReapply). That
 * invariant is what lets a query resolve through *any* built window containing
 * its tiles — the answer is window-independent — and the query functions below
 * lean on it.
 *
 * Slot state (loc slot indices) is per-window and deliberately NOT
 * window-independent: a slot number means nothing outside the window that
 * handed it out. Every slot-based call resolves against the BOUND window, and
 * the world binds the acting player's window (ToriRSServer_WorldSetActive)
 * before running anything that holds slots.
 */
struct ToriRSServerSceneWindow
{
    struct CollisionMap* collision[SCENE_LEVELS];
    /* Absolute tile of the window's (0,0); -1 = not built. */
    int base_x;
    int base_z;
    /* Which scene columns have a bridge deck (LINK_BELOW at raw level 1),
     * captured during apply_terrain and read by apply_loc_collision for
     * place-time level shift (Client-TS / LostCity). Map-square terrain
     * buffers are freed per square, so this has to be persisted for locs in
     * the same build pass. */
    unsigned char link_below[SCENE_TILES][SCENE_TILES];
    /* The window's raw tile settings bytes, gathered before the collision
     * rules run. See gather_terrain_square for why the two are separate
     * passes. */
    uint8_t settings[SCENE_LEVELS][SCENE_TILES][SCENE_TILES];
    struct ToriRSServerSceneLoc* locs;
    int loc_count;
    int loc_capacity;
};

/* torirs_server_scene.h cannot include torirs_server.h, so the pool size is
 * restated there; hold the two headers to the same arithmetic here. */
typedef char scene_window_pool_is_root_plus_players_plus_vessels
    [TORIRSSERVER_SCENE_WINDOW_MAX ==
             1 + TORIRSSERVER_PLAYER_MAX + TORIRSSERVER_SCENE_VESSEL_WINDOW_MAX
         ? 1
         : -1];
typedef char scene_vessel_window_base_is_root_plus_players
    [TORIRSSERVER_SCENE_VESSEL_WINDOW_BASE == 1 + TORIRSSERVER_PLAYER_MAX ? 1 : -1];

static struct ToriRSServerSceneWindow g_windows[TORIRSSERVER_SCENE_WINDOW_MAX];
static struct ToriRSServerSceneWindow* g_bound;
static int g_windows_ready;

/* Static zero-initialisation would leave base_x == 0, which reads as "built at
 * tile 0" — every window has to start explicitly unbuilt. */
static void
scene_windows_ensure(void)
{
    if( g_windows_ready )
        return;
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        g_windows[i].base_x = -1;
        g_windows[i].base_z = -1;
    }
    g_bound = &g_windows[0];
    g_windows_ready = 1;
}

static struct ToriRSServerSceneWindow*
scene_bound(void)
{
    scene_windows_ensure();
    return g_bound;
}

/*
 * The historical single-scene globals, re-aimed at whichever window is bound.
 * Everything below that builds, mutates or looks up slot state was written
 * against one global window and still reads that way — the bind is the only
 * thing that moved. The stateless queries (routes, LoS, occupancy) do NOT use
 * these: they resolve their own window per call, see window_containing.
 */
#define g_collision (scene_bound()->collision)
#define g_base_x (scene_bound()->base_x)
#define g_base_z (scene_bound()->base_z)
#define g_link_below (scene_bound()->link_below)
#define g_settings (scene_bound()->settings)
#define g_locs (scene_bound()->locs)
#define g_loc_count (scene_bound()->loc_count)
#define g_loc_capacity (scene_bound()->loc_capacity)

static int
window_contains(
    const struct ToriRSServerSceneWindow* window,
    int x,
    int z)
{
    if( window->base_x < 0 )
        return 0;
    return x >= window->base_x && x < window->base_x + SCENE_TILES && z >= window->base_z &&
           z < window->base_z + SCENE_TILES;
}

static struct CollisionMap*
window_collision(
    struct ToriRSServerSceneWindow* window,
    int level)
{
    if( level < 0 || level >= SCENE_LEVELS )
        return NULL;
    return window->collision[level];
}

/* The window a query at (x,z) resolves through: the bound window when it
 * covers the tile, else any built window that does. Coherence (see the pool
 * comment) is what makes "any" correct. NULL when nothing covers it. */
static struct ToriRSServerSceneWindow*
window_containing(
    int x,
    int z)
{
    scene_windows_ensure();
    if( window_contains(g_bound, x, z) )
        return g_bound;
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        if( window_contains(&g_windows[i], x, z) )
            return &g_windows[i];
    }
    return NULL;
}

/* Same, for a two-endpoint query: one window must cover both, because local
 * coordinates from two different windows cannot meet in one collision call. */
static struct ToriRSServerSceneWindow*
window_containing2(
    int x,
    int z,
    int x2,
    int z2)
{
    scene_windows_ensure();
    if( window_contains(g_bound, x, z) && window_contains(g_bound, x2, z2) )
        return g_bound;
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        if( window_contains(&g_windows[i], x, z) && window_contains(&g_windows[i], x2, z2) )
            return &g_windows[i];
    }
    return NULL;
}

/* Distinguishes "this tile is outside every window" (refuse) from "there is no
 * collision anywhere because the cache is missing" (the announced open-field
 * fallback in the route functions). */
static int
scene_any_window_built(void)
{
    scene_windows_ensure();
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        if( g_windows[i].base_x >= 0 )
            return 1;
    }
    return 0;
}

static void
window_reset(struct ToriRSServerSceneWindow* window)
{
    for( int level = 0; level < SCENE_LEVELS; level++ )
    {
        if( window->collision[level] )
            collision_map_free(window->collision[level]);
        window->collision[level] = NULL;
    }
    free(window->locs);
    window->locs = NULL;
    window->loc_count = 0;
    window->loc_capacity = 0;
    window->base_x = -1;
    window->base_z = -1;
}

/* Loc configs, decoded on demand: a door swap needs the *new* loc's
 * footprint, which is not in the map square — but decoding all 62,400 up
 * front held ~22.6MB of structs for a scene that touches a few thousand.
 * The raw config bytes stay resident in one arena (`g_loc_raw`), and decoded
 * structs live in `g_loc_configs` bounded by a CLOCK ring: an access sets
 * the id's referenced bit, an insert over a full ring evicts the first
 * cached id whose bit it does not have to clear first. A just-returned
 * config therefore survives at least one full cursor lap, which is what
 * makes the two-configs-in-hand callers (door swaps) safe. */
enum
{
    LOC_CONFIG_CACHE_CAP = 1024,
};
static struct RSCache_Dat2ConfigLoc** g_loc_configs;
static int g_loc_config_count;
static unsigned char* g_loc_raw;
static uint32_t* g_loc_raw_offset;
static int32_t* g_loc_raw_size; /* 0 = the cache has no file for this id */
static unsigned char* g_loc_ref;
static int g_loc_clock_slots[LOC_CONFIG_CACHE_CAP];
static int g_loc_clock_cursor;
static struct RSCache g_loc_profile;

/* ------------------------------------------------------------------ */
/* Window pool API                                                     */
/* ------------------------------------------------------------------ */

struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowRoot(void)
{
    scene_windows_ensure();
    return &g_windows[0];
}

struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowByIndex(int index)
{
    scene_windows_ensure();
    assert(index >= 0);
    assert(index < TORIRSSERVER_SCENE_WINDOW_MAX);
    return &g_windows[index];
}

void
ToriRSServer_SceneBindWindow(struct ToriRSServerSceneWindow* window)
{
    scene_windows_ensure();
    assert(window);
    g_bound = window;
}

struct ToriRSServerSceneWindow*
ToriRSServer_SceneBoundWindow(void)
{
    return scene_bound();
}

int
ToriRSServer_SceneWindowBuilt(const struct ToriRSServerSceneWindow* window)
{
    assert(window);
    return window->base_x >= 0;
}

int
ToriRSServer_SceneWindowContains(
    const struct ToriRSServerSceneWindow* window,
    int x,
    int z)
{
    assert(window);
    return window_contains(window, x, z);
}

struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowFind(
    int x,
    int z)
{
    return window_containing(x, z);
}

void
ToriRSServer_SceneWindowRelease(struct ToriRSServerSceneWindow* window)
{
    /* A deallocator: NULL accepted, like every *Free in the tree. */
    if( !window )
        return;
    scene_windows_ensure();
    window_reset(window);
    /* The bound window must always be a live pool entry; a released one falls
     * back to the root, the same default a world with no player bound gets. */
    if( g_bound == window )
        g_bound = &g_windows[0];
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

struct CollisionMap*
ToriRSServer_SceneCollision(int level)
{
    if( level < 0 || level >= SCENE_LEVELS )
        return NULL;
    return g_collision[level];
}

int
ToriRSServer_SceneBaseX(void)
{
    return g_base_x;
}

int
ToriRSServer_SceneBaseZ(void)
{
    return g_base_z;
}

/* "Is this tile covered by built collision" — by ANY window, not just the
 * bound one: an npc in another player's part of the world is still in scene. */
int
ToriRSServer_SceneContains(
    int x,
    int z)
{
    return window_containing(x, z) != NULL;
}

int
ToriRSServer_SceneWalkBlocked(
    int level,
    int x,
    int z)
{
    struct ToriRSServerSceneWindow* window = window_containing(x, z);
    struct CollisionMap* collision = window ? window_collision(window, level) : NULL;

    if( !collision )
        return 1;
    return (collision_map_tile(collision, x - window->base_x, z - window->base_z) &
            COLL_FLAG_WALK_BLOCKED)
               ? 1
               : 0;
}

struct ToriRSServerSceneLoc*
ToriRSServer_SceneLoc(int slot)
{
    if( slot < 0 || slot >= g_loc_count )
        return NULL;
    return &g_locs[slot];
}

/* ------------------------------------------------------------------ */
/* Loc configs                                                         */
/* ------------------------------------------------------------------ */

/* Make room in the CLOCK ring and claim a slot for `loc_id`. */
static void
loc_config_cache_admit(int loc_id)
{
    for( ;; )
    {
        int* slot = &g_loc_clock_slots[g_loc_clock_cursor];

        g_loc_clock_cursor = (g_loc_clock_cursor + 1) % LOC_CONFIG_CACHE_CAP;
        if( *slot < 0 )
        {
            *slot = loc_id;
            return;
        }
        if( g_loc_ref[*slot] )
        {
            g_loc_ref[*slot] = 0;
            continue;
        }
        RSCache_Dat2ConfigLocFree(g_loc_configs[*slot]);
        g_loc_configs[*slot] = NULL;
        *slot = loc_id;
        return;
    }
}

static const struct RSCache_Dat2ConfigLoc*
loc_config(int loc_id)
{
    struct RSCache_Dat2ConfigLoc* config;

    if( !g_loc_configs || loc_id < 0 || loc_id >= g_loc_config_count )
        return NULL;
    config = g_loc_configs[loc_id];
    if( !config )
    {
        if( g_loc_raw_size[loc_id] <= 0 )
            return NULL;
        config = RSCache_Dat2ConfigLocNewDecodeProfile(
            &g_loc_profile,
            (char*)(g_loc_raw + g_loc_raw_offset[loc_id]),
            g_loc_raw_size[loc_id]);
        if( !config )
            return NULL;
        loc_config_cache_admit(loc_id);
        g_loc_configs[loc_id] = config;
    }
    g_loc_ref[loc_id] = 1;
    return config;
}

/* ------------------------------------------------------------------ */
/* Authored loc ops (rank 1)                                           */
/* ------------------------------------------------------------------ */

/*
 * A `.loc` block's `op1=`..`op5=`, pushed in by the content loader.
 *
 * Pushed rather than pulled, which is the whole design of it. The obvious shape
 * — `ToriRSServer_SceneLocOp` calling `ToriRSServer_ContentLoc` and reading the def —
 * points this file at the content tree, and this file is deliberately the half
 * that knows nothing about one: `collision_doors_test` links `torirs_server_scene.c`
 * against the cache alone and would stop linking. So content *tells* the scene,
 * the same way it tells `ToriRSServer_LocInfoParamOverlay` about a param and
 * `ToriRSServer_ObjInfoCategoryOverlay` about a category, and a binary with no
 * content simply has no overlays and reads rank 0 — which is the right answer
 * for one, not a degraded one.
 *
 * Keyed by loc id and sparse: sixteen trees and a handful of rock families is
 * the whole of it today, so a linear scan over a growable array beats indexing
 * 62,194 loc ids five pointers wide.
 */
struct LocOpOverlay
{
    int loc_id;
    char* ops[5];
};

static struct LocOpOverlay* g_loc_op_overlays;
static int g_loc_op_overlay_count;
static int g_loc_op_overlay_capacity;

static struct LocOpOverlay*
loc_op_overlay(int loc_id)
{
    for( int i = 0; i < g_loc_op_overlay_count; i++ )
    {
        if( g_loc_op_overlays[i].loc_id == loc_id )
            return &g_loc_op_overlays[i];
    }
    return NULL;
}

void
ToriRSServer_SceneLocOpOverlay(
    int loc_id,
    int op_num,
    const char* text)
{
    struct LocOpOverlay* entry;

    assert(text);
    assert(loc_id >= 0);
    assert(op_num >= 1);
    assert(op_num <= 5);

    entry = loc_op_overlay(loc_id);
    if( !entry )
    {
        if( g_loc_op_overlay_count == g_loc_op_overlay_capacity )
        {
            int capacity = g_loc_op_overlay_capacity ? g_loc_op_overlay_capacity * 2 : 32;
            struct LocOpOverlay* grown = (struct LocOpOverlay*)realloc(
                g_loc_op_overlays, (size_t)capacity * sizeof(*grown));

            assert(grown);
            g_loc_op_overlays = grown;
            g_loc_op_overlay_capacity = capacity;
        }
        entry = &g_loc_op_overlays[g_loc_op_overlay_count++];
        memset(entry, 0, sizeof(*entry));
        entry->loc_id = loc_id;
    }
    /* Last statement wins, so a lane's overlay can restate a base tree's op. */
    free(entry->ops[op_num - 1]);
    entry->ops[op_num - 1] = strdup(text);
    assert(entry->ops[op_num - 1]);
}

void
ToriRSServer_SceneLocOpOverlayReset(void)
{
    for( int i = 0; i < g_loc_op_overlay_count; i++ )
    {
        for( int op = 0; op < 5; op++ )
            free(g_loc_op_overlays[i].ops[op]);
    }
    free(g_loc_op_overlays);
    g_loc_op_overlays = NULL;
    g_loc_op_overlay_count = 0;
    g_loc_op_overlay_capacity = 0;
}

int
ToriRSServer_SceneLocOpOverlayCount(void)
{
    return g_loc_op_overlay_count;
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
    free(g_loc_raw);
    g_loc_raw = NULL;
    free(g_loc_raw_offset);
    g_loc_raw_offset = NULL;
    free(g_loc_raw_size);
    g_loc_raw_size = NULL;
    free(g_loc_ref);
    g_loc_ref = NULL;
    for( int i = 0; i < LOC_CONFIG_CACHE_CAP; i++ )
        g_loc_clock_slots[i] = -1;
    g_loc_clock_cursor = 0;
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
    assert(g_loc_configs);

    /* Keep the raw bytes, not the decoded structs — loc_config() decodes on
     * demand into the CLOCK-bounded cache. One arena rather than a block per
     * file: 62,400 separate mallocs is a megabyte of allocator headers. */
    {
        size_t total = 0;
        size_t cursor = 0;

        for( int i = 0; i < archive->file_count; i++ )
        {
            int id = archive->file_ids[i];

            if( id < 0 || id >= g_loc_config_count || files->file_sizes[i] <= 0 )
                continue;
            total += (size_t)files->file_sizes[i];
        }
        g_loc_raw = malloc(total > 0 ? total : 1);
        assert(g_loc_raw);
        g_loc_raw_offset =
            calloc((size_t)g_loc_config_count, sizeof(*g_loc_raw_offset));
        assert(g_loc_raw_offset);
        g_loc_raw_size = calloc((size_t)g_loc_config_count, sizeof(*g_loc_raw_size));
        assert(g_loc_raw_size);
        g_loc_ref = calloc((size_t)g_loc_config_count, sizeof(*g_loc_ref));
        assert(g_loc_ref);

        for( int i = 0; i < archive->file_count; i++ )
        {
            int id = archive->file_ids[i];

            if( id < 0 || id >= g_loc_config_count || files->file_sizes[i] <= 0 )
                continue;
            memcpy(g_loc_raw + cursor, files->files[i], (size_t)files->file_sizes[i]);
            g_loc_raw_offset[id] = (uint32_t)cursor;
            g_loc_raw_size[id] = files->file_sizes[i];
            cursor += (size_t)files->file_sizes[i];
        }
    }

    g_loc_profile = *profile;
    for( int i = 0; i < LOC_CONFIG_CACHE_CAP; i++ )
        g_loc_clock_slots[i] = -1;
    g_loc_clock_cursor = 0;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return g_loc_config_count;
}

const char*
ToriRSServer_SceneLocOp(
    int loc_id,
    int op_num)
{
    const struct LocOpOverlay* overlay;
    const struct RSCache_Dat2ConfigLoc* config;

    if( op_num < 1 || op_num > 5 )
        return NULL;

    /*
     * Rank 1 over rank 0 — the same merge `ToriRSServer_LocCategory` applies one
     * field over, and for the same reason: the cache states what the *client*
     * offers, and a server op the client must not offer has nowhere else to live.
     * `op3=hidden` in a `.loc` block is the whole point, and until this read
     * existed a skilling script could not resume on a hidden op, because the op
     * it named was empty here and `p_oploc` returns silently on an empty op.
     *
     * Ungated on the cache record existing: a loc the overlay names and the cache
     * does not is still a loc, and answering NULL for it would put an authored op
     * back out of reach for exactly the records that need one most.
     */
    overlay = loc_op_overlay(loc_id);
    if( overlay && overlay->ops[op_num - 1] )
        return overlay->ops[op_num - 1];

    config = loc_config(loc_id);
    if( !config )
        return NULL;
    return config->actions[op_num - 1];
}

int
ToriRSServer_LocResolveTransform(
    struct ToriRSServerPlayer* player,
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
        index = ToriRSServer_VarbitGet(player, cfg->transform_varbit);
    else if( cfg->transform_varp != -1 )
    {
        if( cfg->transform_varp >= 0 && cfg->transform_varp < TORIRSSERVER_VARP_COUNT )
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

static void apply_terrain_column(int scene_x, int scene_z);
static void restamp_after_removal(const struct ToriRSServerSceneLoc* removed);

/*
 * Which collision primitive a loc contributes, by shape. Mirrors
 * src/engine/world_builder/world_collision.u.c — the client's own switch — and
 * has to keep mirroring it: the whole value of this file is that both ends
 * block the same tiles.
 *
 * LINK_BELOW is not this function's business any more: `loc->level` is the
 * level the loc is *walked from*, shifted once by `record_loc_at` where the
 * map square is read (LostCity GameMap.loadLocations `actualLevel`), and a
 * `loc_add` names that same level because it comes from a content coord. This
 * used to shift again here, which meant a runtime loc on a bridge deck asked
 * for collision level -1 and got none at all. See docs/COLLISION_MAP.md.
 */
static void
apply_loc_collision(
    struct ToriRSServerSceneLoc* loc,
    int add)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(loc->loc_id);
    struct CollisionMap* map;
    enum CollisionLocAngle angle;
    int blockrange;
    int scene_x, scene_z;

    if( !config || loc->level < 0 || loc->level >= SCENE_LEVELS )
        return;

    scene_x = loc->x - g_base_x;
    scene_z = loc->z - g_base_z;
    if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 || scene_z >= SCENE_TILES )
        return;

    map = g_collision[loc->level];
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
             * `ToriRSServer_SceneFindLoc` and `interaction_target` both need the
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

    /* A removal takes the bits it shares with whatever else is still standing.
     * Put those back — see restamp_after_removal. */
    if( !add )
        restamp_after_removal(loc);
}

/*
 * The tiles a loc's collision stamp can reach, in absolute coordinates and
 * inclusive: its footprint, plus one tile of margin because a wall stamps the
 * complementary bit onto the neighbour across its edge.
 */
static void
loc_stamp_box(
    const struct ToriRSServerSceneLoc* loc,
    int* x0,
    int* z0,
    int* x1,
    int* z1)
{
    int w = loc->size_x > 0 ? loc->size_x : 1;
    int h = loc->size_z > 0 ? loc->size_z : 1;

    *x0 = loc->x - 1;
    *z0 = loc->z - 1;
    *x1 = loc->x + w;
    *z1 = loc->z + h;
}

/*
 * Collision is a function of the loc set, not a running total.
 *
 * `collision_map_del_wall` / `del_loc` / `del_floor` clear their bits
 * unconditionally — that is the reference's own primitive — and two things
 * routinely share a bit. A wall on the east edge of (x,z) and a wall on the
 * west edge of (x+1,z) are the *same edge*, so they stamp the same two flags;
 * a ground-decor loc's FLOOR is the same bit the map square's own Block
 * setting stamps; and a door that swings flush against a wall lands on the
 * very tile that wall already covers. Clearing one used to take the other with
 * it and nothing put it back, so the hole was permanent.
 *
 * Measured over every map square in cache.osrs239: 75 of the game's doors
 * punched a hole in a neighbouring wall the first time they were opened and
 * closed again, making 44 wall edges walkable that are not. `test-collision-doors`
 * is the regression.
 *
 * So a removal is "clear, then put back everything else that is still there":
 * the map square's terrain for the tiles the stamp could have touched, and
 * every other active loc whose own stamp reaches them. Adds are ORs, so
 * re-stamping more than strictly necessary costs time and nothing else.
 */
static void
restamp_after_removal(const struct ToriRSServerSceneLoc* removed)
{
    int x0, z0, x1, z1;

    if( g_base_x < 0 )
        return;
    loc_stamp_box(removed, &x0, &z0, &x1, &z1);

    for( int x = x0; x <= x1; x++ )
    {
        for( int z = z0; z <= z1; z++ )
        {
            int scene_x = x - g_base_x;
            int scene_z = z - g_base_z;

            if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 ||
                scene_z >= SCENE_TILES )
                continue;
            apply_terrain_column(scene_x, scene_z);
        }
    }

    for( int i = 0; i < g_loc_count; i++ )
    {
        struct ToriRSServerSceneLoc* other = &g_locs[i];
        int ox0, oz0, ox1, oz1;

        if( other == removed || !other->active )
            continue;
        loc_stamp_box(other, &ox0, &oz0, &ox1, &oz1);
        if( ox1 < x0 || ox0 > x1 || oz1 < z0 || oz0 > z1 )
            continue;
        apply_loc_collision(other, 1);
    }
}

/*
 * Record one loc at an absolute tile.
 *
 * The tile, level and angle are arguments rather than read off `map_loc`
 * because the instanced build passes a *transformed* placement: the same cache
 * record lands on a different tile, on a different plane, turned by the zone's
 * rotation. The cache path passes the record's own values.
 *
 * The level the record carries is the *cache* level, and a bridge deck is
 * authored one level above the level it is walked from. LostCity's
 * `GameMap.loadLocations` resolves that once, at read time, and hands
 * `actualLevel` to both the collision stamp and `Zone.addStaticLoc` — so a
 * player standing on a bridge is on the same plane as the locs he can see and
 * click, and content addressing them with a plain coord finds them. This
 * server only shifted the collision stamp, which left every bridged loc a
 * plane above every lookup that could reach it: `ToriRSServer_SceneFindLoc` (the
 * click), `loc_find` (content), and the ZoneMap the wire is built from. The
 * Dragonkin coffer is the case that surfaced it — `~rs2012_qbd_prepare_coffer`
 * found nothing on the player's level and added a *second* coffer beside the
 * one the map had already placed.
 */
static void
record_loc_at(
    const struct RSCache_MapLoc* map_loc,
    int abs_x,
    int abs_z,
    int level,
    int angle)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(map_loc->loc_id);
    struct ToriRSServerSceneLoc* loc;

    if( !config )
        return;
    /* The BOUND window's bounds, not any-window: this records into the window
     * being built. */
    if( !window_contains(scene_bound(), abs_x, abs_z) )
        return;

    if( g_link_below[abs_x - g_base_x][abs_z - g_base_z] )
        level--;
    if( level < 0 )
        return;

    if( g_loc_count == g_loc_capacity )
    {
        int capacity = g_loc_capacity ? g_loc_capacity * 2 : 1024;
        struct ToriRSServerSceneLoc* grown = realloc(g_locs, (size_t)capacity * sizeof(*grown));

        assert(grown);
        g_locs = grown;
        g_loc_capacity = capacity;
    }

    loc = &g_locs[g_loc_count++];
    loc->loc_id = map_loc->loc_id;
    loc->shape = map_loc->shape_select;
    loc->angle = angle & 3;
    loc->x = abs_x;
    loc->z = abs_z;
    loc->level = level;
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
     * reusable once something removes it — see ToriRSServer_SceneAddLoc. */
    loc->is_static = 1;
    /* The map square carries no menu of its own: what this loc offers is what
     * its loctype offers. */
    ToriRSServer_LocOpsDefault(&loc->ops);

    apply_loc_collision(loc, 1);
}

static void
record_loc(
    const struct RSCache_MapLoc* map_loc,
    int map_x,
    int map_z)
{
    record_loc_at(map_loc, map_x * 64 + map_loc->chunk_pos_x,
                  map_z * 64 + map_loc->chunk_pos_z, map_loc->chunk_pos_level,
                  map_loc->orientation);
}

/*
 * Gather a map square's tile settings into the scene's settings buffer.
 *
 * Gathering and *applying* are two passes rather than one, which they were until
 * map instances existed. The reason is LINK_BELOW: it is a property of the
 * column, read at level 1 and applied to every level of that column. In a
 * cache-built scene a column never crosses a map square, so reading it while
 * walking one square was safe. In an *instanced* scene each plane of a
 * destination zone has its own source descriptor, so the four levels of one
 * column can come from four different places and the column is not complete
 * until every plane has been copied. Splitting the passes is what lets both
 * builds share one copy of the rules below.
 */
static void
gather_terrain_square(
    const struct RSCache_MapTerrain* terrain,
    int map_x,
    int map_z)
{
    for( int local_x = 0; local_x < 64; local_x++ )
    {
        for( int local_z = 0; local_z < 64; local_z++ )
        {
            int scene_x = map_x * 64 + local_x - g_base_x;
            int scene_z = map_z * 64 + local_z - g_base_z;

            if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 ||
                scene_z >= SCENE_TILES )
                continue;

            for( int level = 0; level < SCENE_LEVELS; level++ )
                g_settings[level][scene_x][scene_z] =
                    terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(local_x, local_z, level)]
                        .settings;
        }
    }
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
/* One column's terrain rules, so a runtime restamp can redo a handful of tiles
 * without walking the scene. `g_link_below` must already be filled — it is a
 * property of the whole column and the build pass above owns it. */
static void
apply_terrain_column(
    int scene_x,
    int scene_z)
{
    int link_below = g_link_below[scene_x][scene_z];

    for( int level = 0; level < SCENE_LEVELS; level++ )
    {
        uint8_t settings = g_settings[level][scene_x][scene_z];
        int true_level = level;

        if( (settings & RSCACHE_FLOFLAG_BLOCK) != 0 )
        {
            if( link_below )
                true_level--;
            if( true_level >= 0 && true_level < SCENE_LEVELS && g_collision[true_level] )
                collision_map_add_floor(g_collision[true_level], scene_x, scene_z);
        }

        /* Roofed-ness, which is what moverestrict=indoors/outdoors reads.
         * REMOVE_ROOF marks the tiles the client hides roofs over, i.e. the
         * ones that are indoors, and it is the same bit the reference stamps
         * ROOF from (LostCity GameMap). Raw cache level, no bridge shift. */
        if( (settings & RSCACHE_FLOFLAG_REMOVE_ROOF) != 0 && g_collision[level] )
            collision_map_change_roof(g_collision[level], scene_x, scene_z, 1);
    }
}

static void
apply_terrain_rules(void)
{
    for( int scene_x = 0; scene_x < SCENE_TILES; scene_x++ )
        for( int scene_z = 0; scene_z < SCENE_TILES; scene_z++ )
            if( (g_settings[1][scene_x][scene_z] & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
                g_link_below[scene_x][scene_z] = 1;

    /* LINK_BELOW for the whole scene first: the shift below reads it, and a
     * column is not complete until every plane has been gathered. */
    for( int scene_x = 0; scene_x < SCENE_TILES; scene_x++ )
        for( int scene_z = 0; scene_z < SCENE_TILES; scene_z++ )
            apply_terrain_column(scene_x, scene_z);
}

/*
 * Formerly a whole-flag-word column overwrite. Loc/terrain collision now use
 * place-time LinkBelow shift; this remains so build's call site is stable.
 */
static void
apply_bridges(void)
{
}

/* TEMPORARY DEBUG: raw tile settings + link_below, for the ToB map survey. */
int
ToriRSServer_SceneDebugSettings(int level, int x, int z)
{
    int sx = x - g_base_x;
    int sz = z - g_base_z;

    if( sx < 0 || sx >= SCENE_TILES || sz < 0 || sz >= SCENE_TILES )
        return -1;
    if( level < 0 || level >= SCENE_LEVELS )
        return -1;
    return g_settings[level][sx][sz] | (g_link_below[sx][sz] ? 0x100 : 0);
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

/*
 * Everything both builds do before they diverge: throw the old scene away, set
 * the origin, open the cache and its keys, allocate collision, decode the loc
 * configs. Returns the opened disk, or NULL — in which case the scene is empty
 * and every tile stays walkable, which is the announced fallback.
 */
static struct RSCache_Dat2Disk*
scene_build_begin(
    const char* cache_dir,
    int zone_x,
    int zone_z,
    struct RSCache* profile)
{
    struct RSCache_Dat2Disk* disk;
    char resolved[512];
    char keys[600];

    assert(profile);
    /* Only the BOUND window is rebuilt; the other windows and the shared loc
     * configs stay exactly as they are. */
    window_reset(scene_bound());

    g_base_x = (zone_x - 6) * 8;
    g_base_z = (zone_z - 6) * 8;

    *profile = RSCache_ProfileZero();
    profile->game = RSCACHE_GAME_OLDSCHOOL;
    profile->epoch = RSCACHE_EPOCH_DAT2;
    profile->revision = TORIRSSERVER_CACHE_REVISION;

    if( !open_disk(cache_dir, &disk, resolved, sizeof(resolved)) )
    {
        fprintf(stderr,
                "torirsserver: no cache at %s — collision disabled (walk through walls)\n",
                cache_dir);
        g_base_x = g_base_z = -1;
        return NULL;
    }
    RSCache_Dat2DiskSetProfile(disk, profile);

    /* The map archives are encrypted at this revision; the keys sit beside the
     * cache, in the file the client reads. Loading them is global state in
     * rscache, which is fine — the mock opens one cache. */
    snprintf(keys, sizeof(keys), "%s/xteas.json", resolved);
    if( !RSCache_XteaConfigLoadKeys(keys) )
        fprintf(stderr, "torirsserver: no %s — encrypted map squares will not decode\n", keys);

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
                                       ToriRSServer_SceneFeatures()->route_window_tiles);
    }
    memset(g_link_below, 0, sizeof(g_link_below));
    memset(g_settings, 0, sizeof(g_settings));

    /* Loc configs are cache state, not window state: decoded once and shared
     * by every window. Rebuilding a window keeps them; only SceneFree — the
     * whole-module teardown — drops them. */
    if( !g_loc_configs )
        load_loc_configs(disk, profile);
    return disk;
}

int
ToriRSServer_SceneBuild(
    const char* cache_dir,
    int zone_x,
    int zone_z)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = scene_build_begin(cache_dir, zone_x, zone_z, &profile);
    int square_x0, square_x1, square_z0, square_z1;
    int locs_before;

    if( !disk )
        return 0;

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

            if( !terrain )
                continue;
            gather_terrain_square(terrain, map_x, map_z);
            RSCache_MapTerrainFree(terrain);
        }
    }
    apply_terrain_rules();

    /* Locs after every column's terrain, because apply_loc_collision reads
     * g_link_below to decide which collision level a loc lands on. */
    for( int map_x = square_x0; map_x <= square_x1; map_x++ )
    {
        for( int map_z = square_z0; map_z <= square_z1; map_z++ )
        {
            struct RSCache_MapLocs* locs = RSCache_MapLocsNewFromCache(disk, map_x, map_z);

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
    fprintf(stderr, "torirsserver: scene built at zone %d,%d (base %d,%d — %d locs)\n", zone_x,
            zone_z, g_base_x, g_base_z, locs_before);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Instanced build                                                     */
/* ------------------------------------------------------------------ */

/*
 * Copy one source zone's tile settings into one destination zone, turned.
 *
 * Walks the *destination* and asks where each tile came from, which is why this
 * uses `ToriRSServer_MapInstanceRotateToSrc` and the loc pass below uses the other
 * one. Only the settings byte is copied: collision is all the server wants from
 * terrain. Heights, overlays and tile shapes matter to the client and travel
 * over the wire as a descriptor for it to resolve itself.
 */
static void
gather_terrain_zone(
    const struct RSCache_MapTerrain* terrain,
    const struct ToriRSServerMapInstanceZone* zone,
    int dst_level,
    int dst_zone_x,
    int dst_zone_z)
{
    int src_local_zone_x = zone->src_zone_x & 7;
    int src_local_zone_z = zone->src_zone_z & 7;

    for( int dx = 0; dx < 8; dx++ )
    {
        for( int dz = 0; dz < 8; dz++ )
        {
            int scene_x = dst_zone_x * 8 + dx;
            int scene_z = dst_zone_z * 8 + dz;
            int sx;
            int sz;

            if( scene_x < 0 || scene_x >= SCENE_TILES || scene_z < 0 ||
                scene_z >= SCENE_TILES )
                continue;
            ToriRSServer_MapInstanceRotateToSrc(zone->rotation, dx, dz, &sx, &sz);
            g_settings[dst_level][scene_x][scene_z] =
                terrain
                    ->tiles_xyz[RSCACHE_MAP_TILE_COORD(src_local_zone_x * 8 + sx,
                                                       src_local_zone_z * 8 + sz,
                                                       zone->src_level)]
                    .settings;
        }
    }
}

/*
 * Copy one source zone's locs into one destination zone, turned.
 *
 * Walks the source's records — the other direction from the terrain pass — so
 * the transform is `_to_dst`, and it takes the loc's footprint because a
 * quarter-turn moves a multi-tile loc's south-west corner to a different corner
 * of its own rectangle.
 *
 * The loc's angle gains the zone's rotation. That is the whole of "the building
 * is turned": every wall, door and stair inside it turns with it, and a wall
 * whose angle did not follow would end up facing out of the room.
 */
static void
record_locs_zone(
    const struct RSCache_MapLocs* locs,
    const struct ToriRSServerMapInstanceZone* zone,
    int dst_level,
    int dst_zone_x,
    int dst_zone_z)
{
    int src_tile_x0 = (zone->src_zone_x & 7) * 8;
    int src_tile_z0 = (zone->src_zone_z & 7) * 8;

    for( int i = 0; i < locs->locs_count; i++ )
    {
        const struct RSCache_MapLoc* map_loc = &locs->locs[i];
        const struct RSCache_Dat2ConfigLoc* config = loc_config(map_loc->loc_id);
        int sx = map_loc->chunk_pos_x - src_tile_x0;
        int sz = map_loc->chunk_pos_z - src_tile_z0;
        int size_x;
        int size_z;
        int dx;
        int dz;
        int angle;

        if( g_trace_loc && map_loc->loc_id == g_trace_loc && sx >= 0 && sx < 8 && sz >= 0 &&
            sz < 8 )
            fprintf(stderr, "TRACE zone loc=%d cpl=%d srclvl=%d sx=%d sz=%d cfg=%d dst=%d\n",
                    map_loc->loc_id, map_loc->chunk_pos_level, zone->src_level, sx, sz,
                    config != NULL, dst_level);
        if( map_loc->chunk_pos_level != zone->src_level )
            continue;
        if( sx < 0 || sx >= 8 || sz < 0 || sz >= 8 )
            continue;
        if( !config )
            continue;

        /* The footprint as placed, i.e. after the loc's own angle has had its
         * say — the same swap record_loc_at applies. The rotation correction
         * needs the extents that actually run along each axis. */
        if( (map_loc->orientation & 1) != 0 )
        {
            size_x = config->size_z;
            size_z = config->size_x;
        }
        else
        {
            size_x = config->size_x;
            size_z = config->size_z;
        }

        ToriRSServer_MapInstanceRotateToDst(zone->rotation, sx, sz, size_x, size_z, &dx, &dz);
        angle = (map_loc->orientation + zone->rotation) & 3;
        record_loc_at(map_loc, g_base_x + dst_zone_x * 8 + dx,
                      g_base_z + dst_zone_z * 8 + dz, dst_level, angle);
    }
}

/*
 * The distinct source map squares a window references.
 *
 * Both instanced passes are grouped by source square rather than by destination
 * zone: a house is dozens of destination zones pointing at a handful of source
 * squares — often one — and each `RSCache_MapTerrainNewFromCache` decrypts and
 * decodes a whole 64x64x4 square. Walking destinations in the obvious order
 * would decode the same square up to 676 times.
 *
 * Returns the count written to `out_x`/`out_z`, capped at `capacity`.
 */
static int
window_source_squares(
    const struct ToriRSServerMapInstanceWindow* window,
    int* out_x,
    int* out_z,
    int capacity)
{
    int count = 0;

    for( int level = 0; level < TORIRSSERVER_MAPINSTANCE_LEVELS; level++ )
    {
        for( int zx = 0; zx < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zx++ )
        {
            for( int zz = 0; zz < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zz++ )
            {
                const struct ToriRSServerMapInstanceZone* zone = &window->zones[level][zx][zz];
                int map_x;
                int map_z;
                int seen = 0;

                if( !zone->set )
                    continue;
                map_x = zone->src_zone_x >> 3;
                map_z = zone->src_zone_z >> 3;
                for( int i = 0; i < count && !seen; i++ )
                {
                    if( out_x[i] == map_x && out_z[i] == map_z )
                        seen = 1;
                }
                if( seen || count >= capacity )
                    continue;
                out_x[count] = map_x;
                out_z[count] = map_z;
                count++;
            }
        }
    }
    return count;
}

int
ToriRSServer_SceneBuildInstance(
    const char* cache_dir,
    int zone_x,
    int zone_z,
    const struct ToriRSServerMapInstanceWindow* window)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    /* One per descriptor is the ceiling: every zone could name a different
     * square. In practice this is 1 to 4. */
    int square_x[TORIRSSERVER_MAPINSTANCE_LEVELS * TORIRSSERVER_MAPINSTANCE_SCENE_ZONES *
                 TORIRSSERVER_MAPINSTANCE_SCENE_ZONES];
    int square_z[TORIRSSERVER_MAPINSTANCE_LEVELS * TORIRSSERVER_MAPINSTANCE_SCENE_ZONES *
                 TORIRSSERVER_MAPINSTANCE_SCENE_ZONES];
    int square_count;
    int locs_before;

    assert(window);
    disk = scene_build_begin(cache_dir, zone_x, zone_z, &profile);
    if( !disk )
        return 0;

    square_count =
        window_source_squares(window, square_x, square_z, (int)(sizeof(square_x) / sizeof(*square_x)));
    if( g_trace_loc < 0 )
    {
        const char* t = getenv("TORIRS_TRACE_LOC");
        g_trace_loc = t ? atoi(t) : 0;
    }
    if( g_trace_loc )
    {
        fprintf(stderr, "TRACE build zone=%d,%d base=%d,%d squares=%d:", zone_x, zone_z, g_base_x,
                g_base_z, square_count);
        for( int q = 0; q < square_count; q++ )
            fprintf(stderr, " %d_%d", square_x[q], square_z[q]);
        fprintf(stderr, "\n");
    }

    for( int s = 0; s < square_count; s++ )
    {
        struct RSCache_MapTerrain* terrain =
            RSCache_MapTerrainNewFromCache(disk, square_x[s], square_z[s]);

        if( !terrain )
            continue;
        for( int level = 0; level < TORIRSSERVER_MAPINSTANCE_LEVELS; level++ )
            for( int zx = 0; zx < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zx++ )
                for( int zz = 0; zz < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zz++ )
                {
                    const struct ToriRSServerMapInstanceZone* zone = &window->zones[level][zx][zz];

                    if( !zone->set || (zone->src_zone_x >> 3) != square_x[s] ||
                        (zone->src_zone_z >> 3) != square_z[s] )
                        continue;
                    gather_terrain_zone(terrain, zone, level, zx, zz);
                }
        RSCache_MapTerrainFree(terrain);
    }
    apply_terrain_rules();

    /* Locs after every column's terrain — apply_loc_collision reads
     * g_link_below, and in an instanced scene a column's four planes can come
     * from four different source squares. */
    for( int s = 0; s < square_count; s++ )
    {
        struct RSCache_MapLocs* locs =
            RSCache_MapLocsNewFromCache(disk, square_x[s], square_z[s]);

        if( !locs )
            continue;
        for( int level = 0; level < TORIRSSERVER_MAPINSTANCE_LEVELS; level++ )
            for( int zx = 0; zx < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zx++ )
                for( int zz = 0; zz < TORIRSSERVER_MAPINSTANCE_SCENE_ZONES; zz++ )
                {
                    const struct ToriRSServerMapInstanceZone* zone = &window->zones[level][zx][zz];

                    if( !zone->set || (zone->src_zone_x >> 3) != square_x[s] ||
                        (zone->src_zone_z >> 3) != square_z[s] )
                        continue;
                    record_locs_zone(locs, zone, level, zx, zz);
                }
        RSCache_MapLocsFree(locs);
    }

    apply_bridges();

    locs_before = g_loc_count;
    RSCache_Dat2DiskFree(disk);
    fprintf(stderr,
            "torirsserver: instanced scene built at zone %d,%d (base %d,%d — %d source zones, %d "
            "locs)\n",
            zone_x, zone_z, g_base_x, g_base_z, window->set_count, locs_before);
    return 1;
}

void
ToriRSServer_SceneFree(void)
{
    /* The historical whole-scene teardown: the bound window and the shared loc
     * configs. Tests that build one scene and free it keep their old contract;
     * the world releases the other windows itself (WorldReset / player
     * teardown, via ToriRSServer_SceneWindowRelease). */
    window_reset(scene_bound());
    free_loc_configs();
}

/* ------------------------------------------------------------------ */
/* Loc lookup and mutation                                             */
/* ------------------------------------------------------------------ */

int
ToriRSServer_SceneFindLoc(
    int x,
    int z,
    int level,
    int loc_id)
{
    int fallback = -1;

    for( int i = 0; i < g_loc_count; i++ )
    {
        struct ToriRSServerSceneLoc* loc = &g_locs[i];

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
ToriRSServer_SceneFindLocId(
    int x,
    int z,
    int level,
    int loc_id)
{
    for( int i = 0; i < g_loc_count; i++ )
    {
        struct ToriRSServerSceneLoc* loc = &g_locs[i];

        /* The loc's own south-west corner and its own id, both exact: this is
         * `loc_find`'s question, and the reference answers it with
         * `Zone.getLoc(x, z, locId)` — locs keyed by their corner tile,
         * filtered by type. `ToriRSServer_SceneFindLoc`'s footprint-plus-fallback
         * is the *click* resolver and deliberately answers a different
         * question; asking it here made `loc_find(coord, X)` report true for
         * any loc standing on the tile. */
        if( !loc->active || loc->level != level )
            continue;
        if( loc->x != x || loc->z != z || loc->loc_id != loc_id )
            continue;
        return i;
    }
    return -1;
}

int
ToriRSServer_SceneFindLocExact(
    int x,
    int z,
    int level,
    int shape)
{
    int added = -1;
    int from_square = -1;
    int tombstone = -1;

    /*
     * The loc's own corner and its own shape, and *not* its footprint: this is
     * the mutation lookup, and a mutation addresses the loc the wire names
     * rather than whatever a click landed on.
     *
     * Three tiers, and each one is load-bearing:
     *
     *   1. an active loc a `loc_add` put here. A `loc_add` leaves the map
     *      square's own loc standing beside it (ToriRSServer_SceneAddLoc), so "the
     *      loc on this tile" for a following `loc_change` / `loc_del` is the
     *      added one. Answering with the square's instead would let a door that
     *      swung past a wall close by deleting that wall.
     *   2. the map square's loc, which is what every mutation on an untouched
     *      tile means.
     *   3. a static tombstone — inactive, but the square still has a loc here.
     *      This is how a `loc_del` on a map-square loc is undone: the slot stays
     *      addressable with nothing live on the tile.
     */
    for( int i = g_loc_count - 1; i >= 0; i-- )
    {
        struct ToriRSServerSceneLoc* loc = &g_locs[i];

        if( loc->x != x || loc->z != z || loc->level != level || loc->shape != shape )
            continue;
        if( loc->active && !loc->is_static )
        {
            if( added < 0 )
                added = i;
        }
        else if( loc->active )
        {
            if( from_square < 0 )
                from_square = i;
        }
        else if( loc->is_static && tombstone < 0 )
            tombstone = i;
    }
    if( added >= 0 )
        return added;
    if( from_square >= 0 )
        return from_square;
    return tombstone;
}

int
ToriRSServer_SceneReplaceLoc(
    int slot,
    int loc_id,
    int angle)
{
    struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);
    const struct RSCache_Dat2ConfigLoc* config;

    if( !loc )
        return 0;
    config = loc_config(loc_id);
    if( !config )
        return 0;

    /* An *inactive* slot is revived rather than refused, which is how a
     * `loc_del` on a map-square loc is undone: the tombstone stays in the array
     * (see ToriRSServer_SceneAddLoc) precisely so the tile can be put back without
     * the caller having to know whether it is currently there. Its collision is
     * already gone, so there is nothing to take away first. */
    if( getenv("TORIRSSERVER_DOORTRACE") && loc->x == 3226 && loc->z == 3223 && loc->level == 0 )
        fprintf(stderr, "  DOORTRACE replace slot=%d %d -> %d\n", slot, loc->loc_id, loc_id);
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
ToriRSServer_SceneAddLoc(
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    const struct RSCache_Dat2ConfigLoc* config = loc_config(loc_id);
    struct ToriRSServerSceneLoc* loc;

    if( !config )
        return -1;
    /* The BOUND window's bounds: a loc slot lives in the window that hands the
     * slot number out, so an add lands in the window the caller is bound to. */
    if( !window_contains(scene_bound(), x, z) )
        return -1;

    /*
     * The map square's own loc on this tile is left exactly where it is.
     *
     * That is the reference: `World.addLoc` stamps the new loc's collision and
     * calls `Zone.addLoc`, which appends to the zone's list — neither one so
     * much as looks at what is already there, and `Zone.removeLoc` puts a
     * static loc back on the list rather than dropping it. Consuming it instead
     * is what made a swinging door delete the wall it swung against: the wall's
     * slot became the door, and closing the door left the tile empty.
     *
     * A *dynamic* loc already here is a different matter — the wire carries one
     * loc per (tile, layer), so a second `loc_add` on the same tile is
     * replacing the first, and stacking them would grow the array without bound
     * for a world that cycles one on a timer.
     */
    {
        int prior = ToriRSServer_SceneFindLocExact(x, z, level, shape);
        struct ToriRSServerSceneLoc* held = ToriRSServer_SceneLoc(prior);

        if( held && held->active && !held->is_static )
            ToriRSServer_SceneRemoveLoc(prior);
    }

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
            struct ToriRSServerSceneLoc* grown = realloc(g_locs, (size_t)capacity * sizeof(*grown));

            assert(grown);
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
    /* Neutral until a caller says otherwise. A reused slot still holds the
     * previous occupant's menu, so this is a reset and not an initialisation:
     * without it, the second loc to land in a recycled slot would inherit the
     * first one's overrides. */
    ToriRSServer_LocOpsDefault(&loc->ops);
    apply_loc_collision(loc, 1);
    return (int)(loc - g_locs);
}

void
ToriRSServer_SceneLocSetOps(
    int slot,
    const struct ToriRSServerLocOps* ops)
{
    struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

    assert(ops);
    assert(loc);
    assert(loc->active);
    loc->ops = *ops;
}

const char*
ToriRSServer_SceneLocPlacementOp(
    int slot,
    int loc_id,
    int op_num)
{
    const struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);
    const char* type_op = ToriRSServer_SceneLocOp(loc_id, op_num);

    if( op_num < 1 || op_num > 5 )
        return NULL;
    /* No slot is a legitimate runtime state, not a contract violation: a loc
     * beyond the scene window lives only in the ZoneMap, and the click on it
     * still has to be judged against something. The type is what the client
     * drew it from in that case, because nothing overrode it. */
    if( !loc )
        return type_op;
    return ToriRSServer_LocOpsLabel(&loc->ops, op_num, type_op);
}

int
ToriRSServer_SceneRemoveLoc(int slot)
{
    struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

    if( !loc || !loc->active )
        return 0;
    if( getenv("TORIRSSERVER_DOORTRACE") && loc->x == 3226 && loc->z == 3223 && loc->level == 0 )
        fprintf(stderr, "  DOORTRACE remove slot=%d id=%d\n", slot, loc->loc_id);
    apply_loc_collision(loc, 0);
    loc->active = 0;
    return 1;
}

/*
 * `ToriRSServer_SceneNextChangedLoc` was here, and with it the `changed` flag on
 * every scene loc.
 *
 * It had one caller — phase 10, re-sending opened doors after a REBUILD_NORMAL —
 * and it could never have worked: `ToriRSServer_SceneBuild` calls
 * `ToriRSServer_SceneFree` first, so by the time the rebuild's re-send ran, the
 * array carrying the flags had been freed and re-read from the cache. The
 * durable record of a loc mutation is `struct ToriRSServerZoneLoc` in the ZoneMap
 * now, which is also what puts collision back (`ToriRSServer_WorldLocsReapply`).
 */

/* ------------------------------------------------------------------ */
/* Routing                                                             */
/* ------------------------------------------------------------------ */

/* Direction index -> tile delta, in the client's World_CoordStep numbering:
 * 0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE. */
static const int k_step_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int k_step_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

void
ToriRSServer_SceneSetFeatures(struct ToriRS_FeatureTable const* features)
{
    g_features = features;
}

struct ToriRS_FeatureTable const*
ToriRSServer_SceneFeatures(void)
{
    if( !g_features )
        g_features = ToriRS_Features_OSRS();
    return g_features;
}

int
ToriRSServer_SceneCanStep(
    int level,
    int x,
    int z,
    int dir)
{
    return ToriRSServer_SceneCanStepExtra(level, x, z, dir, 0);
}

int
ToriRSServer_SceneCanStepExtra(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag)
{
    return ToriRSServer_SceneCanStepTyped(level, x, z, dir, extra_flag, COLL_TYPE_NORMAL);
}

int
ToriRSServer_SceneCanStepTyped(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag,
    int coll_type)
{
    if( dir < 0 || dir > 7 )
        return 0;
    return ToriRSServer_SceneCanTravelTyped(
        level, x, z, k_step_dx[dir], k_step_dz[dir], 1, extra_flag, coll_type);
}

int
ToriRSServer_SceneCanTravel(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag)
{
    return ToriRSServer_SceneCanTravelTyped(
        level, x, z, offset_x, offset_z, size, extra_flag, COLL_TYPE_NORMAL);
}

int
ToriRSServer_SceneCanTravelTyped(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type)
{
    struct ToriRSServerSceneWindow* window = window_containing2(x, z, x + offset_x, z + offset_z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map || size < 1 )
        return 0;
    return collision_map_can_travel_typed(map, x - window->base_x, z - window->base_z, offset_x,
                                          offset_z, size, extra_flag, coll_type);
}

/* Raw collision flags at an absolute tile, 0 outside the built scene. A probe
 * for tests and diagnostics: "is this blocked" has several right answers
 * depending on who is asking, so this hands back the bits and lets the caller
 * decide which question it meant. */
int
ToriRSServer_SceneTileFlags(int level, int x, int z)
{
    struct ToriRSServerSceneWindow* window = window_containing(x, z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
        return 0;
    return collision_map_tile(map, x - window->base_x, z - window->base_z);
}

int
ToriRSServer_SceneLineOfSight(
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
    struct ToriRSServerSceneWindow* window = window_containing2(sx, sz, dx, dz);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
        return 0;
    return collision_map_line_of_sight(map, sx - window->base_x, sz - window->base_z,
                                       dx - window->base_x, dz - window->base_z, sw, sh, dw, dh,
                                       extra);
}

int
ToriRSServer_SceneLineOfWalk(
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
    struct ToriRSServerSceneWindow* window = window_containing2(sx, sz, dx, dz);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
        return 0;
    return collision_map_line_of_walk(map, sx - window->base_x, sz - window->base_z,
                                      dx - window->base_x, dz - window->base_z, sw, sh, dw, dh,
                                      extra);
}

int
ToriRSServer_SceneCheckvis(
    int checkvis,
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z)
{
    /* HuntVis.OFF / LINEOFSIGHT / LINEOFWALK — LostCity HuntVis.ts. */
    if( checkvis == 1 )
        return ToriRSServer_SceneLineOfSight(level, from_x, from_z, to_x, to_z, 1, 1, 1, 1,
                                           0);
    if( checkvis == 2 )
        return ToriRSServer_SceneLineOfWalk(level, from_x, from_z, to_x, to_z, 1, 1, 1, 1, 0);
    return 1;
}

int
ToriRSServer_SceneApproached(
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
    struct ToriRSServerSceneWindow* window = window_containing2(sx, sz, dx, dz);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
        return 0;
    return collision_map_approached(map, sx - window->base_x, sz - window->base_z,
                                    dx - window->base_x, dz - window->base_z, sw, sh, dw, dh);
}

int
ToriRSServer_SceneApproachedPvp(
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
    struct ToriRSServerSceneWindow* window = window_containing2(sx, sz, dx, dz);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
        return 0;
    if( !ToriRSServer_SceneFeatures()->los_symmetric_pvp )
        return ToriRSServer_SceneApproached(level, sx, sz, dx, dz, sw, sh, dw, dh);
    return collision_map_approached_symmetric(map, sx - window->base_x, sz - window->base_z,
                                              dx - window->base_x, dz - window->base_z, sw, sh,
                                              dw, dh);
}

/*
 * Occupancy is BROADCAST: every built window covering the tile gets the stamp,
 * not just the bound one. This is half of the coherence invariant that lets a
 * query resolve through any covering window — an npc stamped only into the
 * window that happened to be bound would be walk-through-able from a player
 * whose window also covers it. Adds are ORs and removals are followed by
 * world_occupancy_restamp after rebuilds, so over-stamping costs nothing.
 */
void
ToriRSServer_SceneChangeOccupancy(
    int level,
    int x,
    int z,
    int size,
    int mask,
    int add)
{
    if( size < 1 || mask == 0 )
        return;
    scene_windows_ensure();
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        struct ToriRSServerSceneWindow* window = &g_windows[i];
        struct CollisionMap* map;

        if( !window_contains(window, x, z) )
            continue;
        map = window_collision(window, level);
        if( !map )
            continue;
        collision_map_change_square(map, x - window->base_x, z - window->base_z, size, mask,
                                    add);
    }
}

int
ToriRSServer_SceneNaivePath(
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
    struct ToriRSServerSceneWindow* window = window_containing2(src_x, src_z, dest_x, dest_z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;
    int sx;
    int sz;
    int ok;

    assert(out_x);
    assert(out_z);
    if( !map )
        return 0;
    ok = collision_map_naive_path(map, src_x - window->base_x, src_z - window->base_z,
                                  dest_x - window->base_x, dest_z - window->base_z, src_width,
                                  src_height, dest_width, dest_height, extra_flag, rng, &sx,
                                  &sz);
    if( !ok )
        return 0;
    *out_x = sx + window->base_x;
    *out_z = sz + window->base_z;
    if( !window_contains(window, *out_x, *out_z) )
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
ToriRSServer_SceneLocApproach(
    int slot,
    struct CollisionApproach* out)
{
    struct ToriRSServerSceneLoc* loc;
    struct ToriRS_FeatureTable const* features = ToriRSServer_SceneFeatures();
    const struct RSCache_Dat2ConfigLoc* config;
    int shape;
    int sized;
    int force;

    assert(out);
    memset(out, 0, sizeof(*out));
    out->mover_size = 1;
    out->kind = COLL_APPROACH_EXACT;

    loc = ToriRSServer_SceneLoc(slot);
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
ToriRSServer_SceneNpcApproach(
    int size,
    struct CollisionApproach* out)
{
    struct ToriRS_FeatureTable const* features = ToriRSServer_SceneFeatures();
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
ToriRSServer_SceneObjApproach(
    int retry_adjacent,
    struct CollisionApproach* out)
{
    struct ToriRS_FeatureTable const* features = ToriRSServer_SceneFeatures();

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
ToriRSServer_SceneOpNearestOpts(struct CollisionNearestOpts* out)
{
    struct ToriRS_FeatureTable const* features = ToriRSServer_SceneFeatures();

    assert(out);
    out->range = features->op_click_nearest_range;
    out->max_dist = 100;
    out->rank_by_rect_distance = features->nearest_ranks_by_rect_distance;
}

void
ToriRSServer_SceneGroundNearestOpts(struct CollisionNearestOpts* out)
{
    struct ToriRS_FeatureTable const* features = ToriRSServer_SceneFeatures();

    assert(out);
    collision_nearest_opts_from_model(features->ground_click_nearest_model, out);
    /* The click landed where no route can end: walk as close as the flood got
     * rather than not at all. Ground/minimap only — see the field. */
    out->unbounded = features->ground_click_nearest_unbounded;
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
    if( !getenv("TORIRSSERVER_VERBOSE") )
        return;
    fprintf(stderr,
            "torirsserver: route level=%d from=%d,%d to=%d,%d steps=%d nearest=%d arrive=%d,%d\n",
            level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
}

int
ToriRSServer_SceneRoute(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    int* path_x,
    int* path_z,
    int max_steps)
{
    struct ToriRSServerSceneWindow* window = window_containing2(from_x, from_z, to_x, to_z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;
    int steps;
    int nearest = 0;
    int arrive_x = to_x;
    int arrive_z = to_z;

    if( from_x == to_x && from_z == to_z )
        return 0;

    if( !map )
    {
        /* No collision anywhere (cache missing): the announced open-field
         * fallback. */
        if( !scene_any_window_built() )
            return route_straight(from_x, from_z, to_x, to_z, path_x, path_z, max_steps);
        /* Collision exists but no one window covers both endpoints: the target
         * is unreachable — do not straight-line through walls the map has not
         * been asked about. */
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }

    {
        /*
         * The unreachable fallback for a bare ground / minimap click. Under the
         * osrs era this is the official BOX10_RECT search, which is what makes
         * a click across a river or into a walled compound walk the player up
         * to the obstacle instead of doing nothing: the modern client sends
         * MOVE_GAMECLICK and no path, so every bit of "as close as it can get"
         * is decided right here.
         */
        struct CollisionNearestOpts nearest_opts;

        ToriRSServer_SceneGroundNearestOpts(&nearest_opts);
        steps = collision_map_route_tiles(map, from_x - window->base_x, from_z - window->base_z,
                                          to_x - window->base_x, to_z - window->base_z, NULL,
                                          &nearest_opts, path_x, path_z, max_steps, &nearest,
                                          &arrive_x, &arrive_z);
    }
    if( steps < 0 )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }
    for( int i = 0; i < steps; i++ )
    {
        path_x[i] += window->base_x;
        path_z[i] += window->base_z;
    }
    arrive_x += window->base_x;
    arrive_z += window->base_z;
    route_trace(level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
    return steps;
}

int
ToriRSServer_SceneRouteOp(
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
    struct ToriRSServerSceneWindow* window = window_containing2(from_x, from_z, to_x, to_z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;
    struct CollisionNearestOpts nearest_opts;
    int steps;
    int nearest = 0;
    int arrive_x = to_x;
    int arrive_z = to_z;

    assert(approach);
    assert(path_x);
    assert(path_z);

    if( from_x == to_x && from_z == to_z &&
        ( !map || collision_map_reached(map, from_x - window->base_x, from_z - window->base_z,
                                        to_x - window->base_x, to_z - window->base_z,
                                        approach) ) )
    {
        if( out_arrive_x )
            *out_arrive_x = from_x;
        if( out_arrive_z )
            *out_arrive_z = from_z;
        return 0;
    }

    if( !map )
    {
        /* Same split as ToriRSServer_SceneRoute: open-field fallback only when
         * no collision exists anywhere; otherwise out-of-window is a refusal. */
        if( !scene_any_window_built() )
            return route_straight(from_x, from_z, to_x, to_z, path_x, path_z, max_steps);
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }

    ToriRSServer_SceneOpNearestOpts(&nearest_opts);
    steps = collision_map_route_tiles(map, from_x - window->base_x, from_z - window->base_z,
                                      to_x - window->base_x, to_z - window->base_z, approach,
                                      &nearest_opts, path_x, path_z, max_steps, &nearest,
                                      &arrive_x, &arrive_z);
    if( steps < 0 )
    {
        route_trace(level, from_x, from_z, to_x, to_z, -1, 0, to_x, to_z);
        return -1;
    }
    for( int i = 0; i < steps; i++ )
    {
        path_x[i] += window->base_x;
        path_z[i] += window->base_z;
    }
    arrive_x += window->base_x;
    arrive_z += window->base_z;
    if( out_arrive_x )
        *out_arrive_x = arrive_x;
    if( out_arrive_z )
        *out_arrive_z = arrive_z;
    route_trace(level, from_x, from_z, to_x, to_z, steps, nearest, arrive_x, arrive_z);
    return steps;
}

int
ToriRSServer_SceneReached(
    int level,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    struct ToriRSServerSceneWindow* window = window_containing2(x, z, dst_x, dst_z);
    struct CollisionMap* map = window ? window_collision(window, level) : NULL;

    if( !map )
    {
        if( !scene_any_window_built() )
        {
            /* No collision anywhere: fall back to Chebyshev adjacent / exact
             * for EXACT. */
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
        return 0;
    }
    return collision_map_reached(map, x - window->base_x, z - window->base_z,
                                 dst_x - window->base_x, dst_z - window->base_z, approach);
}
