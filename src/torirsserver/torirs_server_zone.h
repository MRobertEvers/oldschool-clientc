#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_ZONE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_ZONE_H

/*
 * The world, cut into 8x8 tile zones — the structure every world event is
 * addressed through, and the reason one can be *replayed* to a client that was
 * not there when it happened.
 *
 * Ported from the reference's `engine/src/engine/zone/` (Zone, ZoneMap,
 * ZoneEvent). The keying is identical — `(x >> 3) | (z >> 3) << 11 |
 * level << 22` — because the wire is: every UPDATE_ZONE_* packet names an 8x8
 * zone and every sub-packet that follows carries only a `pos` inside it.
 *
 * ── What this replaces, and why it had to ────────────────────────────
 *
 * Before this, a loc change was a *broadcast*: `ToriRSServer_WorldBroadcastLoc`
 * walked the player pool and sent the packet to everyone on that level whose
 * scene contained the tile. That is correct for everyone who is logged in when
 * the door opens and silently wrong for everyone else — a broadcast has no
 * memory, so the second client to connect got a scene straight out of the cache
 * with every door shut, while the server believed they were open. Ground objs
 * had the mirror-image problem solved a different way: a per-player
 * `ground_sent[]` bitmap, rescanned flat over all 256 slots for every player
 * every tick.
 *
 * A zone holds both the *state* (which locs here differ from the map square,
 * which objs are on the floor, which npcs stand here) and this tick's *events*.
 * A client that has never been told about a zone is sent the state; a client
 * that already holds it is sent the events. That is the whole mechanism, and it
 * is what `docs/osrs230_mockserver.md` §6.1 step 3 asked for.
 *
 * ── The ZoneMap owns loc mutations; the scene does not ───────────────
 *
 * `torirs_server_scene.c` rebuilds itself from the cache whenever the scene origin
 * moves (`ToriRSServer_SceneBuild` calls `ToriRSServer_SceneFree` first), so it cannot
 * be the authority on anything that happened at runtime — a door opened before
 * a rebuild was, in fact, forgotten by the server as well as by the client,
 * despite a comment claiming the changed list survived. The durable record is
 * `struct ToriRSServerZoneLoc` here, and `ToriRSServer_WorldLocsReapply` puts every one
 * of them back onto the freshly built scene so collision and the pick lookup
 * agree with what the clients were told.
 *
 * A loc record is keyed by `(x, z, level, shape)` — the same key the wire uses,
 * since LOC_ADD_CHANGE and LOC_DEL identify a loc by its tile and its shape and
 * nothing else. It is dropped again the moment it matches the map square's own
 * version, so the record set is exactly the diff and a loc cycled on a timer
 * does not leak one per cycle.
 *
 * ── Enclosed vs follows ──────────────────────────────────────────────
 *
 * The reference splits zone events two ways: ENCLOSED events are encoded once
 * into a shared byte buffer and handed to every client in the zone
 * (UPDATE_ZONE_PARTIAL_ENCLOSED), FOLLOWS events are written per client
 * (UPDATE_ZONE_PARTIAL_FOLLOWS + the sub-packet). What decides which is who the
 * event is *for*, so that is the field here: `receiver_pid < 0` means everybody
 * and goes in the shared buffer; anything else is one client's and is written
 * on its own. The receiver-scoped half has no producer yet — this server has no
 * per-player loot ownership — but it is the field the reference's `Obj.receiver64`
 * is, and leaving it out would mean the shared buffer could never learn to skip
 * anyone.
 */

#include <stdint.h>

/* For struct ToriRSServerLocOps: a zone loc record and the LOC_ADD_CHANGE event it
 * queues both carry the placement's own menu, and the scene holds the same
 * type. One definition, in the file that owns the loc. */
#include "torirs_server_scene.h"

struct ToriRSServer;
struct ToriRSServerPlayer;
struct ToriRSServerPlayerZone;
struct ToriRSServerNpc;

/** What a zone's membership lists hold. One enum rather than the `is_npc`
 *  boolean `refile` took, because there are three kinds now and a boolean that
 *  has to mean "obj" when false is a boolean waiting to be misread. */
