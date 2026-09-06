/*
 * Vessels — spawn, steer, move (docs/SAILING_PLAN.md S1).
 *
 * The mover here is the server half of OSRS sailing's world-entity model: the
 * deck is a stationary map instance, and what "moves" is a transform — a fine
 * position, a yaw and a hull rectangle — that this file advances once per world
 * tick. Nothing here touches the wire; S2's WORLDENTITY_INFO reads the state
 * this file maintains.
 *
 * Three decisions worth restating where the code can enforce them:
 *
 * - **Boats have their own map.** Sea terrain opens the hull navigation map;
 *   land, unknown terrain and shore obstacles block it. Walking-player flags
 *   and occupancy never decide where a hull may sail.
 *
 * - **Steps are quantized, headings are not sacred.** Every per-tick step is a
 *   multiple of 32 fine units per axis (the quarter-tile quantum the client's
 *   interpolation expects), but the trig displacement it approximates is
 *   carried exactly in per-axis residuals, so a long diagonal sail lands where
 *   the commanded heading says rather than where the rounding drifted.
 *
 * - **A blocked step stops the boat.** No sliding along the coast, no partial
 *   step: the hull either takes the whole quantized step onto water or parks
 *   (state -> IDLE) with its position untouched. Boat-vs-boat collision does
 *   not exist, faithfully to the deob.
 */

#include "torirs_server_vessel.h"

#include "engine/world_builder/collision_map.h"
#include "torirs_server.h"
#include "torirs_server_mapinstance.h"
#include "torirs_server_scene.h"
#include "world/wev.h"

#include <rscache.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Monotonic hull identity, never recycled (docs/SAILING_PLAN.md S2.1).
 *
 * Process-wide rather than per-world for the same reason the map-instance pool
 * is: a serial only has to be unequal, and one counter cannot hand the same
 * number to two live hulls however many worlds exist.
 */
static int g_vessel_serial;

/* ------------------------------------------------------------------ */
/* Fixed-point trig                                                    */
/* ------------------------------------------------------------------ */

/*
 * 65536-scaled sine over the 2048-unit circle, built lazily from libm (already
 * linked; torirs_server_combat.c uses math.h the same way). Server-side only, so
 * 64-bit intermediates are fine — the no-64-bit rule covers 3rd/toridraw.
 */
static int32_t s_sin16[TORIRSSERVER_VESSEL_ANGLE_UNITS];
static int s_sin16_ready;

static void
vessel_trig_init(void)
{
    static const double k_two_pi = 6.283185307179586476925286766559;
    int i;

    if( s_sin16_ready )
        return;
    /* Truncation toward zero, NOT lround: this table must be byte-identical
     * to the client's (3rd/toridraw shared_tables.c builds its sin table
     * with a plain cast), because the deck→root projection below must land
     * on the same tile the client's Wev_ParentFromDeck computes. With
     * lround here and truncation there, the two ends disagreed by 1-2 fine
     * units at the twelve non-cardinal headings — enough to flap the
     * rider's projected shadow across a tile boundary. */
    for( i = 0; i < TORIRSSERVER_VESSEL_ANGLE_UNITS; i++ )
        s_sin16[i] =
            (int32_t)(sin((double)i * k_two_pi / TORIRSSERVER_VESSEL_ANGLE_UNITS) * 65536.0);
    s_sin16_ready = 1;
}

static int32_t
vessel_sin(int angle)
{
    vessel_trig_init();
    return s_sin16[angle & TORIRSSERVER_VESSEL_ANGLE_MASK];
}

static int32_t
vessel_cos(int angle)
{
    vessel_trig_init();
    return s_sin16[(angle + 512) & TORIRSSERVER_VESSEL_ANGLE_MASK];
}

/** Rotate a deck-local fine offset by yaw into a root-space offset. The bow
 *  direction (0, -1) lands on (-sin, -cos), agreeing with the mover.
 *  Floor shift, no rounding term: the client's Wev_ParentFromDeck floors,
 *  and the observer projection must land on the tile the client draws. */
static void
vessel_rotate_forward(
    int angle,
    int lx,
    int lz,
    int* out_rx,
    int* out_rz)
{
    int64_t c = vessel_cos(angle);
    int64_t s = vessel_sin(angle);

    *out_rx = (int)(((int64_t)lx * c + (int64_t)lz * s) >> 16);
    *out_rz = (int)(((int64_t)lz * c - (int64_t)lx * s) >> 16);
}

/** The transposed rotation — vessel_rotate_forward's inverse (exact up to
 *  the 16.16 floor). Floor shift like the forward, matching the client's
 *  Wev_DeckFromParent so both ends map a root point to the same deck tile. */
static void
vessel_rotate_inverse(
    int angle,
    int rx,
    int rz,
    int* out_lx,
    int* out_lz)
{
    int64_t c = vessel_cos(angle);
    int64_t s = vessel_sin(angle);

    *out_lx = (int)(((int64_t)rx * c - (int64_t)rz * s) >> 16);
    *out_lz = (int)(((int64_t)rz * c + (int64_t)rx * s) >> 16);
}

/* ------------------------------------------------------------------ */
/* Independent boat map and complete hull geometry                      */
/* ------------------------------------------------------------------ */

int
ToriRSServer_VesselTileSailable(int level, int tile_x, int tile_z)
{
    assert(level >= 0);
    assert(level < TORIRSSERVER_MAPINSTANCE_LEVELS);
    return (ToriRSServer_SceneBoatTileFlags(level, tile_x, tile_z) &
            (COLL_FLAG_WALK_BLOCKED | 0xff)) == 0;
}

