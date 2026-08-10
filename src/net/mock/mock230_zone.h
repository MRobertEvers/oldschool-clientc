#ifndef SRC_NET_MOCK_MOCK230_ZONE_H
#define SRC_NET_MOCK_MOCK230_ZONE_H

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
 * Before this, a loc change was a *broadcast*: `mock230_world_broadcast_loc`
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
 * `mock230_scene.c` rebuilds itself from the cache whenever the scene origin
 * moves (`mock230_scene_build` calls `mock230_scene_free` first), so it cannot
 * be the authority on anything that happened at runtime — a door opened before
 * a rebuild was, in fact, forgotten by the server as well as by the client,
 * despite a comment claiming the changed list survived. The durable record is
 * `struct Mock230ZoneLoc` here, and `mock230_world_locs_reapply` puts every one
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

struct Mock230Server;
struct Mock230Player;

/** What a zone's membership lists hold. One enum rather than the `is_npc`
 *  boolean `refile` took, because there are three kinds now and a boolean that
 *  has to mean "obj" when false is a boolean waiting to be misread. */
enum Mock230ZoneKind
{
    MOCK230_ZONE_KIND_NPC = 0,
    MOCK230_ZONE_KIND_OBJ,
    MOCK230_ZONE_KIND_PLAYER,
};

enum
{
    /** Tiles on a side. Not negotiable: it is the wire's `pos` field. */
    MOCK230_ZONE_TILES = 8,

    /**
     * The window of zones a client is kept up to date on, as a radius in zones
     * around the one it is standing in — 7x7, from the reference's
     * `BuildArea.rebuildZones`. Clipped to the build area, which is 13x13 zones
     * (104 tiles) centred on the scene origin, so the most a player can hold is
     * 7*7.
     */
    MOCK230_ZONE_VIEW_RADIUS = 3,
    MOCK230_ZONE_BUILD_RADIUS = 6,
    /**
     * The four map planes. A client's scene holds all of them at once — that is
     * how a building has an upstairs — so the zone window is per (x, z, level)
     * and not per (x, z) at the player's own level.
     */
    MOCK230_ZONE_LEVELS = 4,
    MOCK230_ZONE_ACTIVE_MAX = (MOCK230_ZONE_VIEW_RADIUS * 2 + 1) *
                              (MOCK230_ZONE_VIEW_RADIUS * 2 + 1) *
                              MOCK230_ZONE_LEVELS,
};

/** The wire packets a zone event turns into. See mock230_encode.c. */
enum Mock230ZoneEventKind
{
    MOCK230_ZONE_EV_LOC_ADD_CHANGE = 0,
    MOCK230_ZONE_EV_LOC_DEL,
    MOCK230_ZONE_EV_OBJ_ADD,
    MOCK230_ZONE_EV_OBJ_DEL,
    MOCK230_ZONE_EV_OBJ_COUNT,
    MOCK230_ZONE_EV_LOC_ANIM,
    MOCK230_ZONE_EV_LOC_MERGE,
    /**
     * A projectile in flight — `projanim_pl` / `projanim_npc`.
     *
     * A zone event rather than a per-player send, even though it is aimed at one
     * entity, because everybody standing near the shot has to see it: the
     * reference's `World.mapProjAnim` writes it to the zone the arrow *leaves*,
     * not to the zone the target is in, which is why a fight across a zone
     * boundary looks right to a bystander on either side.
     */
    MOCK230_ZONE_EV_PROJANIM,
    /**
     * Free-standing spotanim at a tile — `spotanim_map`. Wire MAP_ANIM (124).
     * Height/delay ride in `src_height` / `start_delay` (same widths as the
     * packet); nothing else on this event is meaningful.
     */
    MOCK230_ZONE_EV_MAPANIM,
};

/**
 * One thing that happened in a zone this tick.
 *
 * `pos` is the tile's offset inside the zone as the wire spells it,
 * `(x & 7) << 4 | (z & 7)`; the zone itself is named by the UPDATE_ZONE_*
 * header that precedes the sub-packet, which is why the two are only ever
 * produced together.
 */