enum ToriRSServerZoneKind
{
    TORIRSSERVER_ZONE_KIND_NPC = 0,
    TORIRSSERVER_ZONE_KIND_OBJ,
    TORIRSSERVER_ZONE_KIND_PLAYER,
};

enum
{
    /** Tiles on a side. Not negotiable: it is the wire's `pos` field. */
    TORIRSSERVER_ZONE_TILES = 8,

    /**
     * The window of zones a client is kept up to date on, as a radius in zones
     * around the one it is standing in — 7x7, from the reference's
     * `BuildArea.rebuildZones`. Clipped to the build area, which is 13x13 zones
     * (104 tiles) centred on the scene origin, so the most a player can hold is
     * 7*7.
     */
    TORIRSSERVER_ZONE_VIEW_RADIUS = 3,
    TORIRSSERVER_ZONE_BUILD_RADIUS = 6,
    /**
     * The four map planes. A client's scene holds all of them at once — that is
     * how a building has an upstairs — so the zone window is per (x, z, level)
     * and not per (x, z) at the player's own level.
     */
    TORIRSSERVER_ZONE_LEVELS = 4,
    TORIRSSERVER_ZONE_ACTIVE_MAX = (TORIRSSERVER_ZONE_VIEW_RADIUS * 2 + 1) *
                              (TORIRSSERVER_ZONE_VIEW_RADIUS * 2 + 1) *
                              TORIRSSERVER_ZONE_LEVELS,
};

/** The wire packets a zone event turns into. See torirs_server_encode.c. */
enum ToriRSServerZoneEventKind
{
    TORIRSSERVER_ZONE_EV_LOC_ADD_CHANGE = 0,
    TORIRSSERVER_ZONE_EV_LOC_DEL,
    TORIRSSERVER_ZONE_EV_OBJ_ADD,
    TORIRSSERVER_ZONE_EV_OBJ_DEL,
    TORIRSSERVER_ZONE_EV_OBJ_COUNT,
    TORIRSSERVER_ZONE_EV_LOC_ANIM,
    TORIRSSERVER_ZONE_EV_LOC_MERGE,
    /**
     * A projectile in flight — `projanim_pl` / `projanim_npc`.
     *
     * A zone event rather than a per-player send, even though it is aimed at one
     * entity, because everybody standing near the shot has to see it: the
     * reference's `World.mapProjAnim` writes it to the zone the arrow *leaves*,
     * not to the zone the target is in, which is why a fight across a zone
     * boundary looks right to a bystander on either side.
     */
    TORIRSSERVER_ZONE_EV_PROJANIM,
    /**
     * Free-standing spotanim at a tile — `spotanim_map`. Wire MAP_ANIM (124).
     * Height/delay ride in `src_height` / `start_delay` (same widths as the
     * packet); nothing else on this event is meaningful.
     */
    TORIRSSERVER_ZONE_EV_MAPANIM,
};

/**
 * One thing that happened in a zone this tick.
 *
 * `pos` is the tile's offset inside the zone as the wire spells it,
 * `(x & 7) << 4 | (z & 7)`; the zone itself is named by the UPDATE_ZONE_*
 * header that precedes the sub-packet, which is why the two are only ever
 * produced together.
 */
struct ToriRSServerZoneEvent
{
    int kind;
    /** Pid this event is for, or -1 for everyone (the shared buffer). */
    int receiver_pid;
    int pos;
    /** loc id, obj id, or (for LOC_ANIM) seq id. */
    int id;
    int shape, angle;
    int count, old_count;
    /** LOC_ADD_CHANGE only: the placement's own menu, written as
     *  LOC_ADD_CHANGE_V2's opCount/ops/opFlags. Neutral for every other kind. */
    struct ToriRSServerLocOps ops;
    /** LOC_MERGE: start/end cycles, player slot, AABB offsets from the loc tile. */
    int start_cycle, end_cycle;
    int player_pid;
    int east, south, west, north;

