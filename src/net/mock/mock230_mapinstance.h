#ifndef SRC_NET_MOCK_MOCK230_MAPINSTANCE_H
#define SRC_NET_MOCK_MOCK230_MAPINSTANCE_H

/*
 * Map instances — a private copy of a piece of the map, assembled out of zones.
 *
 * The thing a POH, the Pest Control island, a Barrows tunnel and a cutscene set
 * all are: an area at coordinates nobody else is using, whose terrain and locs
 * are *copied* from somewhere in the cache, zone by zone, possibly turned. The
 * player is teleported into it and the client is told to build its scene out of
 * the copies rather than out of the square it is standing on.
 *
 * Six things content can say (`ss_opcode.h` 11009..11014), and they are the four
 * steps plus two queries that both behaviour references agree on — 2009scape
 * `DynamicRegion` and Kronos `DynamicMap`:
 *
 *     alloc      reserve WxH zones somewhere unused          -> handle
 *     setchunk   one destination zone <- one source zone, turned
 *     build      commit: collision here, REBUILD_REGION there
 *     coord      instance-relative offset -> absolute coord
 *     free       give the reservation back
 *     find       which instance is this coord in?
 *
 * **LostCity has none of this**, measured rather than assumed: `engine.rs2`
 * declares no map-allocation command (the nearest thing is a commented-out
 * `region_findbycoord` / `controller_*` block at engine.rs2:1051-1060 that
 * Engine-TS wires to nothing), `BuildArea.rebuildNormal` is the only scene it
 * can send, and there is no construction content in the tree at all. That is
 * why these are engine-only opcodes in the EXTRA band rather than ports.
 *
 * Three things worth knowing before changing anything here:
 *
 * - **The pool is measured, not chosen.** `mapinstance_scan_pool` sweeps the
 *   cache's own maps reference table and treats a square the cache does not
 *   ship as free. Nothing here hardcodes a coordinate band. The sweep starts at
 *   map square x = MAPINSTANCE_SCAN_X0 for the same reason Kronos's pool does
 *   (`DynamicMap.load`: `region.baseX >= 6400`) — far enough from the real map
 *   that an instance is not adjacent to anything — and that band is verified
 *   empty rather than trusted: this cache's 2,934 squares all sit in x 15..98.
 *
 * - **A zone nobody set is void, and stays void.** That is what makes an empty
 *   house floor empty instead of a copy of whatever the previous tenant of that
 *   pool slot put there, and it is also what the wire means: a REBUILD_REGION
 *   descriptor bit of 0 is "no source".
 *
 * - **Rotation is a coordinate transform in two directions and they are not the
 *   same function.** The terrain copy walks *destination* tiles and asks where
 *   each came from; the loc copy walks *source* locs and asks where each goes.
 *   `mock230_mapinstance_rotate_to_src` and `_to_dst` are inverses and the
 *   comments on them say which is which. Getting one of them backwards is
 *   invisible at rotation 0 and 2 and mirrors the zone at 1 and 3.
 */

#include <stdint.h>

/** Concurrent instances. One per active POH / minigame session; the reference
 *  caps by pool size too (Kronos's FREE_REGIONS deque). */
#define MOCK230_MAPINSTANCE_MAX 8

/**
 * Zones per axis in one instance.
 *
 * 13, because that is the scene the wire can describe (REBUILD_REGION carries a
 * 4 x 13 x 13 descriptor grid) and therefore the largest instance a client can
 * be shown at once. Both known consumers want less: 2009scape's POH and its
 * Pest Control island are `reserveArea(8, 8)` and `create(10536)`, one region
 * each, which is 8.
 */
#define MOCK230_MAPINSTANCE_ZONES 13
#define MOCK230_MAPINSTANCE_LEVELS 4

/** Zones per axis in a map square — the granularity the cache's archives, and
 *  therefore the allocator's reservations, come in. */
#define MOCK230_MAPINSTANCE_SQUARE_ZONES 8

/** One destination zone's source. `set == 0` means void. */
struct Mock230MapInstanceZone
{
    int set;
    /** Source zone, in absolute zone units (tile >> 3), and its plane. */
    int src_zone_x;
    int src_zone_z;
    int src_level;
    /** Quarter-turns clockwise applied to the copied zone, 0..3. */
    int rotation;
};

