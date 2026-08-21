#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_MAPINSTANCE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_MAPINSTANCE_H

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
 *   `ToriRSServer_MapInstanceRotateToSrc` and `_to_dst` are inverses and the
 *   comments on them say which is which. Getting one of them backwards is
 *   invisible at rotation 0 and 2 and mirrors the zone at 1 and 3.
 */

#include <stdint.h>

/** Concurrent instances. One per active POH / minigame session; the reference
 *  caps by pool size too (Kronos's FREE_REGIONS deque). */
#define TORIRSSERVER_MAPINSTANCE_MAX 8

/**
 * Zones per axis the *scene* can show (REBUILD_REGION is a 4 x 13 x 13 grid).
 * Encode / collision window / client rebuild all walk this size.
 */
#define TORIRSSERVER_MAPINSTANCE_SCENE_ZONES 13

/**
 * Zones per axis one *reservation* may hold.
 *
 * Larger than the scene window so a sliding 13-zone view can walk a bigger
 * instance (The Gauntlet's 7×7 of 16-tile rooms is 14×14 zones). Storage and
 * `map_instance_alloc` use this; the wire still only ever describes
 * SCENE_ZONES around the player. Raised from 13 when Gauntlet needed a full
 * 7×7 of 16-tile rooms (docs/GAUNTLET.md).
 */
#define TORIRSSERVER_MAPINSTANCE_ZONES 16
#define TORIRSSERVER_MAPINSTANCE_LEVELS 4

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
#define TORIRSSERVER_MAPINSTANCE_VARS 128

/** Zones per axis in a map square — the granularity the cache's archives, and
 *  therefore the allocator's reservations, come in. */
#define TORIRSSERVER_MAPINSTANCE_SQUARE_ZONES 8

/**
 * How long an abandoned instance survives, in ticks (100 x 600ms = 60s).
 *
 * Every reservation starts with this linger: once a player has stood inside it
 * and its whole linger group (below) has then been empty of players for this
 * many consecutive ticks, the world tick tears it down with the full
 * `ToriRSServer_WorldMapInstanceFree` teardown — npcs, floor objects, loc changes
 * and the reservation itself. Until then the instance is intact, which is what
 * lets a session that dropped mid-encounter log back in *inside* it and lets
 * content restart the fight instead of stranding the character on void.
 *
 * `ToriRSServer_MapInstanceSetLinger(handle, 0)` opts an instance out — content
 * then owns the lifetime completely, as everything did before linger existed.
 */
#define TORIRSSERVER_MAPINSTANCE_LINGER_DEFAULT 100

/** One destination zone's source. `set == 0` means void. */
struct ToriRSServerMapInstanceZone
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
 * Filled by `ToriRSServer_MapInstanceWindow` for a scene origin, and consumed by
 * both the collision build (`ToriRSServer_SceneBuildInstance`) and the encoder
 * (`ToriRSServer_SendRebuildRegion`) so the two cannot describe different maps.
 * Index order is [level][zone_x][zone_z] with zone_x/zone_z relative to the
 * scene's south-west zone, which is `origin_zone - 6`.
 */
struct ToriRSServerMapInstanceWindow
{
    struct ToriRSServerMapInstanceZone
        zones[TORIRSSERVER_MAPINSTANCE_LEVELS][TORIRSSERVER_MAPINSTANCE_SCENE_ZONES]
             [TORIRSSERVER_MAPINSTANCE_SCENE_ZONES];
    /** How many entries are set. 0 means "not an instanced scene". */
    int set_count;
};

/** Drop every reservation. Called from world init/reset — a handle is
 *  per-session state and must not outlive the world that issued it. */
void
ToriRSServer_MapInstanceReset(void);

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
ToriRSServer_MapInstanceAlloc(
    const char* cache_dir,
    int zone_w,
    int zone_h);

/**
 * Associate a live player uid with the reservation. The allocator stays
 * content-neutral; its RuneScript host call records the player who requested
 * the instance so later cross-player POH interactions can resolve the host.
 */
int
ToriRSServer_MapInstanceSetOwner(
    int handle,
    int player_uid);

/** The recorded player uid, or 0 for a dead/unowned reservation. */
int
ToriRSServer_MapInstanceOwner(int handle);

/**
 * Find a live reservation owned by `player_uid` and carrying every bit in
 * `required_flags`. A zero mask accepts any reservation; 0 means no match.
 */
int
ToriRSServer_MapInstanceFindOwner(
    int player_uid,
    int required_flags);

/**
 * The first live reservation carrying every flag in `required_flags`, whoever
 * owns it, or 0. The join side of `ToriRSServer_MapInstanceFindOwner`: a player
 * entering somebody else's instance cannot name its owner. A zero or negative
 * mask matches nothing.
 */
int
ToriRSServer_MapInstanceFindFlagged(int required_flags);

