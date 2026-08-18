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
 * Six core things content can say (`ss_opcode.h` 11009..11014), and they are
 * the four steps plus two queries that both behaviour references agree on —
 * 2009scape `DynamicRegion` and Kronos `DynamicMap`:
 *
 *     alloc      reserve WxH zones somewhere unused          -> handle
 *     setchunk   one destination zone <- one source zone, turned
 *     build      commit: collision here, REBUILD_REGION there
 *     coord      instance-relative offset -> absolute coord
 *     free       give the reservation back
 *     find       which instance is this coord in?
 *
 * Later metadata commands expose the allocating player's uid and a generic
 * session-local flag bitset. They do not change scene geometry: ownership lets
 * guest content resolve a host, while flags let every occupant observe shared
 * modes such as a POH dungeon challenge without persisting them to one player.
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
 * Zones per axis the *scene* can show (REBUILD_REGION is a 4 x 13 x 13 grid).
 * Encode / collision window / client rebuild all walk this size.
 */
#define MOCK230_MAPINSTANCE_SCENE_ZONES 13

/**
 * Zones per axis one *reservation* may hold.
 *
 * Larger than the scene window so a sliding 13-zone view can walk a bigger
 * instance (The Gauntlet's 7×7 of 16-tile rooms is 14×14 zones). Storage and
 * `map_instance_alloc` use this; the wire still only ever describes
 * SCENE_ZONES around the player. Raised from 13 when Gauntlet needed a full
 * 7×7 of 16-tile rooms (docs/GAUNTLET.md).
 */
#define MOCK230_MAPINSTANCE_ZONES 16
#define MOCK230_MAPINSTANCE_LEVELS 4

/**
 * Small content-owned integer register file attached to each live instance.
 *
 * Dynamic-map activities need more than yes/no switches: a house game has a
 * phase, two player uids, turn, scores and a prize balance.  The engine owns
 * only the lifetime and bounds of these registers; RuneScript assigns every
 * slot's meaning.  The count is intentionally finite and visible so content
 * cannot turn an instance into an unbounded key/value heap.
 *
 * Raised from 64 to 128 for the Theatre of Blood, which is one activity with
 * six rooms in it. Sixty-four was enough while each room could have the bank to
 * itself; it stopped being enough once several rooms carried live state at
 * once, and the failure mode is the worst kind — `tob.constant`'s own header
 * warns "do not reuse a slot across rooms: one room's leftover value becomes
 * the next room's opening state", and two rooms allocated over each other
 * anyway because the space had run out. Nothing reports that; the next room
 * simply opens mid-fight.
 *
 * The cost is 8 instances x 64 ints = 2 KB.
 */
#define MOCK230_MAPINSTANCE_VARS 128

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
        zones[MOCK230_MAPINSTANCE_LEVELS][MOCK230_MAPINSTANCE_SCENE_ZONES]
             [MOCK230_MAPINSTANCE_SCENE_ZONES];
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
 * Handles are 1-based; 0 is the surface's only "no instance" value, and it is
 * what this returns when the arguments are out of range or the pool is
 * exhausted. Content is expected to check, because an instance is a resource
 * rather than a coordinate. `cache_dir` is where the free-square sweep reads its
 * reference table (once per process).
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
 * Associate a live player uid with the reservation. The allocator stays
 * content-neutral; its RuneScript host call records the player who requested
 * the instance so later cross-player POH interactions can resolve the host.
 */
int
mock230_mapinstance_set_owner(
    int handle,
    int player_uid);

/** The recorded player uid, or 0 for a dead/unowned reservation. */
int
mock230_mapinstance_owner(int handle);

/**
 * Find a live reservation owned by `player_uid` and carrying every bit in
 * `required_flags`. A zero mask accepts any reservation; 0 means no match.
 */
int
mock230_mapinstance_find_owner(
    int player_uid,
    int required_flags);

/**
 * The first live reservation carrying every flag in `required_flags`, whoever
 * owns it, or 0. The join side of `mock230_mapinstance_find_owner`: a player
 * entering somebody else's instance cannot name its owner. A zero or negative
 * mask matches nothing.
 */