struct Mock230ZoneEvent
{
    int kind;
    /** Pid this event is for, or -1 for everyone (the shared buffer). */
    int receiver_pid;
    int pos;
    /** loc id, obj id, or (for LOC_ANIM) seq id. */
    int id;
    int shape, angle;
    int count, old_count;
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
struct Mock230ZoneLoc
{
    int x, z, level;
    int shape;
    int loc_id, angle;
    int base_loc_id, base_angle;
    /** 1 when a `loc_add` put this loc here *over* the map square's own loc,
     *  which is still standing underneath. A rebuild has to reproduce that
     *  rather than replace the square's loc, or removing this one again leaves
     *  the tile empty — see mock230_scene_add_loc. */
    int over_base;
};

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

/** Packed zone key, byte-identical to the reference's `ZoneMap.zoneIndex`. */
int
mock230_zone_index(
    int x,
    int z,
    int level);

/** Drop every zone. `mock230_world_reset` and shutdown; not a per-tick call. */
void
mock230_zone_free(struct Mock230Server* srv);

/* ------------------------------------------------------------------ */
/* Entity membership                                                   */
/* ------------------------------------------------------------------ */

/*
 * Membership is *reconciled*, not hooked.
 *
 * The reference calls `zone.enter()` / `zone.leave()` at each move site. Here an
 * npc's tile is written from five places (spawn, step, teleport, the respawn in
 * mock230_combat.c, the selftest) and a ground obj's activity from four, so the
 * npc and the obj each carry the zone they were last filed under and one pass
 * per tick fixes up whatever moved. A hook that is forgotten leaves an npc
 * filed in a zone it left; a reconcile cannot be forgotten.
 *
 * Both run in phase 8, before anything is encoded in phase 10.
 */
void
mock230_zone_sync_npcs(struct Mock230Server* srv);
void
mock230_zone_sync_objs(struct Mock230Server* srv);

/** Reconcile which zone every logged-in player is filed under. Phase 8, beside
 *  the npcs and objs, and for the same reason: membership is what makes
 *  "who does this client hold" answerable without a pool scan. */
void
mock230_zone_sync_players(struct Mock230Server* srv);

void
mock230_zone_player_refile(
    struct Mock230Server* srv,
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
mock230_zone_npc_refile(
    struct Mock230Server* srv,
    int slot);

void
mock230_zone_obj_refile(
    struct Mock230Server* srv,
    int slot);

/**
 * Npc slots within `radius` tiles of (x, z) on `level`.
 *
 * The world's own proximity query, and no longer what NPC_INFO asks: a tile box
 * is not clipped to the build area, so near its edge this answers with npcs
 * standing outside the region a client has a scene for. What a client is told
 * about comes from `struct Mock230PlayerArea` instead. This stays for the
 * world-side callers that genuinely want "who is near this tile" with no client
 * in the question.
 *
 * Returns the number written to `out`, never more than `max`.
 */
int
mock230_zone_npcs_near(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int radius,
    int* out,
    int max);

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
mock230_area_move(struct Mock230Player* player);

/**
 * Unsubscribe from every zone and empty the area.
 *
 * Both ends, which is the point: each zone in the window holds this player's
 * pid, so an area cleared from one side only would leave the map pushing into
 * a client that no longer claims the zone.
 */
void
mock230_area_clear(struct Mock230Player* player);

/**
 * Who is standing in this client's subscribed zones, right now.
 *
 * Walked at the moment a packet needs it rather than kept up to date, which is
 * what makes it impossible for the answer to be stale — it is derived from the
 * authoritative map every time it is asked. The plane and the view radius are
 * applied inside, so `out` holds only what the caller could actually send.
 *
 * `mock230_area_npcs` fills npc slots, `mock230_area_players` fills pids.
 */
int
mock230_area_npcs(
    const struct Mock230Player* player,
    int radius,
    int* out,
    int max);

int
mock230_area_players(
    const struct Mock230Player* player,
    int radius,
    int* out,
    int max);

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
 */
void
mock230_zone_loc_changed(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    int base_loc_id,
    int base_angle,
    int over_base);

/**
 * Queue LOC_ANIM for the loc at (x,z,level) with the given shape/angle and seq.
 * Does not mutate zone loc records — animation is ephemeral.
 */
void
mock230_zone_loc_anim(
    struct Mock230Server* srv,
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
mock230_zone_loc_merge(
    struct Mock230Server* srv,
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
mock230_zone_projanim(
    struct Mock230Server* srv,
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
mock230_zone_mapanim(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int spotanim,
    int height,
    int delay);

/** The loc record at this key, or NULL. */
struct Mock230ZoneLoc*
mock230_zone_loc_find(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape);

/** The record on this tile currently holding `loc_id`, or NULL. This is
 *  `loc_find`'s reach beyond the scene window: a runtime-added loc is
 *  addressable anywhere in the world, the way the reference's
 *  `World.getLoc` is. Static map locs outside the scene are not visible
 *  here — the ZoneMap is the diff, not the map. */
struct Mock230ZoneLoc*
mock230_zone_loc_find_id(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int loc_id);

/** Walk every recorded loc change in the world. Used by the rebuild, which has
 *  to put all of them back onto a scene that was just re-read from the cache. */
void
mock230_zone_locs_foreach(
    struct Mock230Server* srv,
    void (*fn)(struct Mock230ZoneLoc* loc, void* user),
    void* user);

/** Forget durable loc mutations and unsent loc events inside an absolute tile
 *  rectangle. Map-instance teardown calls this before returning a pooled
 *  destination to the allocator; otherwise the next tenant's rebuild reapplies
 *  the previous tenant's doors, walls, and collapsed scenery. */
void
mock230_zone_locs_clear_rect(
    struct Mock230Server* srv,
    int x,
    int z,
    int width,
    int height);

/* ------------------------------------------------------------------ */
/* Obj events                                                          */
/* ------------------------------------------------------------------ */

/*
 * Ground objs keep living in `Mock230Server.ground[]` — the pool is the
 * allocator and its slots are stable — and the zone holds the slot numbers. So
 * these three take a slot and read the rest off it.
 */
void
mock230_zone_obj_added(
    struct Mock230Server* srv,
    int slot);
void
mock230_zone_obj_removed(
    struct Mock230Server* srv,
    int slot);
void
mock230_zone_obj_counted(
    struct Mock230Server* srv,
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
mock230_zone_update_player(struct Mock230Player* player);

/** Forget what this client holds, so the next update re-sends every zone in
 *  full. Login, and every REBUILD_NORMAL — the rebuild resets the client's
 *  scene to the cache's version, which un-opens every door it had been told
 *  about. */
void
mock230_zone_player_reset(struct Mock230Player* player);

/** Phase 11: drop this tick's events and shared buffers. State stays. */
void
mock230_zone_reset(struct Mock230Server* srv);

/** Growth gauges for the perf harness: how many zones the map holds and the
 *  hash table capacity (never shrinks during a session). */
void
mock230_zone_map_stats(
    struct Mock230Server const* srv,
    int* out_count,
    int* out_capacity);

#endif