/**
 * Read or write a content-owned, session-local flag on a live instance.
 *
 * This is deliberately a generic bitset rather than POH state: challenge mode
 * belongs to the current house visit, is observed by every guest, and must not
 * leak into the owner's durable save. Other instanced content may assign its
 * own masks. A non-positive mask or dead handle reads false and rejects writes.
 */
int
ToriRSServer_MapInstanceFlagGet(
    int handle,
    int mask);
int
ToriRSServer_MapInstanceFlagSet(
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
ToriRSServer_MapInstanceVarGet(
    int handle,
    int slot);
int
ToriRSServer_MapInstanceVarSet(
    int handle,
    int slot,
    int value);

/** Clear every live reservation owned by this uid; returns the number cleared. */
int
ToriRSServer_MapInstanceClearOwner(int player_uid);

/**
 * How many ticks this instance survives once its linger group is empty.
 *
 * 0 disables the engine teardown entirely — content owns the lifetime, which
 * is what every instance had before linger existed. Reads return the setting
 * (0 for a dead handle); writes reject a dead handle or a negative count.
 */
int
ToriRSServer_MapInstanceSetLinger(
    int handle,
    int ticks);
int
ToriRSServer_MapInstanceLinger(int handle);

/**
 * Bind this instance into a linger group.
 *
 * Emptiness is judged per GROUP, not per reservation: a raid whose lobby and
 * fight room are two reservations has a lobby that is legitimately empty for
 * the whole fight (ToA's Nexus), and a satellite map nobody stands in between
 * visits (Sotetseg's shadow realm). Any nonzero content-chosen tag; instances
 * sharing a tag count each other's occupants and expire together. 0 (the
 * default) means the instance is its own group.
 */
int
ToriRSServer_MapInstanceSetLingerGroup(
    int handle,
    int group);
int
ToriRSServer_MapInstanceLingerGroup(int handle);

/**
 * One linger clock advance for one instance; called once per world tick.
 *
 * `group_occupied` is whether any player is standing anywhere in the
 * instance's linger group this tick (the caller computes it — occupancy is the
 * world's knowledge, not the registry's). Returns 1 when the instance has just
 * expired — occupied once, then empty past its linger — and the caller must
 * tear it down; 0 otherwise. An instance that was never entered never expires,
 * so a map built ahead of the teleport into it is safe at any build latency.
 */
int
ToriRSServer_MapInstanceLingerTick(
    int handle,
    int group_occupied);

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
ToriRSServer_MapInstanceSetchunk(
    int handle,
    int level,
    int zone_x,
    int zone_z,
    int src_x,
    int src_z,
    int src_level,
    int rotation);

/** Mark the instance assembled. Returns 0 for a dead handle. The scene rebuild
 *  is the caller's — see `ToriRSServer_WorldMapInstanceBuilt`. */
int
ToriRSServer_MapInstanceBuild(int handle);

/**
 * Which build this instance is on: a pool-wide counter bumped by every
 * `_build`, so no two builds ever share a value. 0 = dead handle or unbuilt.
 *
 * Exists because handle and coordinates are both reused: freeing an instance
 * and allocating another hands back the same handle at the same square, so
 * they cannot tell a client's scene apart from the one built on its grave.
 * `ToriRSServerPlayer::scene_instance_generation` records what a client was last
 * shown and a mismatch forces a rebuild (docs/ORANGE_WEDGE.md §18).
 */
int
ToriRSServer_MapInstanceGeneration(int handle);

/** Absolute south-west tile of the instance. Returns 0 for a dead handle. */
int
ToriRSServer_MapInstanceBase(
    int handle,
    int* out_x,
    int* out_z);

/** Absolute tile rectangle reserved for this instance, including the whole-map
 *  square padding the allocator owns. Returns 0 for a dead handle. Teardown
 *  uses the reserved rectangle rather than only the assembled zones so no
 *  runtime state can survive in padding that the next tenant may assemble. */
int
ToriRSServer_MapInstanceBounds(
    int handle,
    int* out_x,
    int* out_z,
    int* out_width,
    int* out_height);

/** Release the reservation. Returns 0 for a dead handle. Freeing an instance a
 *  player is standing in is content's bug; the engine does not cover it. */
int
ToriRSServer_MapInstanceFree(int handle);

/** How many reservations are live. For leak checks — a session that ends with
 *  a non-zero count released nothing. */
int
ToriRSServer_MapInstanceLiveCount(void);

/** The handle whose reserved area contains this absolute tile, or 0. */
int
ToriRSServer_MapInstanceFind(
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
ToriRSServer_MapInstanceSourceTile(
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
ToriRSServer_MapInstanceWindow(
    int zone_x,
    int zone_z,
    struct ToriRSServerMapInstanceWindow* out);

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
ToriRSServer_MapInstanceRotateToSrc(
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
ToriRSServer_MapInstanceRotateToDst(
    int rotation,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int* out_dx,
    int* out_dz);

#endif