/* The client and server decode the very same archive-72 records. Keeping the
 * native bounds separate from the deck reservation prevents custom instance
 * dimensions from shrinking a ship's collision or losing its signed offset. */
static struct WevConfigTable g_vessel_configs;
static char g_vessel_config_cache[1024];
static int g_vessel_configs_loaded;

static const struct WevConfig*
vessel_config(int id)
{
    const char* cache_dir;

    if( id == 0 ) return NULL; /* Synthetic geometry fixtures have no cache id. */
    cache_dir = ToriRSServer_WorldCacheDir();
    assert(cache_dir);
    assert(strlen(cache_dir) < sizeof(g_vessel_config_cache));
    if( !g_vessel_configs_loaded || strcmp(cache_dir, g_vessel_config_cache) )
    {
        struct RSCache_Dat2Disk* disk;
        struct RSCache_Dat2DiskArchive* archive = NULL;
        struct RSCache_FileList* files = NULL;
        struct RSCache profile = RSCache_ProfileForIdentity(
            RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, TORIRSSERVER_CACHE_REVISION, 0u);
        int count = 0;

        WevConfigTable_Free(&g_vessel_configs);
        snprintf(g_vessel_config_cache, sizeof(g_vessel_config_cache), "%s", cache_dir);
        g_vessel_configs_loaded = 1;
        disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
        if( !disk )
        {
            char relative[1100];
            snprintf(relative, sizeof(relative), "../%s", cache_dir);
            disk = RSCache_Dat2DiskNewFromDirectory(relative);
        }
        if( disk )
        {
            RSCache_Dat2DiskSetProfile(disk, &profile);
            archive = RSCache_Dat2DiskArchiveNewLoad(
                disk, RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
                RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
            if( archive && RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
                files = RSCache_FileListNewFromDecode(
                    archive->data, archive->data_size, archive->file_count);
        }
        if( files )
        {
            for( int i = 0; i < files->file_count; i++ )
            {
                int file_id = archive->file_ids ? archive->file_ids[i] : i;
                if( file_id + 1 > count ) count = file_id + 1;
            }
            if( count > 0 )
            {
                struct WevConfig* entries = calloc((size_t)count, sizeof(*entries));
                assert(entries);
                for( int i = 0; i < count; i++ ) WevConfig_Init(&entries[i], -1);
                for( int i = 0; i < files->file_count; i++ )
                {
                    int file_id = archive->file_ids ? archive->file_ids[i] : i;
                    if( file_id < 0 || files->file_sizes[i] <= 0 ) continue;
                    WevConfig_FreeContents(&entries[file_id]);
                    if( !WevConfig_Decode(&entries[file_id], file_id,
                            (const uint8_t*)files->files[i], files->file_sizes[i]) )
                        fprintf(stderr, "vessel: invalid native config %d\n", file_id);
                }
                WevConfigTable_Set(&g_vessel_configs, entries, count);
            }
        }
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
    }
    return WevConfigTable_Has(&g_vessel_configs, id)
               ? WevConfigTable_Get(&g_vessel_configs, id) : NULL;
}

static void
vessel_bound_rect(const struct ToriRSServerVessel* vessel,
                   int* half_x, int* half_z, int* off_x, int* off_z)
{
    const struct WevConfig* config = vessel_config(vessel->config_id);

    *half_x = vessel->size_x_tiles * 64;
    *half_z = vessel->size_z_tiles * 64;
    *off_x = *off_z = 0;
    if( config )
    {
        /* The Zenith (id9) has no authored bounds: its declared wire extent
         * is the documented fallback. Every ordinary boat uses native size. */
        if( config->bounds_w > 0 ) *half_x = config->bounds_w / 2;
        if( config->bounds_h > 0 ) *half_z = config->bounds_h / 2;
        *off_x = config->bounds_off_x;
        *off_z = config->bounds_off_z;
    }
}

struct VesselPoint
{
    int x, z;
};

static int64_t
vessel_cross(struct VesselPoint a, struct VesselPoint b, struct VesselPoint c)
{
    return (int64_t)(b.x - a.x) * (c.z - a.z) -
           (int64_t)(b.z - a.z) * (c.x - a.x);
}

static void
vessel_corners(const struct ToriRSServerVessel* vessel, int x, int z, int angle,
               struct VesselPoint* points)
{
    int hx, hz, off_x, off_z;
    static const int sign_x[4] = { -1, 1, 1, -1 };
    static const int sign_z[4] = { -1, -1, 1, 1 };

    vessel_bound_rect(vessel, &hx, &hz, &off_x, &off_z);
    for( int i = 0; i < 4; i++ )
    {
        vessel_rotate_forward(angle, off_x + sign_x[i] * hx, off_z + sign_z[i] * hz,
                              &points[i].x, &points[i].z);
        points[i].x += x;
        points[i].z += z;
    }
}

/* Monotone convex hull of two four-corner poses, including identical poses. */
static int
vessel_convex_hull(struct VesselPoint* points, struct VesselPoint* hull)
{
    int n = 0;
    int unique = 0;

    for( int i = 1; i < 8; i++ )
    {
        struct VesselPoint held = points[i];
        int j = i;
        while( j > 0 && (points[j - 1].x > held.x ||
                         (points[j - 1].x == held.x && points[j - 1].z > held.z)) )
        {
            points[j] = points[j - 1];
            j--;
        }
        points[j] = held;
    }
    for( int i = 0; i < 8; i++ )
        if( unique == 0 || points[i].x != points[unique - 1].x ||
                          points[i].z != points[unique - 1].z )
            points[unique++] = points[i];
    for( int i = 0; i < unique; i++ )
    {
        while( n >= 2 && vessel_cross(hull[n - 2], hull[n - 1], points[i]) <= 0 )
            n--;
        hull[n++] = points[i];
    }
    for( int i = unique - 2, lower = n + 1; i >= 0; i-- )
    {
        while( n >= lower && vessel_cross(hull[n - 2], hull[n - 1], points[i]) <= 0 )
            n--;
        hull[n++] = points[i];
    }
    return n - 1;
}

/* Separating axes of the convex hull, plus the tile's axes supplied by the
 * caller's AABB bounds. Exact edge contact is clear; positive overlap blocks.
 * margin bounds the small arc bulge between adjacent turn samples. */
static int
vessel_polygon_hits_tile(const struct VesselPoint* hull, int n, int tx, int tz,
                         int margin)
{
    int x0 = tx * 128 - margin;
    int z0 = tz * 128 - margin;
    int x1 = (tx + 1) * 128 + margin;
    int z1 = (tz + 1) * 128 + margin;

    for( int i = 0; i < n; i++ )
    {
        int next = (i + 1) % n;
        int nx = hull[next].z - hull[i].z;
        int nz = hull[i].x - hull[next].x;
        int64_t pmin = (int64_t)nx * hull[0].x + (int64_t)nz * hull[0].z;
        int64_t pmax = pmin;
        int64_t tmin = (int64_t)nx * (nx >= 0 ? x0 : x1) +
                       (int64_t)nz * (nz >= 0 ? z0 : z1);
        int64_t tmax = (int64_t)nx * (nx >= 0 ? x1 : x0) +
                       (int64_t)nz * (nz >= 0 ? z1 : z0);

        for( int j = 1; j < n; j++ )
        {
            int64_t p = (int64_t)nx * hull[j].x + (int64_t)nz * hull[j].z;
            if( p < pmin ) pmin = p;
            if( p > pmax ) pmax = p;
        }
        if( pmax <= tmin || tmax <= pmin )
            return 0;
    }
    return 1;
}

static int
vessel_swept_segment(const struct ToriRSServerVessel* vessel,
                     int x0, int z0, int a0, int x1, int z1, int a1, int margin)
{
    struct VesselPoint points[8];
    struct VesselPoint hull[16];
    int min_x, max_x, min_z, max_z;
    int n;

    vessel_corners(vessel, x0, z0, a0, points);
    vessel_corners(vessel, x1, z1, a1, points + 4);
    n = vessel_convex_hull(points, hull);
    assert(n >= 3);
    min_x = max_x = hull[0].x;
    min_z = max_z = hull[0].z;
    for( int i = 1; i < n; i++ )
    {
        if( hull[i].x < min_x ) min_x = hull[i].x;
        if( hull[i].x > max_x ) max_x = hull[i].x;
        if( hull[i].z < min_z ) min_z = hull[i].z;
        if( hull[i].z > max_z ) max_z = hull[i].z;
    }
    for( int tx = (min_x - margin) >> 7; tx <= (max_x + margin - 1) >> 7; tx++ )
        for( int tz = (min_z - margin) >> 7; tz <= (max_z + margin - 1) >> 7; tz++ )
            if( !ToriRSServer_VesselTileSailable(vessel->level, tx, tz) &&
                vessel_polygon_hits_tile(hull, n, tx, tz, margin) )
                return 0;
    return 1;
}

int
ToriRSServer_VesselCanOccupy(const struct ToriRSServerVessel* vessel,
                           int fine_x, int fine_z, int angle)
{
    assert(vessel);
    assert(vessel->in_use);
    return vessel_swept_segment(vessel, fine_x, fine_z, angle,
                               fine_x, fine_z, angle, 0);
}

/* Translation is checked continuously, not only at its destination. For a
 * turn, adjacent endpoint hulls enclose the chord swept by every corner; the
 * radius * (1 - cos(half arc)) margin encloses the arc outside that chord.
 * One extra fine unit covers the fixed-point corner rounding. */
static int
vessel_sweep_sailable(const struct ToriRSServerVessel* vessel,
                      int next_x, int next_z, int next_angle)
{
    int arc = (next_angle - vessel->angle) & TORIRSSERVER_VESSEL_ANGLE_MASK;
    int steps;
    int x0 = vessel->fine_x;
    int z0 = vessel->fine_z;
    int a0 = vessel->angle;
    int hx, hz, off_x, off_z;
    double radius;

    vessel_bound_rect(vessel, &hx, &hz, &off_x, &off_z);
    radius = hypot(hx + abs(off_x), hz + abs(off_z));
    if( arc > 1024 ) arc -= 2048;
    steps = (abs(arc) + 15) / 16;
    if( steps < 1 ) steps = 1;
    for( int i = 1; i <= steps; i++ )
    {
        int x1 = vessel->fine_x + (int)((int64_t)(next_x - vessel->fine_x) * i / steps);
        int z1 = vessel->fine_z + (int)((int64_t)(next_z - vessel->fine_z) * i / steps);
        int a1 = vessel->angle + arc * i / steps;
        double half_arc = abs(a1 - a0) * (3.14159265358979323846 / 2048.0);
        int margin = arc ? (int)ceil(radius * (1.0 - cos(half_arc))) + 1 : 0;

        if( !vessel_swept_segment(vessel, x0, z0, a0, x1, z1, a1, margin) )
            return 0;
        x0 = x1;
        z0 = z1;
        a0 = a1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int
ToriRSServer_VesselSpawn(
    struct ToriRSServer* srv,
    int config_id,
    int size_x_tiles,
    int size_z_tiles,
    int level,
    int tile_x,
    int tile_z,
    int angle)
{
    struct ToriRSServerVessel* vessel;
    int slot;
    int zone_w;
    int zone_h;
    int instance;

    assert(srv);
    assert(config_id >= 0);
    assert(size_x_tiles > 0);
    assert(size_z_tiles > 0);
    assert(level >= 0);
    assert(level < TORIRSSERVER_MAPINSTANCE_LEVELS);
    assert(tile_x >= 0);
    assert(tile_z >= 0);
    assert(angle >= 0);
    assert(angle < TORIRSSERVER_VESSEL_ANGLE_UNITS);

    for( slot = 0; slot < TORIRSSERVER_VESSEL_MAX; slot++ )
        if( !srv->vessels[slot].in_use )
            break;
    /* Pool exhaustion is a capacity bug, not a state content can reach: 32
     * hulls outruns the 8-instance deck pool four times over. */
    assert(slot < TORIRSSERVER_VESSEL_MAX);

    /* Deck reservation, rounded up to whole zones (8 tiles each). The
     * instance pool being exhausted IS a state content can reach, and 0 is the
     * same "check your handle" answer map_instance_alloc gives. */
    zone_w = (size_x_tiles + 7) / 8;
    zone_h = (size_z_tiles + 7) / 8;
    /* 13 zones is the client's hard grid stride (REBUILD_WORLDENTITY decodes
     * onto the 13x13 instance array, gameproto_parse.c) — and 14+ would also
     * index the encoder's own zones[4][13][13] out of bounds. The instance
     * pool accepts 16, so refuse here, where the size is still a request. */
    if( zone_w > 13 || zone_h > 13 )
        return 0;
    instance = ToriRSServer_MapInstanceAlloc(ToriRSServer_WorldCacheDir(), zone_w, zone_h);
    if( instance == 0 )
        return 0;
    /* The vessel owns the deck's lifetime: an empty deck is a boat nobody
     * boarded, not an abandoned instance for the linger clock to reap. */
    ToriRSServer_MapInstanceSetLinger(instance, 0);

    vessel = &srv->vessels[slot];
    memset(vessel, 0, sizeof(*vessel));
    vessel->in_use = 1;
    vessel->index = slot + 1;
    /* Never reused, unlike the slot and the view id. See the field's comment:
     * this is the only thing that tells a wire encoder "the hull under view 1
     * is not the hull you were told about", when a free and a spawn land in
     * the same tick and both recycle the same numbers. */
    vessel->serial = ++g_vessel_serial;
    vessel->seq_id = -1;
    /* Lowest free world-view id. 0 when all 15 are taken: the hull still sails,
     * it just has no name the wire can say — see the field's comment. */
    for( int view = 1; view <= TORIRSSERVER_WEV_VIEW_MAX; view++ )
    {
        int taken = 0;

        for( int i = 0; i < TORIRSSERVER_VESSEL_MAX && !taken; i++ )
            if( srv->vessels[i].in_use && srv->vessels[i].view_id == view )
                taken = 1;
        if( !taken )
        {
            vessel->view_id = view;
            break;
        }
    }
    vessel->config_id = config_id;
    vessel->size_x_tiles = size_x_tiles;
    vessel->size_z_tiles = size_z_tiles;
    /* Full hull, scaled by footprint: the bar reads as the boat's bulk and a
     * bigger hull has more of it. Values are arbitrary until hull damage
     * exists; the SHAPE (hp == hp_max at spawn, both nonzero) is what the
     * sidepanel's 0/0 placeholder needed. */
    vessel->hp_max = 50 * (size_x_tiles > 0 ? size_x_tiles : 1) *
                     (size_z_tiles > 0 ? size_z_tiles : 1);
    vessel->hp = vessel->hp_max;
    /* Deterministic per slot, small enough for the shortest option table:
     * the composed name is stable across respawns of the same slot, and two
     * hulls afloat at once read differently. */
    {
        int slot = (int)(vessel - srv->vessels);

        vessel->name_descriptor = 1 + (slot * 7) % 20;
        vessel->name_noun = 1 + (slot * 13 + 5) % 20;
    }
    vessel->deck_src_x = -1;
    vessel->deck_src_z = -1;
    vessel->instance = instance;
    vessel->level = level;
    vessel->fine_x = tile_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->fine_z = tile_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->angle = angle;
    vessel->state = TORIRSSERVER_VESSEL_IDLE;
    /* Nearest compass point to the spawn yaw, so a bare "set speed and go"
     * sails the way the hull already faces. */
    vessel->heading = ((angle + TORIRSSERVER_VESSEL_HEADING_STEP / 2) /
                      TORIRSSERVER_VESSEL_HEADING_STEP) &
                      15;
    vessel->speed_tier = TORIRSSERVER_VESSEL_SPEED_TIER_MIN;
    vessel->base_speed_fine = 64;
    vessel->speed_cap_fine = 256;
    vessel->turn_rate = TORIRSSERVER_VESSEL_TURN_RATE_DEFAULT;

    /* Lowest free deck-window slot (see the field's comment). 0 when all are
     * taken: the hull still sails, riders of THIS hull just lose deck
     * collision once their own window follows the hull away from the pool. */
    for( int wi = 0; wi < TORIRSSERVER_SCENE_VESSEL_WINDOW_MAX; wi++ )
    {
        int pool_index = TORIRSSERVER_SCENE_VESSEL_WINDOW_BASE + wi;
        int taken = 0;

        for( int i = 0; i < TORIRSSERVER_VESSEL_MAX && !taken; i++ )
            if( srv->vessels[i].in_use && srv->vessels[i].deck_window == pool_index )
                taken = 1;
        if( !taken )
        {
            vessel->deck_window = pool_index;
            break;
        }
    }
    if( vessel->deck_window == 0 )
        fprintf(stderr,
                "torirsserver: vessel %d spawned with no free deck window — "
                "riders cannot walk this deck while it sails\n",
                vessel->index);

    srv->vessel_count++;
    return vessel->index;
}

int
ToriRSServer_VesselFree(
    struct ToriRSServer* srv,
    int handle)
{
    struct ToriRSServerVessel* vessel;

    assert(srv);
    vessel = ToriRSServer_VesselGet(srv, handle);
    if( !vessel )
        return 0;

    /*
     * Nobody is left standing on a deck that is about to stop existing
     * (docs/sailing_coverage.csv SAIL-54).
     *
     * A deck tile is a pool square — hundreds of squares off the real map,
     * reachable by no route, and once the vessel is gone `obs_*` collapses back
     * onto that raw tile so no other client can see them either. A rider left
     * behind is not misplaced, they are deleted from the game while still
     * logged in.
     *
     * They are put down where they LOOKED like they were standing: their own
     * deck tile projected through the hull's final transform, which is the last
     * place every other client saw them. The disembark then reads as the hull
     * vanishing from under them rather than as a teleport across the map.
     * Whether that root tile is open water is content's business — a scuttling
     * script that wants a dock moves them first; the engine's job is only to
     * refuse to strand them in the pool.
     *
     * Before the instance release, because both the ownership test and the
     * projection belong to the vessel and neither survives it.
     */
    {
        struct ToriRSServerPlayer* was_active = srv->active_player;

        for( int i = 0; i < srv->player_count; i++ )
        {
            struct ToriRSServerPlayer* player = &srv->players[i];
            int fine_x = 0;
            int fine_z = 0;

            if( !player->active )
                continue;
            if( ToriRSServer_VesselAtTile(srv, player->x, player->z) != vessel )
                continue;
            ToriRSServer_VesselDeckTileToRoot(vessel, player->x, player->z, &fine_x, &fine_z);
            /* WorldTeleport acts on the bound player, so bind each in turn and
             * put the previous binding back — the same save/restore every
             * world-scoped helper called from outside a tick does. */
            ToriRSServer_WorldSetActive(srv, player);
            ToriRSServer_WorldTeleport(srv, vessel->level, fine_x >> 7, fine_z >> 7);
        }
        ToriRSServer_WorldSetActive(srv, was_active);
    }

    /* The deck's pinned collision window goes with the deck. */
    if( vessel->deck_window != 0 )
        ToriRSServer_SceneWindowRelease(
            ToriRSServer_SceneWindowByIndex(vessel->deck_window));

    /* The world-level release, not the bare registry one: the deck may hold
     * npcs, floor objects and loc changes, and the pool re-issues its squares
     * immediately (see ToriRSServer_WorldMapInstanceFree). */
    ToriRSServer_WorldMapInstanceFree(srv, vessel->instance);
    memset(vessel, 0, sizeof(*vessel));
    srv->vessel_count--;
    return 1;
}

struct ToriRSServerVessel*
ToriRSServer_VesselGet(
    struct ToriRSServer* srv,
    int handle)
{
    struct ToriRSServerVessel* vessel;

    assert(srv);
    if( handle < 1 || handle > TORIRSSERVER_VESSEL_MAX )
        return NULL;
    vessel = &srv->vessels[handle - 1];
    return vessel->in_use ? vessel : NULL;
}

int
ToriRSServer_VesselLiveCount(struct ToriRSServer* srv)
{
    assert(srv);
    return srv->vessel_count;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

void
ToriRSServer_VesselSetTarget(
    struct ToriRSServerVessel* vessel,
    int tile_x,
    int tile_z)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(tile_x >= 0);
    assert(tile_z >= 0);

    /* Tile centers (tile*128 + 64) are already 32-unit-quantum aligned, so
     * arrival can snap to the target without breaking the step invariant. */
    vessel->target_fine_x = tile_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->target_fine_z = tile_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->state = TORIRSSERVER_VESSEL_TARGET;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

void
ToriRSServer_VesselSetHeading(
    struct ToriRSServerVessel* vessel,
    int heading)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(heading >= 0);
    assert(heading < 16);

    vessel->heading = heading;
    vessel->state = TORIRSSERVER_VESSEL_HEADING;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

void
ToriRSServer_VesselSetSpeed(
    struct ToriRSServerVessel* vessel,
    int speed_tier)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(speed_tier >= TORIRSSERVER_VESSEL_SPEED_TIER_MIN);
    assert(speed_tier <= TORIRSSERVER_VESSEL_SPEED_TIER_MAX);

    int cap = vessel->speed_cap_fine > 0 ? vessel->speed_cap_fine : 256;
    int max_tier = cap / 64;
    if( max_tier < 1 ) max_tier = 1;
    vessel->speed_tier = speed_tier < max_tier ? speed_tier : max_tier;
}

void
ToriRSServer_VesselStop(struct ToriRSServerVessel* vessel)
{
    assert(vessel);
    assert(vessel->in_use);

    vessel->state = TORIRSSERVER_VESSEL_IDLE;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

/* ------------------------------------------------------------------ */
/* Mover                                                               */
/* ------------------------------------------------------------------ */

/** The 16-point compass heading whose sail direction (-sin, -cos) best points
 *  along (dx, dz). Solving -sin t = dx, -cos t = dz gives t = atan2(-dx, -dz),
 *  rounded to the nearest multiple of 128. */
static int
vessel_heading_toward(
    int dx,
    int dz)
{
    static const double k_two_pi = 6.283185307179586476925286766559;
    int angle;

    angle = (int)lround(
                atan2((double)-dx, (double)-dz) * (TORIRSSERVER_VESSEL_ANGLE_UNITS / k_two_pi)) &
            TORIRSSERVER_VESSEL_ANGLE_MASK;
    return ((angle + TORIRSSERVER_VESSEL_HEADING_STEP / 2) / TORIRSSERVER_VESSEL_HEADING_STEP) & 15;
}

int
ToriRSServer_VesselHeadingToward(
    int dx,
    int dz)
{
    return vessel_heading_toward(dx, dz);
}

/** Round a fine displacement to the nearest 32-unit quantum (half rounds up;
 *  the arithmetic shift makes that hold for negatives too). */
static int
vessel_quantize(int fine)
{
    return ((fine + TORIRSSERVER_VESSEL_FINE_QUANTUM / 2) >> 5) << 5;
}

/**
 * Per-tick mover trace, off unless TORIRS_VESSEL_DEBUG is set.
 *
 * The mover's failure modes are all silent: a blocked step parks the hull and
 * every later tick returns at the IDLE guard, so "sailed once and stopped" and
 * "never ticked at all" look identical from outside. This names which.
 */
static int
vessel_debug_enabled(void)
{
    static int cached = -1;

    if( cached < 0 )
        cached = getenv("TORIRS_VESSEL_DEBUG") ? 1 : 0;
    return cached;
}

static void
vessel_tick(struct ToriRSServerVessel* vessel)
{
    int desired;
    int arc;
    int next_angle;
    int next_x = vessel->fine_x;
    int next_z = vessel->fine_z;
    int next_residual_x = vessel->residual_x;
    int next_residual_z = vessel->residual_z;
    int speed = vessel->speed_tier * 64;
    int arrived = 0;
    int cap = vessel->speed_cap_fine > 0 ? vessel->speed_cap_fine : 256;

    if( speed > cap ) speed = cap;
    if( vessel->state == TORIRSSERVER_VESSEL_IDLE )
        return;
    if( vessel->state == TORIRSSERVER_VESSEL_TARGET )
    {
        int dx = vessel->target_fine_x - vessel->fine_x;
        int dz = vessel->target_fine_z - vessel->fine_z;
        if( dx == 0 && dz == 0 )
        {
            ToriRSServer_VesselStop(vessel);
            return;
        }
        vessel->heading = vessel_heading_toward(dx, dz);
    }

    desired = vessel->heading * TORIRSSERVER_VESSEL_HEADING_STEP;
    arc = (desired - vessel->angle) & TORIRSSERVER_VESSEL_ANGLE_MASK;
    if( arc > TORIRSSERVER_VESSEL_ANGLE_UNITS / 2 )
        arc -= TORIRSSERVER_VESSEL_ANGLE_UNITS;
    if( arc > vessel->turn_rate )
        arc = vessel->turn_rate;
    else if( arc < -vessel->turn_rate )
        arc = -vessel->turn_rate;
    next_angle = (vessel->angle + arc) & TORIRSSERVER_VESSEL_ANGLE_MASK;

    /* Sails-down turns still collide. Reverse is a stationary half-tile nudge. */
    if( vessel->state == TORIRSSERVER_VESSEL_HEADING && !vessel->sails_set )
        speed = vessel->reversing ? -64 : 0;

    if( vessel->state == TORIRSSERVER_VESSEL_TARGET )
    {
        int dx = vessel->target_fine_x - vessel->fine_x;
        int dz = vessel->target_fine_z - vessel->fine_z;
        /* Euclidean arrival avoids a diagonal final snap exceeding speed. */
        if( (int64_t)dx * dx + (int64_t)dz * dz <= (int64_t)speed * speed )
        {
            next_x = vessel->target_fine_x;
            next_z = vessel->target_fine_z;
            arrived = 1;
        }
    }
    if( !arrived && speed != 0 )
    {
        int ideal_x = (int)((-(int64_t)vessel_sin(next_angle) * speed + 32768) >> 16);
        int ideal_z = (int)((-(int64_t)vessel_cos(next_angle) * speed + 32768) >> 16);
        int want_x = ideal_x + vessel->residual_x;
        int want_z = ideal_z + vessel->residual_z;
        int step_x = vessel_quantize(want_x);
        int step_z = vessel_quantize(want_z);

        next_residual_x = want_x - step_x;
        next_residual_z = want_z - step_z;
        next_x += step_x;
        next_z += step_z;
    }

    if( !vessel_sweep_sailable(vessel, next_x, next_z, next_angle) )
    {
        if( vessel_debug_enabled() )
            fprintf(stderr,
                    "vessel: boat collision refused pose %d,%d,%d -> %d,%d,%d; parked\n",
                    vessel->fine_x, vessel->fine_z, vessel->angle,
                    next_x, next_z, next_angle);
        vessel->blocked_notice = 1;
        ToriRSServer_VesselStop(vessel);
        return;
    }

    /* Commit the pose together: a blocked turn must not leave half a hull in
     * the shore while the position remains at the previous tick. */
    vessel->angle = next_angle;
    vessel->fine_x = next_x;
    vessel->fine_z = next_z;
    vessel->residual_x = next_residual_x;
    vessel->residual_z = next_residual_z;
    if( arrived )
        ToriRSServer_VesselStop(vessel);
}

int
ToriRSServer_VesselNearest(
    struct ToriRSServer* srv,
    int tile_x,
    int tile_z,
    int level,
    int range)
{
    int best = 0;
    int best_gap = 0;

    assert(srv);
    if( range < 0 )
        return 0;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
    {
        struct ToriRSServerVessel* vessel = &srv->vessels[i];
        int hx;
        int hz;
        int dx;
        int dz;
        int gap;

        if( !vessel->in_use || vessel->level != level )
            continue;
        hx = vessel->fine_x >> 7;
        hz = vessel->fine_z >> 7;
        dx = hx > tile_x ? hx - tile_x : tile_x - hx;
        dz = hz > tile_z ? hz - tile_z : tile_z - hz;
        gap = dx > dz ? dx : dz;
        if( gap > range )
            continue;
        if( best == 0 || gap < best_gap )
        {
            best = vessel->index;
            best_gap = gap;
        }
    }
    return best;
}

int
ToriRSServer_VesselBoardPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    struct ToriRSServerVessel* vessel)
{
    int base_tile_x = 0;
    int base_tile_z = 0;
    int level;

    assert(srv);
    assert(player);
    assert(vessel);
    if( !ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z) )
        return 0;
    /* The native pivot puts the raft's one-tile deck at local x3, not x4.
     * Search actual walkable deck tiles around that pivot and stay inside the
     * native hull extent; an empty outer instance tile is not a gangplank. */
    level = ToriRSServer_VesselDeckPlane(vessel);
    const struct WevConfig* config = vessel_config(vessel->config_id);
    int zones_x = (vessel->size_x_tiles + 7) / 8;
    int zones_z = (vessel->size_z_tiles + 7) / 8;
    int pivot_x = zones_x * 512 + (config ? config->pivot_x : 0);
    int pivot_z = zones_z * 512 + (config ? config->pivot_z : 0);
    int center_x = pivot_x >> 7;
    int center_z = pivot_z >> 7;
    int hx, hz, off_x, off_z;
    vessel_bound_rect(vessel, &hx, &hz, &off_x, &off_z);
    for( int radius = 0; radius < 16; radius++ )
        for( int dz = -radius; dz <= radius; dz++ )
            for( int dx = -radius; dx <= radius; dx++ )
            {
                if( radius && abs(dx) != radius && abs(dz) != radius ) continue;
                int x = center_x + dx;
                int z = center_z + dz;
                int local_x = x * 128 + 64 - pivot_x - off_x;
                int local_z = z * 128 + 64 - pivot_z - off_z;
                if( x < 0 || z < 0 || x >= zones_x * 8 || z >= zones_z * 8 ||
                    local_x < -hx || local_x >= hx || local_z < -hz || local_z >= hz ||
                    ToriRSServer_SceneWalkBlocked(level, base_tile_x + x, base_tile_z + z) )
                    continue;
                ToriRSServer_WorldSetActive(srv, player);
                ToriRSServer_WorldTeleport(srv, level, base_tile_x + x, base_tile_z + z);
                return 1;
            }
    return 0;
}

int
ToriRSServer_VesselDisembarkPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    struct ToriRSServerVessel* vessel;
    int anchor_x;
    int anchor_z;

    assert(srv);
    assert(player);
    vessel = ToriRSServer_VesselAtTile(srv, player->x, player->z);
    if( !vessel )
        return 0;
    /* Ashore FROM the hull's projected position: rings outward, nearest
     * first, and the first WALKABLE root tile wins. At sea every ring is
     * water and the search fails — which is the right answer, not a bug: a
     * boat in open water has no shore, and the caller says so. */
    anchor_x = vessel->fine_x >> 7;
    anchor_z = vessel->fine_z >> 7;
    for( int ring = 1; ring <= TORIRSSERVER_VESSEL_DISEMBARK_RANGE; ring++ )
        for( int dz = -ring; dz <= ring; dz++ )
            for( int dx = -ring; dx <= ring; dx++ )
            {
                int tx;
                int tz;

                /* The ring's edge only — the inside was searched already. */
                if( dx > -ring && dx < ring && dz > -ring && dz < ring )
                    continue;
                tx = anchor_x + dx;
                tz = anchor_z + dz;
                if( ToriRSServer_SceneWalkBlocked(vessel->level, tx, tz) )
                    continue;
                /* Water is walk-clear for a hull and not for a person: the
                 * gangplank must land on ground, not on the sea the boat is
                 * floating in. */
                if( ToriRSServer_VesselTileSailable(vessel->level, tx, tz) )
                    continue;
                ToriRSServer_WorldSetActive(srv, player);
                ToriRSServer_WorldTeleport(srv, vessel->level, tx, tz);
                return 1;
            }
    return 0;
}

int
ToriRSServer_VesselTakeBlocked(struct ToriRSServerVessel* vessel)
{
    int blocked;

    assert(vessel);
    blocked = vessel->blocked_notice;
    vessel->blocked_notice = 0;
    return blocked;
}

void
ToriRSServer_VesselTickAll(struct ToriRSServer* srv)
{
    int i;

    assert(srv);
    if( srv->vessel_count == 0 )
        return;
    for( i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use )
            vessel_tick(&srv->vessels[i]);
}

/* ------------------------------------------------------------------ */
/* Deck <-> root projection                                            */
/* ------------------------------------------------------------------ */

/** Native archive-72 pivot, decoded by the same code as the client. */
static void
vessel_config_pivot(int config_id, int* out_px, int* out_pz)
{
    const struct WevConfig* config = vessel_config(config_id);
    *out_px = config ? config->pivot_x : 0;
    *out_pz = config ? config->pivot_z : 0;
}

int
ToriRSServer_VesselDeckPlane(const struct ToriRSServerVessel* vessel)
{
    const struct WevConfig* config;
    assert(vessel);
    config = vessel_config(vessel->config_id);
    return config ? config->plane : 0;
}

/** The recenter the CLIENT applies: half the ZONE-ROUNDED deck box (the wire
 *  publishes zone counts, so the client's view is zones×8 tiles regardless of
 *  the hull's own size) plus the config pivot. One helper so DeckToRoot and
 *  RootToDeck cannot drift apart. */
static void
vessel_recenter_fine(
    const struct ToriRSServerVessel* vessel,
    int* out_cx,
    int* out_cz)
{
    int px;
    int pz;

    vessel_config_pivot(vessel->config_id, &px, &pz);
    *out_cx = ((vessel->size_x_tiles + 7) / 8) * 8 * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2) +
              px;
    *out_cz = ((vessel->size_z_tiles + 7) / 8) * 8 * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2) +
              pz;
}