    /*
     * PROJANIM. The spotanim rides in `id` with every other config id this
     * struct carries; the rest is the shot's own geometry and needs its own
     * names — `start_cycle`/`end_cycle` above are *cycles* the client counts a
     * merged loc for, and the two delays here are the tick the arrow leaves and
     * the tick it arrives, which is a different quantity that happens to be two
     * ints wide.
     */
    /** Destination tile relative to the source, which is where `pos` points. */
    int dx_offset, dz_offset;
    /**
     * The destination as an ABSOLUTE tile, carried alongside the offsets rather
     * than instead of them because the two revisions want different ones.
     *
     * The classic packet spells the destination as one signed byte per axis
     * from the source tile. Revision 239's MAP_PROJANIM_V2 writes a whole
     * CoordGrid -- `p4Alt1 end.packed`, level/x/z bitpacked absolutely -- and
     * feeding it the offsets lands the arc a few tiles from the world origin
     * while the packet frames perfectly and the client draws a projectile
     * nobody can see.
     *
     * Kept on the event rather than reconstructed in the encoder because the
     * encoder is handed a zone-local `pos` and never learns which zone it is.
     */
    int dst_x, dst_z, dst_level;
    /** Whom it homes on, in the wire's encoding: `slot + 1` for an npc,
     *  `-slot - 1` for a player, 0 for a shot that just lands on a tile. */
    int target;
    int src_height, dst_height;
    int start_delay, end_delay;
    /** Client-TS `angle` and `startpos` — how high the arc goes and how far
     *  along it the projectile is drawn at spawn. */
    int peak, arc;

    /**
     * OBJ_ADD: ticks until this obj vanishes, or 0 for one that never does.
     *
     * A field with no classic equivalent -- the older packet says only that an
     * obj is there, and the client keeps drawing it until told otherwise, so
     * the server has to send OBJ_DEL when a drop expires. Revision 239's
     * ObjAdd carries the lifetime up front. Sending 0 ("never despawns") for a
     * timed drop is not a protocol error and never desyncs anything; the client
     * simply draws the item until the server's own OBJ_DEL catches up, which is
     * a visible difference on a busy floor and nowhere else.
     */
    int despawn_ticks;
};

/**
 * A loc that is not what the map square says it is.
 *
 * `loc_id < 0` means the tile's loc of this shape has been removed. `base_*` is
 * what the cache has here and is captured when the record is created, so the
 * record can be retired when a revert restores it; a `base_loc_id` of -1 means
 * the square has nothing here and the record describes a `loc_add`.
 */
struct ToriRSServerZoneLoc
{
    int x, z, level;
    int shape;
    int loc_id, angle;
    int base_loc_id, base_angle;
    /** 1 when a `loc_add` put this loc here *over* the map square's own loc,
     *  which is still standing underneath. A rebuild has to reproduce that
     *  rather than replace the square's loc, or removing this one again leaves
     *  the tile empty — see ToriRSServer_SceneAddLoc. */
    int over_base;
    /** The placement's own menu, replayed to every client that arrives after
     *  the change. Without it here a door opened before you logged in would be
     *  drawn open and still offer "Open", because the zone state is the only
     *  memory a late client is built from. */
    struct ToriRSServerLocOps ops;
};

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

/** Packed zone key, byte-identical to the reference's `ZoneMap.zoneIndex`. */
int
ToriRSServer_ZoneIndex(
    int x,
    int z,
    int level);

/** Drop every zone. `ToriRSServer_WorldReset` and shutdown; not a per-tick call. */
void
ToriRSServer_ZoneFree(struct ToriRSServer* srv);

/* ------------------------------------------------------------------ */
/* Entity membership                                                   */
/* ------------------------------------------------------------------ */

/*
 * Membership is *reconciled*, not hooked.
 *
 * The reference calls `zone.enter()` / `zone.leave()` at each move site. Here an
 * npc's tile is written from five places (spawn, step, teleport, the respawn in
 * torirs_server_combat.c, the selftest) and a ground obj's activity from four, so the
 * npc and the obj each carry the zone they were last filed under and one pass
 * per tick fixes up whatever moved. A hook that is forgotten leaves an npc
 * filed in a zone it left; a reconcile cannot be forgotten.
 *
 * Both run in phase 8, before anything is encoded in phase 10.
 */
void
ToriRSServer_ZoneSyncNpcs(struct ToriRSServer* srv);
void
ToriRSServer_ZoneSyncObjs(struct ToriRSServer* srv);