/**
 * The 4 x 13 x 13 window a scene needs, indexed by destination.
 *
 * Filled by `mock230_mapinstance_window` for a scene origin, and consumed by
 * both the collision build (`mock230_scene_build_instance`) and the encoder
 * (`mock230_send_rebuild_region`) so the two cannot describe different maps.
 * Index order is [level][zone_x][zone_z] with zone_x/zone_z relative to the
 * scene's south-west zone, which is `origin_zone - 6`.
 */
struct Mock230MapInstanceWindow
{
    struct Mock230MapInstanceZone
        zones[MOCK230_MAPINSTANCE_LEVELS][MOCK230_MAPINSTANCE_ZONES][MOCK230_MAPINSTANCE_ZONES];
    /** How many entries are set. 0 means "not an instanced scene". */
    int set_count;
};

/** Drop every reservation. Called from world init/reset — a handle is
 *  per-session state and must not outlive the world that issued it. */
void
mock230_mapinstance_reset(void);

/**
 * Reserve `zone_w` x `zone_h` zones of unused map and return a handle.
 *
 * Returns -1 when the arguments are out of range or the pool is exhausted;
 * content is expected to check, because an instance is a resource rather than a
 * coordinate. `cache_dir` is where the free-square sweep reads its reference
 * table (once per process).
 *
 * The reservation is rounded up to whole map squares, so two instances never
 * share a square and a square is never half free.
 */
int
mock230_mapinstance_alloc(
    const char* cache_dir,
    int zone_w,
    int zone_h);

/**
 * Point one zone of the instance at one zone of the cache.
 *
 * `level`/`zone_x`/`zone_z` are instance-relative and 0-based; `src_x`/`src_z`
 * are absolute *tiles* anywhere inside the source zone (they are floored to the
 * zone) and `src_level` is the source plane. `rotation` is quarter-turns
 * clockwise, 0..3.
 *
 * Returns 0 for a dead handle or an out-of-range destination.
 */
int
mock230_mapinstance_setchunk(
    int handle,
    int level,
    int zone_x,
    int zone_z,
    int src_x,
    int src_z,
    int src_level,
    int rotation);

/** Mark the instance assembled. Returns 0 for a dead handle. The scene rebuild
 *  is the caller's — see `mock230_world_mapinstance_built`. */
int
mock230_mapinstance_build(int handle);

/** Absolute south-west tile of the instance. Returns 0 for a dead handle. */
int
mock230_mapinstance_base(
    int handle,
    int* out_x,
    int* out_z);

/** Release the reservation. Returns 0 for a dead handle. Freeing an instance a
 *  player is standing in is content's bug; the engine does not cover it. */
int
mock230_mapinstance_free(int handle);

/** The handle whose reserved area contains this absolute tile, or -1. */
int
mock230_mapinstance_find(
    int x,
    int z);

/**
 * Fill the descriptor window for a scene whose origin zone is (zone_x, zone_z).
 *
 * Returns the number of set zones; 0 means no instance overlaps the scene and
 * the caller should build from the cache the ordinary way.
 */
int
mock230_mapinstance_window(
    int zone_x,
    int zone_z,
    struct Mock230MapInstanceWindow* out);

/**
 * Where in the source zone a destination tile comes from.
 *
 * Walks *backwards* — this is the terrain copy's direction, which iterates the
 * destination and reads the source. For rotation r, destination local (dx, dz)
 * in 0..7 reads source local:
 *
 *     r=0  (dx, dz)        r=1  (dz, 7-dx)
 *     r=2  (7-dx, 7-dz)    r=3  (7-dz, dx)
 */
void
mock230_mapinstance_rotate_to_src(
    int rotation,
    int dx,
    int dz,
    int* out_sx,
    int* out_sz);

/**
 * Where a source tile ends up in the destination zone — the inverse of the
 * above, and the loc copy's direction (it iterates the source's locs).
 *
 * A loc is not a point: it occupies `size_x` x `size_z` tiles from its
 * south-west corner, and a quarter-turn moves that corner to a different one of
 * the rectangle's four. `size_x`/`size_z` are the footprint as placed (already
 * swapped for the loc's own odd angle); pass 1,1 for a point.
 */
void
mock230_mapinstance_rotate_to_dst(
    int rotation,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int* out_dx,
    int* out_dz);

#endif