void
ToriRSServer_VesselDeckToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_fine_x,
    int deck_fine_z,
    int* out_fine_x,
    int* out_fine_z)
{
    int cx;
    int cz;
    int lx;
    int lz;
    int rx;
    int rz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_fine_x);
    assert(out_fine_z);

    vessel_recenter_fine(vessel, &cx, &cz);
    lx = deck_fine_x - cx;
    lz = deck_fine_z - cz;
    vessel_rotate_forward(vessel->angle, lx, lz, &rx, &rz);
    *out_fine_x = vessel->fine_x + rx;
    *out_fine_z = vessel->fine_z + rz;
}

void
ToriRSServer_VesselRootToDeck(
    const struct ToriRSServerVessel* vessel,
    int root_fine_x,
    int root_fine_z,
    int* out_deck_fine_x,
    int* out_deck_fine_z)
{
    int cx;
    int cz;
    int lx;
    int lz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_deck_fine_x);
    assert(out_deck_fine_z);

    vessel_recenter_fine(vessel, &cx, &cz);
    vessel_rotate_inverse(
        vessel->angle, root_fine_x - vessel->fine_x, root_fine_z - vessel->fine_z, &lx, &lz);
    *out_deck_fine_x = lx + cx;
    *out_deck_fine_z = lz + cz;
}