/** Reconcile which zone every logged-in player is filed under. Phase 8, beside
 *  the npcs and objs, and for the same reason: membership is what makes
 *  "who does this client hold" answerable without a pool scan. */
void
ToriRSServer_ZoneSyncPlayers(struct ToriRSServer* srv);

void
ToriRSServer_ZonePlayerRefile(
    struct ToriRSServer* srv,
    int pid);

/**
 * Reconcile one slot immediately rather than waiting for the tick's pass.
 *
 * The one case the tick's pass cannot cover: a slot that is freed and handed
 * out again *inside* one tick. The reconcile only ever sees the end state, so it
 * would file the new occupant while the old zone still held the slot number. The
 * pool allocators call this before they recycle a slot, with the entity still
 * marked inactive, which unfiles it.
 */
void
ToriRSServer_ZoneNpcRefile(
    struct ToriRSServer* srv,
    int slot);

void
ToriRSServer_ZoneObjRefile(
    struct ToriRSServer* srv,
    int slot);

/**
 * Move `player`'s zone subscription to wherever the player now stands.
 *
 * Phase 8, after the map reconciles membership. A set difference, not a
 * rebuild: a player crossing a zone boundary leaves 7 columns of zones and
 * enters 7, and the other 42 are not touched. A player who has not changed
 * zone costs one compare.
 *
 * Subscribing to a zone takes its entities into `player->area`; unsubscribing
 * drops them. Everything after that is pushed by the map — see `refile`.
 */
void
ToriRSServer_PlayerzonemapMove(struct ToriRSServerPlayer* player);

/**
 * Unsubscribe from every zone and empty the area.
 *
 * Both ends, which is the point: each zone in the window holds this player's
 * pid, so an area cleared from one side only would leave the map pushing into
 * a client that no longer claims the zone.
 */
void
ToriRSServer_PlayerzonemapClear(struct ToriRSServerPlayer* player);

/**
 * The per-axis tile gap between the player's tile and an npc's FOOTPRINT: 0 on
 * an axis the player overlaps, otherwise the distance to the nearer edge.
 *
 * The same measure as `npc_player_distance` in torirs_server_world.c (the reference's
 * `CoordGrid.distanceTo`), split per axis because the wire's view test is a box
 * rather than a Chebyshev radius. It is separate rather than shared because
 * that one is the AI's, static to its own translation unit, and answers with a
 * single number.
 *
 * Never measure view range off `npc->x/z` alone: that is the npc's SOUTH-WEST
 * ORIGIN, so for anything bigger than 1x1 it is an arbitrary corner of the
 * body — a 7x7 boss leaves view seven tiles early on his east and north sides
 * and seven late on the other two.
 */
void
ToriRSServer_NpcViewDeltas(
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player,
    int* out_dx,
    int* out_dz);

/**
 * Who is standing in this client's subscribed zones, right now.
 *
 * Walked at the moment a packet needs it rather than kept up to date, which is
 * what makes it impossible for the answer to be stale — it is derived from the
 * authoritative world map every time it is asked. Plane and view radius are
 * applied inside, PER ENTITY, so `out` holds only what the caller could
 * actually send: a zone straddling the edge of the radius otherwise contributes
 * everything in it, up to 7 tiles past the range, and a bounded `out` then
 * loses entities that are genuinely beside the player.
 *
 * `_npcs` measures `radius` to the npc's footprint (`ToriRSServer_NpcViewDeltas`),
 * so a sized npc is a candidate while any tile of it is in range; `_players`
 * measures the tile, players being 1x1. Both stay exact per entity — widening
 * the radius instead would let a far fringe crowd a bounded `out`.
 */
int
ToriRSServer_PlayerzonemapNpcs(
    struct ToriRSServerPlayer* player,
    int radius,
    int* out,
    int max);

int
ToriRSServer_PlayerzonemapPlayers(
    struct ToriRSServerPlayer* player,
    int radius,
    int* out,
    int max);