int
mock230_mapinstance_find_flagged(int required_flags);

/**
 * Read or write a content-owned, session-local flag on a live instance.
 *
 * This is deliberately a generic bitset rather than POH state: challenge mode
 * belongs to the current house visit, is observed by every guest, and must not
 * leak into the owner's durable save. Other instanced content may assign its
 * own masks. A non-positive mask or dead handle reads false and rejects writes.
 */
int
mock230_mapinstance_flag_get(
    int handle,
    int mask);
int
mock230_mapinstance_flag_set(
    int handle,
    int mask,
    int enabled);

/**
 * Read or write one content-owned, session-local integer register.
 *
 * Registers start at zero on allocation and are erased on free/reset.  Invalid
 * handles or slots read as zero and reject writes.  Values are deliberately
 * unrestricted signed ints: player uids, scores and clocks all share the same
 * RuneScript integer representation.
 */
int
mock230_mapinstance_var_get(
    int handle,
    int slot);
int
mock230_mapinstance_var_set(
    int handle,
    int slot,
    int value);

/** Clear every live reservation owned by this uid; returns the number cleared. */
int
mock230_mapinstance_clear_owner(int player_uid);

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

/**
 * Which build this instance is on: a pool-wide counter bumped by every
 * `_build`, so no two builds ever share a value. 0 = dead handle or unbuilt.
 *
 * Exists because handle and coordinates are both reused: freeing an instance
 * and allocating another hands back the same handle at the same square, so
 * they cannot tell a client's scene apart from the one built on its grave.
 * `Mock230Player::scene_instance_generation` records what a client was last
 * shown and a mismatch forces a rebuild (docs/ORANGE_WEDGE.md §18).
 */
int
mock230_mapinstance_generation(int handle);

/** Absolute south-west tile of the instance. Returns 0 for a dead handle. */
int
mock230_mapinstance_base(
    int handle,
    int* out_x,
    int* out_z);

/** Absolute tile rectangle reserved for this instance, including the whole-map
 *  square padding the allocator owns. Returns 0 for a dead handle. Teardown
 *  uses the reserved rectangle rather than only the assembled zones so no
 *  runtime state can survive in padding that the next tenant may assemble. */
int
mock230_mapinstance_bounds(
    int handle,
    int* out_x,
    int* out_z,
    int* out_width,
    int* out_height);

/** Release the reservation. Returns 0 for a dead handle. Freeing an instance a
 *  player is standing in is content's bug; the engine does not cover it. */
int
mock230_mapinstance_free(int handle);

/** How many reservations are live. For leak checks — a session that ends with
 *  a non-zero count released nothing. */
int
mock230_mapinstance_live_count(void);

/** The handle whose reserved area contains this absolute tile, or 0. */
int
mock230_mapinstance_find(
    int x,
    int z);

/**
 * Which square of the real map an instanced tile was copied from.
 *
 * `*out_x` / `*out_z` come back as an absolute *tile* in the source zone's
 * south-west corner, so `>> 6` is the source map square and `>> 3` the source
 * zone. Returns 0 for a dead handle, a tile outside the reservation, or a
 * destination zone nothing was ever pointed at (an instance is allowed to have
 * holes, and a player standing in one came from nowhere).
 *
 * Exists because an instance's coordinates say nothing about where it *is* in
 * the world's terms. The pool hands out map squares from x >= 100, far past the
 * edge of the real map, so anything keyed on the player's map square — region
 * music is the case this was added for, but nothing about it is music-specific
 * — reads an address that no content, table or wiki has ever described. The
 * template square is the address that means something.
 *
 * `level` is the destination plane. The lookup falls back to plane 0 when the
 * player's own plane has no zone set there, because content routinely builds
 * one storey and leaves the rest void, and a player on the void plane is still
 * standing in the same place.
 */
int
mock230_mapinstance_source_tile(
    int handle,
    int level,
    int x,
    int z,
    int* out_x,
    int* out_z);

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