void
ToriRSServer_VesselDeckTileToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_tile_x,
    int deck_tile_z,
    int* out_fine_x,
    int* out_fine_z)
{
    int base_x = 0;
    int base_z = 0;
    int ok;

    assert(vessel);
    assert(vessel->in_use);
    assert(vessel->instance > 0);
    assert(out_fine_x);
    assert(out_fine_z);

    ok = ToriRSServer_MapInstanceBase(vessel->instance, &base_x, &base_z);
    /* The vessel owns its reservation for its whole life; a dead handle here
     * means somebody freed the instance out from under it. */
    assert(ok);
    (void)ok;

    ToriRSServer_VesselDeckToRoot(
        vessel,
        (deck_tile_x - base_x) * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64,
        (deck_tile_z - base_z) * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64,
        out_fine_x,
        out_fine_z);
}

/* ------------------------------------------------------------------ */
/* Wire-facing lookups (docs/SAILING_PLAN.md S2)                       */
/* ------------------------------------------------------------------ */

struct ToriRSServerVessel*
ToriRSServer_VesselByView(
    struct ToriRSServer* srv,
    int view_id)
{
    assert(srv);

    /* View 0 is the root world and names no vessel; an id past the registry is
     * a value off the wire, not a caller bug. */
    if( view_id <= 0 || view_id > TORIRSSERVER_WEV_VIEW_MAX )
        return NULL;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use && srv->vessels[i].view_id == view_id )
            return &srv->vessels[i];
    return NULL;
}

struct ToriRSServerVessel*
ToriRSServer_VesselAtTile(
    struct ToriRSServer* srv,
    int tile_x,
    int tile_z)
{
    int instance;

    assert(srv);

    /* The pool already answers "which reservation is this tile in"; the vessel
     * is the one hull that owns that reservation. Cheaper than re-deriving
     * bounds here, and it agrees with the pool by construction. */
    instance = ToriRSServer_MapInstanceFind(tile_x, tile_z);
    if( instance == 0 )
        return NULL;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use && srv->vessels[i].instance == instance )
            return &srv->vessels[i];
    return NULL;
}

void
ToriRSServer_VesselDeckZones(
    const struct ToriRSServerVessel* vessel,
    int* out_zones_x,
    int* out_zones_z)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(out_zones_x);
    assert(out_zones_z);

    /* The same rounding VesselSpawn reserved the instance with — and the wire's
     * size nibbles are zone counts, so this is what goes on the wire too. */
    *out_zones_x = (vessel->size_x_tiles + 7) / 8;
    *out_zones_z = (vessel->size_z_tiles + 7) / 8;
}