/**
 * The npcs in view that a zonemap walk structurally CANNOT return, because the
 * observer and the npc are filed in different coordinate frames
 * (docs/sailing_coverage.csv SAIL-50).
 *
 * A vessel deck is a map instance: its tiles live in the pool, hundreds of
 * squares off the real map, and they are in nobody's subscription but the
 * riders'. So `_PlayerzonemapNpcs` cannot return a deckhand to a shore player
 * no matter how close the boat is — a range test is not the missing piece,
 * the candidate set is.
 *
 * This is that second candidate set: the npcs on the decks of hulls the
 * observer is NOT standing on. It is strictly ADDITIVE and disjoint from the
 * zonemap walk by construction — an npc is filed in exactly one zone, and the
 * observer's own hull is skipped precisely because its deck IS their zonemap —
 * so a caller appends the result and changes nothing else.
 *
 * The mirror direction (a rider seeing the shore) is deliberately NOT here;
 * the implementation carries why, and it is a wire-encoding limit rather than
 * a candidate-set one.
 *
 * `radius` and the footprint measure are the caller's, matching
 * `_PlayerzonemapNpcs` exactly — keeping and adding must agree or an npc is
 * added and removed on alternate ticks.
 *
 * Returns 0 immediately in a world with no live vessel, which is what keeps
 * the ordinary case free.
 */
int
ToriRSServer_PlayerCrossFrameNpcs(
    struct ToriRSServerPlayer* player,
    int radius,
    int* out,
    int max);

/** This client's entry for `index`, or NULL when it does not hold that zone. */
struct ToriRSServerPlayerZone*
ToriRSServer_PlayerzonemapFind(
    struct ToriRSServerPlayer* player,
    int index);

/* ------------------------------------------------------------------ */
/* Loc records                                                         */
/* ------------------------------------------------------------------ */

/**
 * Record that (x, z, level, shape) now holds `loc_id` at `angle`, and queue the
 * wire event for it. `loc_id < 0` is a removal.
 *
 * `base_loc_id` / `base_angle` describe what the map square has here and are
 * only read when the record is created (-1 for a loc the square does not have).
 * When the new state matches the base the record is retired, because "the same
 * as the cache" is not a difference worth replaying.
 *
 * `ops` is the placement's own right-click menu. Required, not optional: every
 * placement has a menu, and the one a caller with nothing to override passes is
 * `ToriRSServer_LocOpsDefault`, not NULL.
 */
void
ToriRSServer_ZoneLocChanged(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    int base_loc_id,
    int base_angle,
    int over_base,
    const struct ToriRSServerLocOps* ops);

/**
 * Queue LOC_ANIM for the loc at (x,z,level) with the given shape/angle and seq.
 * Does not mutate zone loc records — animation is ephemeral.
 */
void
ToriRSServer_ZoneLocAnim(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int seq_id);

/**
 * Queue LOC_MERGE (P_LOCMERGE): temporarily hide the loc and attach its model to
 * `player_pid` for [start_cycle, end_cycle) client ticks. AABB offsets are
 * relative to the loc tile (east/south/west/north as LostCity mergeLoc args).
 */
void
ToriRSServer_ZoneLocMerge(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int start_cycle,
    int end_cycle,
    int player_pid,
    int east,
    int south,
    int west,
    int north);

/**
 * Queue MAP_PROJANIM: a projectile leaving (x,z,level) for (dst_x,dst_z).
 *
 * Filed in the *source* tile's zone, so it reaches whoever can see the shooter.
 * The reference does the same, and it is what makes the arrow visible to a
 * bystander when the target is in the next zone over.
 *
 * `target` is the wire encoding — `slot + 1` for an npc, `-slot - 1` for a
 * player, 0 for a shot at nothing — and it is what the client re-aims the arc at
 * every cycle, so a moving target is followed rather than led.
 */
void
ToriRSServer_ZoneProjanim(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int dst_x,
    int dst_z,
    int target,
    int spotanim,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc);

/**
 * Queue MAP_ANIM: free-standing spotanim at (x,z,level). Filed in that tile's
 * zone so everyone who can see the tile gets it (LostCity `World.animMap`).
 */
void
ToriRSServer_ZoneMapanim(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int spotanim,
    int height,
    int delay);

/** The loc record at this key, or NULL. */
struct ToriRSServerZoneLoc*
ToriRSServer_ZoneLocFind(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape);

/** The record on this tile currently holding `loc_id`, or NULL. This is
 *  `loc_find`'s reach beyond the scene window: a runtime-added loc is
 *  addressable anywhere in the world, the way the reference's
 *  `World.getLoc` is. Static map locs outside the scene are not visible
 *  here — the ZoneMap is the diff, not the map. */
struct ToriRSServerZoneLoc*
ToriRSServer_ZoneLocFindId(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int loc_id);

/** Walk every recorded loc change in the world. Used by the rebuild, which has
 *  to put all of them back onto a scene that was just re-read from the cache. */
void
ToriRSServer_ZoneLocsForeach(
    struct ToriRSServer* srv,
    void (*fn)(struct ToriRSServerZoneLoc* loc, void* user),
    void* user);

/** Forget durable loc mutations and unsent loc events inside an absolute tile
 *  rectangle. Map-instance teardown calls this before returning a pooled
 *  destination to the allocator; otherwise the next tenant's rebuild reapplies
 *  the previous tenant's doors, walls, and collapsed scenery. */
void
ToriRSServer_ZoneLocsClearRect(
    struct ToriRSServer* srv,
    int x,
    int z,
    int width,
    int height);

/* ------------------------------------------------------------------ */
/* Obj events                                                          */
/* ------------------------------------------------------------------ */

/*
 * Ground objs keep living in `ToriRSServer.ground[]` — the pool is the
 * allocator and its slots are stable — and the zone holds the slot numbers. So
 * these three take a slot and read the rest off it.
 */
void
ToriRSServer_ZoneObjAdded(
    struct ToriRSServer* srv,
    int slot);
void
ToriRSServer_ZoneObjRemoved(
    struct ToriRSServer* srv,
    int slot);
void
ToriRSServer_ZoneObjCounted(
    struct ToriRSServer* srv,
    int slot,
    int old_count,
    int new_count);

/* ------------------------------------------------------------------ */
/* The per-tick flush                                                  */
/* ------------------------------------------------------------------ */

/**
 * Bring one client's zones up to date: phase 10, right after NPC_INFO, which is
 * where the reference puts `updateZones()`.
 *
 * Zones the client does not hold yet are sent UPDATE_ZONE_FULL_FOLLOWS (which
 * resets its memory of them) followed by the zone's current state. Zones it does
 * hold are sent this tick's events. The first case is the replay, and it is what
 * makes a door opened an hour ago open for someone logging in now.
 */
void
ToriRSServer_ZoneUpdatePlayer(struct ToriRSServerPlayer* player);

/** Forget what this client holds, so the next update re-sends every zone in
 *  full. Login, and every REBUILD_NORMAL — the rebuild resets the client's
 *  scene to the cache's version, which un-opens every door it had been told
 *  about. */
void
ToriRSServer_ZonePlayerReset(struct ToriRSServerPlayer* player);

/** Phase 11: drop this tick's events and shared buffers. State stays. */
void
ToriRSServer_ZoneReset(struct ToriRSServer* srv);

/**
 * Count current-tick events of `kind`; `id < 0` matches every config id.
 *
 * This is an observation seam for deterministic encounter tests. It reads the
 * same queued records the wire flush consumes and never mutates zone state.
 */
int
ToriRSServer_ZoneEventCount(
    struct ToriRSServer* srv,
    int kind,
    int id);

/** Last current-tick event id of `kind`, or -1 when none exists. */
int
ToriRSServer_ZoneEventLastId(
    struct ToriRSServer* srv,
    int kind);

/** Copy the last current-tick event matching `kind` and optional `id` into
 *  `out`. Returns 1 when found, 0 otherwise. The queued event remains owned by
 *  the zone map and is not consumed. */
int
ToriRSServer_ZoneEventLast(
    struct ToriRSServer* srv,
    int kind,
    int id,
    struct ToriRSServerZoneEvent* out);

/** Growth gauges for the perf harness: how many zones the map holds and the
 *  hash table capacity (never shrinks during a session). */
void
ToriRSServer_ZoneMapStats(
    struct ToriRSServer const* srv,
    int* out_count,
    int* out_capacity);

#endif
