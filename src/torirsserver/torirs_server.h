#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_H

/*
 * OSRS rev-230 game server: shared state, and the seam between its files.
 *
 * NOT a mock any more, whatever the filenames say. It is the server this
 * project runs, and the `ToriRSServer_` prefix is a misnomer twice over — it also
 * reads a rev-239 cache while speaking the rev-230 wire. Renaming is on the
 * roadmap (docs/osrs230_mockserver.md §6.1) and has not happened yet.
 *
 *   torirs_server_main.c       the listening socket and the 600 ms tick loop
 *   torirs_server_transport.c  where bytes come from — a socket, or an in-process queue
 *   torirs_server_ws.c         the socket byte stream: raw TCP or WebSocket, sniffed
 *   torirs_server_session.c    login handshake + ISAAC + inbound framing, as a state machine
 *   torirs_server_embed.c      the server hosted inside another process, no socket at all
 *   torirs_server_save.c       player persistence, one ini per player
 *   torirs_server_boot.c       the loader order, which is load-bearing
 *   torirs_server_world.c      game state — movement, NPCs, containers, interactions
 *   torirs_server_encode.c     every server->client packet
 *   torirs_server_scripts.c    the ServerScript host seam (166 of 396 opcodes)
 *   torirs_server_content.c    the LostCity content tree: packs, configs, map spawns
 *   torirs_server_objinfo.c    obj metadata (name / wearpos / stackable) from the cache
 *   torirs_server_friends.c    friend / ignore / private-chat state, keyed by name
 *
 * ── How the three structures relate ──────────────────────────────────
 *
 *   ToriRSServer   the WORLD. npcs, ground objs, scene, tick, scripts, and a
 *                   pool of players. One per process today.
 *   ToriRSServerPlayer   a PLAYER. Its own inventory, stats, varps, interaction —
 *                   plus `world`, `session` and `pid`, so it is addressable on
 *                   its own. This is the struct that gets saved.
 *   ToriRSServerSession  a CONNECTION. Transport, both ISAAC ciphers, the login
 *                   state machine, the inbound frame reader. No game state.
 *
 * These were one struct until the split. The session hangs off the player
 * rather than the world — a packet is addressed to a player, and with more than
 * one, "send the inventory" has to know whose.
 *
 * ── `srv->active_player` is not "the player" ──────────────────────────
 *
 * It is **whose turn it is**: the player the phase currently running, or the
 * packet currently being decoded, or the script currently executing is acting
 * on. The per-player phases set it as they iterate the pool
 * (`ToriRSServer_WorldSetActive`), the session sets it before dispatching a packet,
 * and it is meaningless outside those. A subsystem that reads it is asking "who
 * am I doing this for", which is the right question; a subsystem that reads it
 * to mean "the player in this world" is a bug, and the field is named the way it
 * is so that reads like one. It was called `player` while the pool held one, and
 * every such read looked correct.
 *
 * The reference keeps the same thing on its script state (`activePlayer`); what
 * is different here is that the engine paths read it off the world rather than
 * being handed a player, which is the residue of the single-player era and is
 * removed one subsystem at a time by giving the function a `ToriRSServerPlayer*`.
 *
 * What is genuinely per-player now: the entity streams (PLAYER_INFO tracks
 * other players, NPC_INFO tracks npcs per player), the scene rebuild flag, the
 * ground-obj "already told them" set, and every encoder. What is still shared:
 * the *scene origin* — one 104x104 build area for the whole world, so players
 * further apart than it can cover would fight over it (`ToriRSServer_SceneBuild` is
 * a singleton). See docs/osrs230_mockserver.md §6.1 step 3: zones are what
 * removes that.
 *
 * ── Authority ────────────────────────────────────────────────────────
 *
 * The server is authoritative in the way a real one is: the client asks to
 * walk, to wear, to swap two inventory slots, and nothing moves until a packet
 * comes back saying it did. That exercises the client's server-driven paths
 * rather than its local-prediction ones.
 *
 * Wire encoding lives entirely in torirs_server_encode.c and goes through
 * 3rd/rsareabuf. See docs/osrs230_mockserver.md for the protocol notes, the
 * transport seam (§3.13b), the interaction model (§3.13c), the dispatch tables
 * and opcode gap report (§3.13d), and the roadmap (§6.1).
 */

/*
 * The cache this server reads, and the profile revision every decoder is opened
 * with. The content tree in OSRS-Content/osrs239-content is an unpack of exactly
 * this cache — its `meta.ini` says so — and the ids in its pack files are that
 * cache's. Pointing one at a different cache than the other silently spawns the
 * wrong npc rather than failing.
 *
 * The server still speaks the rev-230 protocol: what moved to 239 is the content,
 * not the wire.
 */
#define TORIRSSERVER_CACHE_REVISION 239
/*
 * The pristine cache, deliberately: the world reads the same directory JS5
 * serves. Every server-authored value reaches the runtime through the content
 * tree's text and the server band — nothing server-side needs a bake — and
 * booting from one is how the world drifted from its own tree (a stale
 * cache.osrs239.baked carried old params and door counts: 32 selftest failures
 * against it vs 23 against pristine + tree, measured 2026-08-06). When
 * client-visible content matters, point BOTH the world and JS5 at the baked
 * cache; run-osrs239.sh makes that one knob ($CACHE).
 */
#define TORIRSSERVER_CACHE_DIR_DEFAULT "cache.osrs239"

#include "torirs_server_bank.h"
#include "torirs_server_interface_state.h"
#include "torirs_server_poh.h"
#include "torirs_server_wire.h"
#include "mock239_runclientscript.h"
#include "torirs_server_zone.h"

#include "engine/world_builder/collision_map.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

struct ToriRSServerConn;
struct ToriRSServerSession;

/* ------------------------------------------------------------------ */
/* Coordinates                                                         */
/* ------------------------------------------------------------------ */

/*
 * RuneScript packs a coord into one int as
 * (level << 28) | ((mx * 64 + lx) << 14) | (mz * 64 + lz).
 *
 * The compiler emits coord literals this way (`ssc_lex.c`) and the host has to
 * read them the same way, so this is a wire agreement between two halves of
 * this repo rather than a convenience.
 *
 * It is stated here because it was already stated in five places —
 * torirs_server_scripts.c's four file-local statics, torirs_server_db.c, torirs_server_worldmap.c,
 * torirs_server_world.c and ssc_lex.c — and a per-domain opcode file that needs a
 * coord had no way to reach any of them. Adding a sixth private copy is the
 * drift this repo has paid for before, so the shared form lives in the header
 * the domain files already include. **The statics in torirs_server_scripts.c should
 * be deleted in favour of these**; that is a pure deletion and it needs the
 * one-time torirs_server_scripts.c exception this lane does not hold.
 */
static inline int32_t
ToriRSServer_CoordPack(int level, int x, int z)
{
    return (int32_t)(((uint32_t)level << 28) | ((uint32_t)x << 14) | (uint32_t)z);
}

static inline int
ToriRSServer_CoordLevel(int32_t coord)
{
    return (int)(((uint32_t)coord >> 28) & 0x3);
}

static inline int
ToriRSServer_CoordX(int32_t coord)
{
    return (int)(((uint32_t)coord >> 14) & 0x3fff);
}

static inline int
ToriRSServer_CoordZ(int32_t coord)
{
    return (int)((uint32_t)coord & 0x3fff);
}

/**
 * LostCity CoordGrid.fine(pos, size) — half-tile face point for FACE_COORD.
 * A 1×1 tile at `pos` centres at `2*pos+1`; a WxL loc centres at `2*pos+W`.
 */
static inline int
ToriRSServer_CoordFine(int pos, int size)
{
    return pos * 2 + size;
}

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

enum
{
    /*
     * Npcs this world can hold, and — separately — npcs one client can be
     * tracking.
     *
     * These were one number, 256, annotated "the tracked count is an 8-bit
     * field on the wire, so this must stay under 256". The annotation is true
     * about the *tracked* count and says nothing about the world. In OSRS239
     * v5, NPC_INFO's 16-bit index is local to one player's client and names a
     * nearby instance; it is neither a cache type id nor a world-roster id.
     * (The classic stream's equivalent index is 14 bits.)
     *
     * The mock currently reuses its world-pool slot as the client's local-slot
     * number because TORIRSSERVER_NPC_MAX is only 4096. That is an implementation
     * convenience, not protocol identity; a larger pool would add a per-player
     * slot map rather than constrain cache NPC ids.
     *
     * So: TORIRSSERVER_NPC_MAX is a memory decision (336 bytes per npc, statically
     * allocated in the world), TORIRSSERVER_TRACKED_NPC_MAX is the wire's.
     *
     * What made the world cap load-bearing was the encoder scanning every slot
     * in the world for every client every tick; NPC_INFO asks the ZoneMap for
     * the npcs within 15 tiles now (ToriRSServer_ZoneNpcsNear), so the two numbers
     * are free to be different sizes.
     *
     * 4096 is sized against TORIRSSERVER_STATIC_SPAWN_OUT below, not against the
     * roster: the world roster is 23,139 spawns (docs/ITEM_AND_NPCS.md) and
     * what has to fit is the densest *window*, 2,250 spawns (Varrock, near
     * 3392,3328). The rest is headroom for everything content npc_adds.
     *
     * **This is a memory decision and not a wire one**, and an earlier version
     * of this comment had that wrong:
     *
     *   classic (230)  14-bit client-local instance index
     *   v5 (239)       16-bit client-local index; 14-bit initial type, with a
     *                   same-packet mask-0x1 unsigned-16-bit replacement for
     *                   a type above 0x3fff
     *
     * The slot namespace belongs to each client, not to the cache and not to
     * the world roster. What a *client* is told comes out of that client's
     * `ToriRSServerPlayerArea`, and a 7x7 zone window cannot hold more npcs than the
     * stream's own TORIRSSERVER_TRACKED_NPC_MAX.
     */
    TORIRSSERVER_NPC_MAX = 4096,
    TORIRSSERVER_TRACKED_NPC_MAX = 255,

    /*
     * How many distinct npc names one client can have outstanding.
     *
     * A client never holds more than TORIRSSERVER_TRACKED_NPC_MAX npcs, so 255 names
     * would do. The headroom is for the one case that would otherwise bite: a
     * name released this tick and handed to a different npc in the same packet.
     * With 1024 and rotating allocation that cannot happen in any realistic
     * churn, and the cost is 2KB per client.
     *
     * It must also fit the wire's index field, which is 16 bits on v5 and 14 on
     * classic — see the static assertion in torirs_server_encode.c.
     */
    TORIRSSERVER_CLIENT_NPC_SLOTS = 1024,

    /*
     * How much of the world roster is standing up at any moment.
     *
     * The content tree states where every npc in OldSchool lives — 23,139 of
     * them. Creating all of them was what the roster loop used to do, and it
     * worked while the roster was Lumbridge's 63. It cannot scale as an eager
     * in-memory strategy, but the wire is not the reason: the client-local
     * instance index (16 bits on OSRS239 v5, 14 on classic) is not the world or
     * cache namespace.
     *
     * So the roster is a statement about the world and the pool is a window on
     * to it. A spawn is realised when its home tile comes within
     * TORIRSSERVER_STATIC_SPAWN_IN of the scene centre and retired when it passes
     * TORIRSSERVER_STATIC_SPAWN_OUT; the gap between them is hysteresis, and it is
     * the whole reason there are two numbers. With one, a player pacing across
     * the boundary would create and destroy the same npcs every rebuild, and
     * each cycle resets their hitpoints, their aggression and their `[ai_spawn]`.
     *
     * IN is well past what anyone can see. The client is told about npcs within
     * 15 tiles and the scene itself is TORIRSSERVER_SCENE_TILES (104), so at 160 an
     * npc has existed for a hundred tiles' walking before it can be looked at —
     * which is the property the old create-everything loop was defending, and
     * the reason it is stated as a distance rather than tuned down to the
     * visible radius.
     */
    TORIRSSERVER_STATIC_SPAWN_IN = 160,
    TORIRSSERVER_STATIC_SPAWN_OUT = 224,
    /* The client sends at most 25 waypoints per move request; the server stores
     * the same bound and walks greedily toward the current one (LostCity
     * PathingEntity.waypoints). Scratch buffers for a full expanded route —
     * NPC one-step routing, selftests — still use TORIRSSERVER_STEP_MAX. */
    TORIRSSERVER_WAYPOINT_MAX = 25,
    TORIRSSERVER_STEP_MAX = 256,
    TORIRSSERVER_INV_SLOTS = 28,
    TORIRSSERVER_WORN_SLOTS = 14,

    /*
     * Rows in a container registry table — see torirs_server_container.h.
     *
     * A storage ceiling, not a container count: the cache names 1026 invs and
     * every one of them is registrable, so this bounds how many a single player
     * (or the world) can hold *at once*. The reference has no equivalent because
     * `Player.invs` is a Map; a fixed table is what a fixed player struct can
     * carry, and overflowing it is reported rather than absorbed.
     */
    TORIRSSERVER_CONTAINER_MAX = 16,
    /** Shared (world-scoped) rows are created on first use and never evicted
     *  (torirs_server_container.h's header comment), so this has to cover every
     *  distinct shop any player visits over one server lifetime, not just
     *  those open at once. 640 clears the wiki-catalogued shop roster
     *  (docs/SHOPS_PLAN.md §1.3: 593 distinct shops) with headroom for the
     *  handful of non-shop shared containers (party chest, GE offer slots). */
    TORIRSSERVER_WORLD_CONTAINER_MAX = 640,
    /** How many components may listen to one inv at once (`inv_transmit`).
     *  For a player-owned row this is worn tab + equipment-stats + a side
     *  panel; for a shared shop row it is one (component, player) pair per
     *  player who has it open — TORIRSSERVER_PLAYER_MAX (8) players, shopmain and
     *  shopside each, doubled for headroom. */
    TORIRSSERVER_CONTAINER_LISTENERS_MAX = 16,

    /*
     * Players this world can hold.
     *
     * The wire's own ceiling is 2047 (the 11-bit pid field), so this is a memory
     * decision rather than a protocol one: a player is ~25 KB, most of it varps.
     * Eight is what fits comfortably in the statically-allocated world the socket
     * server keeps and is well past what any test drives.
     *
     * Raising it is not what made multiplayer work and lowering it is not what
     * would break it: the pool was already here at 1. What made it work is that
     * the streams became per-player — see `ToriRSServerPlayer.tracked_*` and
     * `ToriRSServer_SendPlayerInfo`.
     */
    TORIRSSERVER_PLAYER_MAX = 8,

    /*
     * Container ids, interface ids and component uids are NOT here. They are
     * the cache's numbers, not this server's, so they come from the content
     * tree by name — see torirs_server_ids.h. What stays in this file is what sizes
     * an array or what the protocol means.
     */

    /* Scene is 104x104 tiles based at (zone - 6) * 8. Rebuild once the player
     * comes within 16 tiles of an edge, the same margin the reference uses. */
    TORIRSSERVER_SCENE_TILES = 104,
    TORIRSSERVER_REBUILD_MARGIN = 16,

    /*
     * How far NPC_INFO reaches — the radius the low-resolution adds use and the
     * range the high-resolution loop keeps at. The 6-bit delta on the v5 wire
     * could carry ±31, and using it would be wrong: an npc added at 20 tiles is
     * out of range on the very next tick, so it is removed, re-added, removed,
     * and never renders. The wire's capacity is not the view distance.
     *
     * It is measured to the npc's FOOTPRINT, not to its south-west origin —
     * `ToriRSServer_NpcViewDeltas`. The distinction is the whole difference
     * between a size-1 npc and a boss: TzKal-Zuk is 7x7 with his origin on his
     * west edge, so a corner measure removed him from the client 16 tiles east
     * of that origin while his nearest tile was 10 tiles away and his model
     * filled the screen. That looked like a painter bug for as long as nobody
     * checked whether the entity was still in the client's pool.
     *
     * Two bounds hold the reach in:
     *
     *   - The wire. The v5 add carries 6-bit signed deltas from the ORIGIN, so
     *     origins may reach 15 + (TORIRSSERVER_NPC_SIZE_MAX - 1) = 22 and 31 is the
     *     ceiling. The classic add carries 5 bits (-16..15) and cannot express
     *     even 16, so that encoder keeps the corner box — see there.
     *   - The scene. It used to be argued from this constant sitting one tile
     *     inside TORIRSSERVER_REBUILD_MARGIN; that argument is gone now that a
     *     footprint reaches past the margin, and it was never the real one.
     *     `window_holds` clips the client's zone window to the build area, so
     *     the candidate set cannot contain an npc the client has no scene for
     *     however far this reaches. The NPC_INFO selftest in
     *     torirs_server_world_selftest.c builds that edge rather than walking
     *     to it precisely because the margin coincidence used to hide the
     *     structural clip.
     */
    TORIRSSERVER_NPC_VIEW_TILES = 15,

    /*
     * ...AND HOW FAR IT GROWS WHEN THE ROOM IS EMPTY.
     *
     * 15 is the reference's RESTING radius, not a fixed one. OSRS' npc stream
     * carries a per-observer view distance that starts at 15 and expands a tile
     * per tick while the client is holding few npcs, snapping back to 15 the
     * moment it is holding many — cheap in a crowded city, and the reason a
     * boss at the far end of an otherwise empty room is drawn at all.
     *
     * Without it this server's radius was flat, and the failure is silent in a
     * way worth spelling out: an npc outside the radius is not merely undrawn,
     * it is not in the client's list, so a `npc_anim` played on the tick it
     * dies is written to nobody and CLEARED in phase 12. There is no retry. A
     * ToB Nylocas Matomenos walking from its spawn to the Maiden crosses 15
     * tiles from a player standing where that room is fought from, so half of
     * every wave died off-list — measured 7 of 14 death animations lost, and
     * from the player's side a crab that simply vanishes instead of dying.
     *
     * The ceiling is the wire's, not a preference: the v5 add writes 6-bit
     * signed deltas from the ORIGIN, so the furthest expressible origin is 31
     * and a footprint at `view` puts its origin at `view + TORIRSSERVER_NPC_SIZE_MAX
     * - 1`. 24 + 7 = 31 exactly.
     */
    TORIRSSERVER_NPC_VIEW_TILES_MAX = 24,

    /*
     * The crowd that stops the growth. The reference compares against its
     * preferred npc count and this list is 255 long, so a quarter of it is the
     * same shape of rule: below this the radius grows, at or above it the
     * radius returns to its resting 15 rather than decaying, because a client
     * that has just walked into a crowd wants the cheap radius THIS tick.
     */
    TORIRSSERVER_NPC_VIEW_CROWD = 64,

    /*
     * The biggest npc footprint the view test reaches out for, and a real
     * limit rather than a description: the zone pre-reject in `area_entities`
     * pads by it, so an npc bigger than this can be dropped from the candidate
     * set with its body in plain view. `ToriRSServer_WorldNpcSpawn` warns when
     * the cache hands it one, which is the only way that stays loud.
     *
     * 8 covers everything in the OSRS239 roster (Zuk is the largest at 7) with
     * a tile to spare, and 15 + (8 - 1) = 22 is inside both the v5 delta's ±31
     * and the zone window's guaranteed 24 tiles (TORIRSSERVER_ZONE_VIEW_RADIUS * 8).
     */
    TORIRSSERVER_NPC_SIZE_MAX = 8,

    /*
     * The same distance for players, and a separate constant because it is a
     * separate wire fact: a new-player record carries 5-bit signed deltas, so
     * it reaches -16..15 and 15 is the most that can be added without the
     * coordinate wrapping and putting the player on the wrong side of the
     * observer. The npc figure is a choice about churn; this one is arithmetic.
     */
    TORIRSSERVER_PLAYER_VIEW_TILES = 15,


    /*
     * Player variables the scripts can read and write.
     *
     * This used to be 256, which covered everything the mock's own content
     * wrote. The bank broke that: its settings are *varbits*, and a varbit is a
     * bit range inside whichever varplayer the cache happens to have put it in
     * — the nine tab counters alone live in varps 867, 1052, 1053, 1793 and
     * 3750, and the side panel's slot locks are varp 4611. None of those are
     * ids content picked; they are the client's, so the array has to reach
     * them. rev 230 has about 5,000 varps.
     */
    /* Sized off the cache, plus room for the server's own.
     *
     * This was 5000 and the cache's highest varp id is 5704, so every varp from
     * 5000 up was silently dropped: ToriRSServer_WorldSetVarp bounds-checks and
     * returns, so a write to one looked like it worked and transmitted nothing.
     *
     * The tail above the cache maximum is where `ids = server` varps land —
     * content's own state, like the %com_* combat set, which the engine reads
     * and never transmits. Those must be declared `transmit=no`: a real rev-230
     * client has no varp of that id and would be told about a variable it
     * cannot have.
     *
     * Still a flat per-player array (docs/osrs230_mockserver.md §6.1 wants it
     * sparse); this makes it correct before it makes it small. */
    /*
     * How far the cache's own varps reach — 5725, meaning ids 0..5724.
     *
     * Not the varp group's file count, which is 5705. A varbit names a varp id
     * whether or not the varp group carries a record for it, and this cache has
     * varbits based as high as 5724. Sizing this off the group put twenty
     * server varps (`%com_*`, `%damagestyle`, `%prayer_drain_*`,
     * `%newplayer_seeded`, `%mock_mapzone_log`) directly on top of packed
     * varbits. `ToriRSServer_VarbitLoad` reports the real ceiling at boot and
     * complains if a cache ever reaches past this.
     */
    TORIRSSERVER_VARP_CACHE_MAX = 5725,
    /*
     * Room above the cache for the tree's own varps, and it is a MEASUREMENT.
     *
     * 512 was a guess and the tree outgrew it: `configs/all.varp.compack` now
     * reaches 6223 where the array reached 6216, and the seven ids over the end
     * were the `%com_*` block the combat stats are computed into. The symptom
     * was not "combat is slightly wrong" — `[proc,player_combat_stat]` aborted
     * on `POP_VARP` every time an npc swung, so `[ai_opplayer2,_]` never
     * finished, the goblin never hit back, and the fight ran to the selftest's
     * 200-tick cap. A varp allocation is content's to make (PORTING_GUIDE
     * §2.4 item 4) and this array is the engine's floor beneath it, so the
     * floor is now checked rather than assumed: `ToriRSServer_ContentLoad` refuses
     * to boot when the tree declares a varp this cannot hold, and says by how
     * much.
     *
     * Raised 1024 -> 2048 when Chambers of Xeric was added. The tree's highest
     * allocated varp reached 6774 against a ceiling of 6749, and the symptom
     * was exactly the one described above: `[proc,cox_selftest_olm_spec_order]`
     * aborted on PUSH_VARP, so the whole `::coxrun` fixture died before it
     * could report, and its result varp read back as the harness's own seed
     * rather than as a failure count. The abort message names the varp, which
     * is what made it findable at all.
     */
    TORIRSSERVER_VARP_SERVER_HEADROOM = 2048,
    TORIRSSERVER_VARP_COUNT = TORIRSSERVER_VARP_CACHE_MAX + TORIRSSERVER_VARP_SERVER_HEADROOM,
    /*
     * World-shared variables — `%name` resolving to SSC_SYM_VARS, one value per
     * world rather than one per player.
     *
     * The cache names none of these: `vars` is an authored namespace allocated
     * from 0 by tools/ss_allocate.py into `pack/vars.alloc`, so this is a pure
     * server-side id space and 256 is a ceiling on how many facts a world can
     * hold, not an echo of anything in the cache. It is checked at boot the way
     * TORIRSSERVER_VARP_COUNT is, rather than trusted: a `vars` id past the end would
     * otherwise abort the first script that touched it.
     *
     * What wants this is state that is the *world's* and not a player's, which
     * before now had nowhere to live and so was written per-player and silently
     * diverged between two players standing in the same room — Pyramid Plunder's
     * correct entrance (one door for everyone, rerolled every 25 minutes) and its
     * per-room correct tomb door are the first two.
     */
    TORIRSSERVER_VARS_COUNT = 256,
    /* Ground items. The world roster is 2,256 obj spawns
     * (docs/ITEM_AND_NPCS.md); a busy fight adds a handful per kill, and those
     * expire.
     *
     * Unlike the npc pool this holds the *whole* roster rather than a window on
     * it, and the difference is the wire: a ground obj has no slot on it —
     * OBJ_ADD is zone state and the client is told about a zone when it enters
     * one — so nothing above bounds this but memory, and 4096 entries is a
     * rounding error next to a map square. Windowing it would buy nothing and
     * cost the same hysteresis problem the npcs have. */
    TORIRSSERVER_GROUND_MAX = 4096,
    /**
     * Pending `[ai_queue<n>]` entries per npc.
     *
     * The reference's is a linked list with no cap at all, and `npc_queue`
     * *aborts* the script when this one is full — so the number has to have
     * room for every entry a single exchange can leave in flight, not for the
     * typical one. A ranged swing arms two (`[ai_queue1]` retaliation and
     * `[ai_queue2]` damage, both at the tick the projectile lands), and a rapid
     * shortbow's three-tick cadence fires again before a shot from ten tiles
     * away has arrived: four is exactly the point where a second shot in flight
     * kills the swing script. Eight leaves the same headroom for a fight where
     * an npc is being hit by more than one thing.
     */
    TORIRSSERVER_NPC_QUEUE_MAX = 8,
    /** Script-owned integer state slots carried by each live npc instance. */
    /* Slots 0..15 are established runtime state; slot 16 is the
     * GiantChinchompa post-special dismissal latch, slot 17 retains the Spirit
     * Graahk's generation-safe normal-combat target, and 62 is npc-side poison
     * severity (`^npc_poison_var_slot`, skill_combat/configs/combat.constant).
     *
     * 64 rather than one past the highest slot in use, because content picks
     * these numbers and nothing checks them at compile time: `npc_var_get`
     * aborts the running script past the end, and content reasonably assumes a
     * slot chosen far from the crowded low range is free. Poison did exactly
     * that at 62, and since `[ai_opplayer2,_]` — the shared default melee AI —
     * ticks poison one line above auto-retaliate, the abort took retaliation
     * with it and every default-AI npc stopped hitting back. Headroom here is
     * 46 words × TORIRSSERVER_NPC_MAX, under a megabyte, and it buys back a class of
     *
     * Note on cost: `struct ToriRSServer` is a stack local in the selftest
     * (torirs_server_world.c), so each slot is TORIRSSERVER_NPC_MAX * 4 bytes of frame.
     * At 64 that frame needs a raised stack or a static/heap `srv`; growing
     * this further without checking segfaults the suite before its first
     * check. */
    TORIRSSERVER_NPC_VAR_MAX = 64,

    /**
     * Where an npc is in the death sequence — `ToriRSServerNpc.death_stage`.
     *
     * LostCity spends a death across four ticks and three suspensions, and the
     * stages here are those suspensions. See `ToriRSServer_CombatNpcTick` for the
     * tick-by-tick ledger and `[proc,npc_death]`
     * (LostCity_Server/content/scripts/skill_combat/scripts/npc/npc_death.rs2)
     * for the script this reproduces.
     */
    /** Alive. Also the value while `death_tick` is -1, which is the real gate. */
    TORIRSSERVER_DEATH_NONE = 0,
    /** Hitpoints reached zero; `[ai_queue3]` is due next npc phase.
     *  The reference is `npc_queue(3, 0, 0)` in `[proc,npc_default_damage]`. */
    TORIRSSERVER_DEATH_QUEUED = 1,
    /** The death script is parked in `npc_arrivedelay`, letting the step it was
     *  mid-way through finish before it falls over. */
    TORIRSSERVER_DEATH_ARRIVE = 2,
    /** The death animation is playing; the corpse is waiting out `npc_delay(1)`
     *  before the drop table runs and `npc_del` removes it. */
    TORIRSSERVER_DEATH_CORPSE = 3,
    /** The drop table has run — exactly once, which is what this stage exists to
     *  guarantee — and the npc is waiting to be removed. It stays here while a
     *  suspended `[ai_queue3]` still owns it, and leaves without being removed
     *  at all if that script put its hitpoints back. */
    TORIRSSERVER_DEATH_REAP = 4,
    /**
     * Loc mutations that can be waiting to revert at once. A busy mining site
     * is a dozen; the sizing case is a whole floor pattern armed on one tick.
     * The Hunllef's damaging-floor patterns are rectangles over its 12x12
     * arena, and the largest — the paired full-length bands — covers 96 tiles,
     * every one of them a `loc_add` with a duration. At 128 that single pattern
     * left 32 slots for the rest of the world, and an overflow here does not
     * fail loudly: `ToriRSServer_WorldLocRevertQueue` warns on stderr and the loc
     * stays put forever, which for that pattern means a permanently damaging
     * floor tile. The cost is 40 bytes each, so headroom is cheap.
     */
    TORIRSSERVER_LOC_REVERT_MAX = 512,
    /**
     * Drops waiting for their delay to run out (`inv_dropitem_delayed`).
     *
     * Smaller than the revert table because the delays are short — the one
     * caller is ranged ammo landing a tick or two after the shot, so an entry
     * lives about as long as an arrow is in the air. A stray-arrow volley is a
     * handful in flight at once, not a mining site's worth.
     */
    TORIRSSERVER_OBJ_DELAYED_MAX = 32,
    /* `TORIRSSERVER_LOOT_TICKS` was here — 200 ticks on the floor, annotated
     * "LostCity's ^lootdrop_duration", beside a content tree already stating
     * `^lootdrop_duration = 200`. Naming the constant you are duplicating does
     * not stop it being a duplicate: the engine reads the real one now, through
     * `ToriRSServer_Ids()->lootdrop_duration`. */

    /* Parked script bookkeeping. */
    /*
     * 64, up from 32, which was up from 16: the reference's queue is an
     * UNCAPPED linked list, so every number here is ours and every overflow is
     * an SSVM abort content cannot see coming — it does not drop the entry, it
     * kills the rest of the script that queued it.
     *
     * 32 came from the QBD's worst case (three wall waves + their steppers,
     * four soul shadows, Royal-crossbow bleed splats, the platform hazard,
     * siphon and worm chains). The Theatre's Nylocas room beats it by a wide
     * margin and does so as a matter of routine: the room's own cap is 24
     * nylocas alive, every one of them swings at a player every three ticks
     * through `queue*(combat_damage_player, ...)`, and a wave that ages out
     * detonates into everyone within two tiles at once. A tank taking the room
     * is the case, and 32 was not enough for it.
     *
     * What that cost was not a missed hit. The overflow aborted whatever proc
     * was mid-queue, and the one that noticed was the SUPPORT COLLAPSE: its
     * damage sweep is a `huntall` loop of `queue*`s, and the abort took the
     * retarget that follows it with it — so a pillar fell, hurt nobody past the
     * cap, and left its attackers chewing on a support that was no longer
     * there.
     */
    TORIRSSERVER_QUEUE_MAX = 64,
    /** Arguments one queued script can carry. The reference's list is
     *  unbounded; five are used by the GWD barrage landing queues, and eight
     *  leaves fixed-size headroom for the next content-owned effect. Overflow
     *  is reported, never truncated silently. */
    TORIRSSERVER_QUEUE_ARG_MAX = 8,
    /*
     * The engine queue is its own array because the reference's is its own list:
     * `unlinkQueuedScript`'s default branch walks `queue` and `weakQueue` and
     * never `engineQueue`, so `clearqueue` cannot cancel a zone trigger. Sharing
     * one array would have made that a filter somebody could forget.
     *
     * Four is the most one tick can add (mapzoneexit, mapzone, zoneexit, zone)
     * and entries only accumulate while the player is busy — which is also when
     * the reference refuses to walk them anywhere new. Eight is two crossings'
     * worth of slack; an overflow is reported for the same reason
     * `ToriRSServer_ScriptsQueueHook`'s is.
     */
    TORIRSSERVER_ENGINE_QUEUE_MAX = 8,
    /*
     * Player timers, and the one number here that was never sized against the
     * content tree.
     *
     * The reference has no ceiling at all — `Player.timers` is a Map — so any
     * figure here is this engine's invention, and 8 was small enough to be hit
     * on a quiet login. Measured on a fresh session (TORIRSSERVER_TIMER_TRACE, since
     * removed): `teleport_cooldown_clock`, `health_regen`, `stat_restore`,
     * `playtime_tick`, `poison`, `fermenting_wine` and `puro_circle_tick` are
     * armed before the player has done anything, which is 7 of 8. The tree
     * declares about forty distinct player timers, and prayer/curse drain,
     * `summoning_tick`, `farming_growth`, `sa_regen`, `stamina_expire` and the
     * antifire pair are all ordinary things to hold at once.
     *
     * What a full table cost is worth stating, because it does not look like a
     * timer bug from the screen. `settimer` aborts the running script, and
     * `~curse_toggle` calls it from `~curse_switched` *after* it has already set
     * the mask and the headicon and *before* `~curse_activate_anim` — so the
     * first click on Turmoil lit the button, printed nothing, played nothing,
     * and drained nothing. The second click read the mask as on and turned it
     * back off; the third re-armed into whatever slot had since expired and
     * worked. Reported as "I have to click Turmoil three times". `~prayer_toggle`
     * has the same shape via `~prayer_switched`.
     *
     * 32 is four times the measured login load and costs 480 bytes per player.
     * Timers are session state and are not persisted, so this is not a save
     * format number.
     */
    TORIRSSERVER_TIMER_MAX = 32,
    TORIRSSERVER_WORLD_QUEUE_MAX = 16,
    /*
     * Distinct components one parked script may be resumed by.
     *
     * The chat dialogues need one (the container, or the continue prompt), and
     * 8 was chosen for those. `skillmulti` (interface 270, the make-menu) needs
     * eighteen: its product rows are eighteen *separate* static components
     * `a`..`r`, not sub-ids of one container, so `~skill_multi` registers a
     * resume button per row and which one came back is `last_com`. A table that
     * silently stopped at 8 would leave rows `i`..`r` looking live and doing
     * nothing — `if_addresumebutton` drops the overflow rather than failing.
     *
     * Construction's add-room panel is larger again: 27 visible room rows are
     * separate static components. 32 covers that cache-shaped menu plus a few
     * controls, at 4 bytes each.
     */
    TORIRSSERVER_RESUME_BUTTON_MAX = 32,
    /*
     * Highest sub-id `if_addresumebutton` arms on the component it registers.
     *
     * A resume button on a *container* is the multi-choice dialogue: its rows
     * are `cc_create`d children with sub-ids 1..5, and arming only slot 0 arms
     * the empty container.
     *
     * 28 is `skillmulti`'s number and it is exact on both sides, which is why
     * it is not rounded up. On the low side, `skillmulti_itembutton_triggered`
     * (clientscript 2052) resumes with `cc_find($row, %varcint200)` — **the
     * resume sub-id IS the chosen quantity** — and `skillmulti_itembutton_draw`
     * creates hidden children 0..28 for exactly that lookup, 28 being the cap
     * `skillmulti_quantitybutton_x_receive` clamps a typed amount to. On the
     * high side, the *visible* children of a row (the beige button faces and
     * the item model) are created at sub-ids 29 and up: arming those would make
     * them interactive, and since they carry no op and no CS2 hook they would
     * win the topmost hit and answer with a bare EVENT_CLICK resume — the row's
     * "Make" op, and with it the quantity, would never run. The range must
     * cover the quantity slots and stop before the decoration.
     */
    TORIRSSERVER_RESUME_SUB_MAX = 28,

    /*
     * Arguments `runclientscript*` will carry.
     *
     * Three numbers bound this and they are not the same number: the compiler
     * builds at most SSC_MAX_VARARG_TYPES of them, the wire reader stops at
     * PKT_RUNCLIENTSCRIPT_ARG_MAX (`src/net/rev/revpacket.h`), and this is the
     * one in the middle. All three are 28 so `ge_pricechecker_prices` (28 ints)
     * compiles and arrives intact. The host case *aborts* on a longer type
     * string rather than truncating, because a clientscript run with three of
     * its four arguments does not fail, it draws the wrong panel.
     */
    TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX = 28,

    /*
     * Combat: the unarmed attack interval, and nothing else.
     *
     * Four ticks, which is what a weapon with no `attackrate` param and a player
     * with no weapon swing at. Everything an *npc* does is on its record instead
     * — the attack rate, the reach, how long its corpse lies there and how long
     * until it comes back — because those are per-npc and a content author owns
     * them. `TORIRSSERVER_DEATH_TICKS`, `TORIRSSERVER_RESPAWN_TICKS` and
     * `TORIRSSERVER_ATTACK_RANGE` used to sit here saying otherwise; they are
     * `death_delay`, `respawnrate` and `attackrange` on `struct ToriRSServerNpcDef`,
     * and the engine reads them through `ToriRSServer_ContentNpcDefault()`.
     *
     * `TORIRSSERVER_PLAYER_MAX_HP` was here too and had no readers at all: the
     * player's maximum is `stat_level[hitpoints]`, which the levels decide.
     */
    TORIRSSERVER_ATTACK_SPEED = 4,

    /*
     * Run energy is kept in hundredths of a percent, which is the unit
     * OldSchool's own drain formula is written in: one running step off an
     * unencumbered player costs 67 of these, so a percent is not a fine enough
     * grain to hold the remainder. The wire and the orb carry the percent.
     */
    TORIRSSERVER_RUN_ENERGY_MAX = 10000,

    /* IF_SETEVENTS bit 0: the component accepts a plain click, answered with
     * IF_BUTTON. Mirrors RS_MINIMENU_EVENT_CLICK on the client. */
    TORIRSSERVER_EVENT_CLICK = 0x1,

    /* Wire sentinels in the classic info streams. */
    TORIRSSERVER_PLAYER_TERMINATOR = 2047,
    TORIRSSERVER_NPC_TERMINATOR = 16383,

    /*
     * FACE_ENTITY's id space is two ranges, not one.
     *
     * The client reads a face target below 32768 as an *npc slot* and one at or
     * above it as `32768 + player index` (world_cycle.c, WORLD_FACING_*), and
     * resolves that index by searching the player pool for a matching
     * `server_pid` (world.c, World_PlayerGetByServerPid). The number is
     * therefore *absolute*: it names the same player on every client's stream,
     * which is what lets one shared NPC_INFO encode serve every observer. The
     * reference does exactly this — `PathingEntity.setFaceEntity()` stores
     * `this.target.slot + 32768`, the world-global pool slot, and computes the
     * block once per npc per tick (`World.cycle` -> `rsbuf.computeNpc`).
     *
     * Writing a bare 2047 asks the client to face npc slot 2047, which never
     * exists: the lookup returns NULL, the branch falls through, and the npc
     * keeps whatever yaw it had. Nothing reports it. Every npc in a fight stood
     * facing wherever it happened to be walking.
     *
     * There is deliberately no `TORIRSSERVER_FACE_LOCAL_PLAYER` constant. There used
     * to be — `TORIRSSERVER_FACE_PLAYER_BASE + 2047`, "the local player" — and it is
     * a *self-alias*: a value whose meaning depends on who is reading it,
     * written into a field every observer reads. With one client that was
     * invisible; with two it made a goblin fighting alice turn to face bob on
     * bob's screen. The face id is `TORIRSSERVER_FACE_PLAYER_BASE + player->pid`
     * now, through `ToriRSServer_NpcFacePlayer()` (below the npc struct), and the
     * constant is deleted so no site can reach for "whoever happens to be
     * watching" again.
     */
    TORIRSSERVER_FACE_PLAYER_BASE = 32768,

    /* New-npc record field widths. These MUST match what the rev-230 table
     * declares (GameProtoRevTable.npc_slot_bits / .npc_type_bits) — the two
     * ends of the same bitstream. A mismatch does not fail the decode, it
     * shifts every field after the offending one. */
    TORIRSSERVER_NPC_SLOT_BITS = 14,
    TORIRSSERVER_NPC_TYPE_BITS = 14,
    TORIRSSERVER_NPC_TYPE_MAX = (1 << TORIRSSERVER_NPC_TYPE_BITS) - 1,
    /*
     * Revision 239 also has a 14-bit initial type. A config above that range
     * is installed in the same NPC_INFO packet through its 0x1 transformation
     * update, whose transformed unsigned-short operand carries 0..65535.
     * 0xffff is the block's "none" sentinel.
     */
    TORIRSSERVER_NPC_CONFIG_MAX = 65534,
};

/*
 * Extended-info mask bits, mirroring the client's readers exactly
 * (src/net/rev/packets/pkt_player_info.h and pkt_npc_info.h). The writers must
 * emit fields in ascending bit order, because that is the order the reader
 * tests them in — the mask says which fields are present, never where.
 */
enum
{
    TORIRSSERVER_PMASK_APPEARANCE = 0x001,
    TORIRSSERVER_PMASK_SEQUENCE = 0x002,
    TORIRSSERVER_PMASK_FACE_ENTITY = 0x004,
    TORIRSSERVER_PMASK_SAY = 0x008,
    TORIRSSERVER_PMASK_DAMAGE = 0x010,
    TORIRSSERVER_PMASK_FACE_COORD = 0x020,
    TORIRSSERVER_PMASK_CHAT = 0x040,
    /* Not a field: it says the mask itself is two bytes. See put_player_mask. */
    TORIRSSERVER_PMASK_BIG_UPDATE = 0x080,
    TORIRSSERVER_PMASK_SPOTANIM = 0x100,
    TORIRSSERVER_PMASK_EXACT_MOVE = 0x200,
    TORIRSSERVER_PMASK_DAMAGE2 = 0x400,
};

/*
 * Every hitsplat one entity takes in one tick.
 *
 * This used to be a single `damage`/`damage_type` pair, and a second hit on the
 * same entity in the same tick simply overwrote the first — one splat reached
 * the client where two were dealt. It is not a rare corner: anything with more
 * than one attacker coalesces (measured in the Inferno's Zuk phase, where the
 * ranger and the mager land on the Ancestral Glyph together: 38 hits produced
 * 27 splats, and all 11 that vanished were same-tick pairs). From in front of
 * the game it reads as hitsplats rendering "only sometimes", which is how it
 * was reported.
 *
 * Neither the wire nor the client was ever the limit. Both hitmark encoders are
 * LISTS — a count followed by that many (type, value, delay, slot-limit)
 * quadruples — and the rev-239 actor keeps four concurrent hitmark slots, which
 * is what the fourth field of each quadruple states and what
 * `WORLD_ENTITY_DAMAGE_SLOTS` is on the client. Only the mock's own per-tick
 * state was scalar, so it could never say more than one.
 *
 * Four, to match the client's slot count: a fifth splat in one tick has nowhere
 * to be drawn, so dropping it here and dropping it there are the same thing.
 */
enum
{
    TORIRSSERVER_HITMARK_MAX = 4
};

struct ToriRSServerHitmark
{
    int damage;
    /**
     * A hitsplat *config* id (group 32) — 28 damage, 26 block. Not a style.
     *
     * What content states, and NOT necessarily what goes on the wire: the
     * encoder promotes a leaf to the wrapper that carries the viewer's own
     * settings. See `ToriRSServer_HitsplatForViewer`.
     */
    int type;
    /**
     * Which player slot dealt this, or -1 for damage nobody owns (poison, a
     * trap, an npc hitting an npc).
     *
     * The splat list lives on the entity and is encoded once per VIEWER, and
     * "was this my damage" is a fact about the pair, not about the hit. Without
     * it every hitsplat in the game is somebody's own — which is precisely the
     * state that made setting 5 ("hitsplats caused by damage that you did not
     * deal are tinted") impossible to honour.
     */
    int dealer_slot;
};

/* The npc mask is a single byte — there is no widening bit. */
enum
{
    TORIRSSERVER_NMASK_DAMAGE2 = 0x01,
    TORIRSSERVER_NMASK_ANIM = 0x02,
    TORIRSSERVER_NMASK_FACE_ENTITY = 0x04,
    TORIRSSERVER_NMASK_SAY = 0x08,
    TORIRSSERVER_NMASK_DAMAGE = 0x10,
    TORIRSSERVER_NMASK_CHANGE_TYPE = 0x20,
    TORIRSSERVER_NMASK_SPOTANIM = 0x40,
    TORIRSSERVER_NMASK_FACE_COORD = 0x80,
};

/* Appearance/equipment slot numbering. The cache's wearpos fields, the worn
 * container's slots, and the 12-entry appearance blob all index the same
 * space, so one enum serves all three. 6/8/11 hold body kits rather than
 * items; an item claims them through wearpos_2 / wearpos_3 to hide the kit
 * underneath (a full helm hides hair + jaw, a platebody hides arms). */
/*
 * Skills. The index is the protocol's: UPDATE_STAT carries it, the client's own
 * stat table uses it, and content/pack/stat.pack names it. Only the six combat
 * skills are used, but the array includes Sailing (23) and feature-flagged
 * Summoning (24) so every stat id the rev-239 client can store stays in range.
 */
enum
{
    TORIRSSERVER_STAT_ATTACK = 0,
    TORIRSSERVER_STAT_DEFENCE = 1,
    TORIRSSERVER_STAT_STRENGTH = 2,
    TORIRSSERVER_STAT_HITPOINTS = 3,
    TORIRSSERVER_STAT_RANGED = 4,
    TORIRSSERVER_STAT_PRAYER = 5,
    TORIRSSERVER_STAT_MAGIC = 6,
    TORIRSSERVER_STAT_AGILITY = 16,
    TORIRSSERVER_STAT_SAILING = 23,
    TORIRSSERVER_STAT_SUMMONING = 24,
    TORIRSSERVER_STAT_COUNT = 25,
};

/*
 * The experience a single skill may hold, in tenths — OldSchool's 200,000,000.
 *
 * A ceiling rather than an assumption: `stat_xp_tenths` is an `int`, and
 * 200,000,000 xp is 2,000,000,000 tenths, which sits within seven percent of
 * INT_MAX. An uncapped total therefore does not merely grow past the game's
 * limit, it wraps negative, and a negative total reads back through
 * `ToriRSServer_CombatLevelForXp` as level 1 — a maxed skill would silently
 * become an unskilled one. Clamped at every mutation, and at 0 on the way down
 * for the same reason from the other side.
 */
#define TORIRSSERVER_XP_MAX_TENTHS 2000000000

/*
 * The ceiling a script may raise an npc stat to.
 *
 * The reference's own, and engine rather than content: `NpcOps.ts:507`
 * (`NPC_STATADD`) ends `levels[stat] = Math.min(added, 255)`, and 255 is there
 * because a stat level is one byte on the wire, not because any npc is meant to
 * have that much of anything. Named rather than spelled at the two clamps so it
 * cannot be mistaken for a balance number.
 */
#define TORIRSSERVER_NPC_STAT_MAX 255

/*
 * How long a player keeps fighting with no input of their own.
 *
 * OldSchool's anti-AFK rule, stated on the wiki's Auto Retaliate page: the
 * character retaliates "for 20 minutes if no player input is given, after which
 * players stop attacking all together even if they are attacked by monsters".
 * 20 minutes is 1200 seconds and a tick is 600 ms, so 2000 ticks.
 *
 * Engine and not content, because the thing being measured is the *connection*:
 * `ToriRSServerPlayer::last_input_tick` is set by the inbound packet router and
 * nothing else can see it. A real click is unaffected — the packet that carries
 * it resets the clock before its own handler runs — so this only ever bites
 * combat that continues without the player, which is the whole point.
 */
#define TORIRSSERVER_AFK_COMBAT_TICKS 2000

/*
 * TORIRSSERVER_FAMILIAR_DEBUG=1 — one stderr line per tick per owned npc (mode,
 * waypoint, both combat targets, tile, face) plus every `npc_setmode` on one.
 *
 * A familiar's pursuit is a handshake between a script timer, a stored mode, an
 * engine combat target and a queued waypoint, and NONE of the four is visible
 * from the client: "my familiar just follows me and never attacks" is the same
 * observation whether the mode never left `playerfollow`, the target never
 * latched, the waypoint was queued into a mode that ignores waypoints, or the
 * facing was cleared every turn. Reading the four side by side is what
 * separated them.
 *
 * Cached rather than re-read: this sits in the per-tick npc loop.
 */
static inline int
ToriRSServer_FamiliarDebug(void)
{
    static int on = -1;

    if( on < 0 )
        on = getenv("TORIRSSERVER_FAMILIAR_DEBUG") != NULL;
    return on;
}

/*
 * How far from its OWNER a summoned familiar may be dragged by a fight before
 * the engine drops the target — the owner-relative replacement for the
 * spawn-anchored `maxrange` leash every world npc uses (`tile_within_maxrange`).
 *
 * Ten tiles because that is already content's number:
 * `~summoning_familiar_assist_allowed` (summoning_combat.rs2) refuses a victim
 * more than ten tiles from the owner. The two have to agree — a familiar the
 * engine still considers engaged while content has given up (or the reverse) is
 * a familiar that stands in a fight doing nothing, which is exactly the failure
 * this constant was added to end.
 */
#define TORIRSSERVER_FAMILIAR_LEASH 10

/** `ToriRSServerPlayer::last_input_tick` for a slot with no client behind it. Not a
 *  time, so it is outside every time: an armed clock may legitimately be a
 *  negative tick early in a world's life. */
#define TORIRSSERVER_INPUT_TICK_NEVER INT32_MIN

/*
 * Attack styles, in the order the combat interface lists them. OldSchool folds
 * the style into the effective level before the roll: accurate is +3 attack,
 * aggressive +3 strength, defensive +3 defence, controlled +1 to all three.
 */
enum ToriRSServerAttackStyle
{
    TORIRSSERVER_STYLE_ACCURATE = 0,
    TORIRSSERVER_STYLE_AGGRESSIVE = 1,
    /* The cache's order, not intuition's: DBTable 78's per-layout style rows
     * put controlled at 2 and defensive at 3 (`combat_interface_hacksword`:
     * 2,Lunge,(Controlled) / 3,Block,(Defensive); unarmed skips 2 entirely,
     * which is why its Block writes com_mode=3). These were transposed once
     * and every Block click trained shared XP. */
    TORIRSSERVER_STYLE_CONTROLLED = 2,
    TORIRSSERVER_STYLE_DEFENSIVE = 3,
};

enum ToriRSServerWearPos
{
    TORIRSSERVER_WEAR_HEAD = 0,
    TORIRSSERVER_WEAR_CAPE = 1,
    TORIRSSERVER_WEAR_AMULET = 2,
    TORIRSSERVER_WEAR_WEAPON = 3,
    TORIRSSERVER_WEAR_BODY = 4,
    TORIRSSERVER_WEAR_SHIELD = 5,
    TORIRSSERVER_WEAR_ARMS = 6,
    TORIRSSERVER_WEAR_LEGS = 7,
    TORIRSSERVER_WEAR_HAIR = 8,
    TORIRSSERVER_WEAR_HANDS = 9,
    TORIRSSERVER_WEAR_FEET = 10,
    TORIRSSERVER_WEAR_JAW = 11,
    TORIRSSERVER_WEAR_RING = 12,
    TORIRSSERVER_WEAR_AMMO = 13,
};

/* ------------------------------------------------------------------ */
/* Obj metadata (torirs_server_objinfo.c)                                    */
/* ------------------------------------------------------------------ */

struct ToriRSServerObjInfo
{
    const char* name;
    /**
     * The examine text — obj config opcode 3, `RSCache_Dat2ConfigObj.examine`,
     * `desc=` in the content tree's export.
     *
     * NULL when the record states none, which is what a placeholder or an
     * unnamed id looks like. It is the only string a rev-230 "Examine" op can
     * print, and it was decoded by the cache and dropped here until `oc_desc`
     * needed it: the config-query note in torirs_server_scripts.c said "the obj
     * record's examine text is read by nothing here", which was true, and is
     * why op 10 did nothing on every panel in the game.
     */
    const char* desc;
    /** Primary equipment slot, or -1 when the item cannot be worn. */
    int wearpos;
    /** Extra slots the item occupies: a 2h weapon's shield slot, the
     *  hair/jaw a full helm covers. -1 when unused. */
    int wearpos_2;
    int wearpos_3;
    int stackable;
    /** Grams, from the obj record's own weight field (config opcode 75). It is
     *  the only input to the run-energy drain rate, so an unarmoured player
     *  with an empty backpack really does run twice as far as a fully kitted
     *  one. Signed: weight-reducing items are negative. */
    int weight;
    /** Store value — obj config opcode 12. `oc_cost` returns this; high alch
     *  is content's `calc(oc_cost($obj) * 60 / 100)`. Default 1 when absent. */
    int cost;
    /** 1 when the item is members-only (config opcode 16). */
    int members;
    /** 1 when players may trade it (config opcode 15 clears; defaults true).
     *  Distinct from GE listing — `oc_tradeable` reads this. */
    int tradeable;
    /*
     * Bank notes ("certificates"), both directions.
     *
     * A *note* record carries `noted_template` — the shared template obj it
     * borrows its model from — and `noted_id`, the item it stands for. A plain
     * item carries neither, and nothing in the cache points from an item to its
     * note. So un-noting reads straight out of the record, and noting needs the
     * reverse index ToriRSServer_ObjInfoLoad builds by walking the whole table.
     *
     * All three are -1 when absent, which is also what "this obj has no note
     * form" means — the case a bank has to report rather than assume.
     */
    int noted_id;
    int noted_template;
    /** The note form of this item, or -1. */
    int cert_id;
    /*
     * Bank placeholders, and they are the note link's twin — opcodes 148/149
     * against 97/98, the same two fields with the same two meanings.
     *
     * A *placeholder* record carries `placeholder_template` (the shared
     * `template_for_placeholder` obj it borrows its shape from) and
     * `placeholder_id`, the item it stands for. A plain item carries only
     * `placeholder_id`, pointing forward at its placeholder. So both directions
     * read straight out of the record and neither needs an index:
     *
     *   1277  Bronze sword   placeholder 14730 / template     -1
     *  14730  "null"         placeholder  1277 / template  14401
     *
     * -1 for both when the cache gives an obj no placeholder, which is most of
     * them — a placeholder exists only for items a bank can hold.
     */
    int placeholder_id;
    int placeholder_template;
    /** 1 when the cache had a record for this id at all. Not the same as
     *  "has a name": every note record is named `null`, because a note takes
     *  its name from the item it stands for. Gating on the name — which is what
     *  this used to do — made every note in the game report as unknown, and an
     *  unknown obj is not stackable, so a stack of 20 notes came out as 1. */
    int known;
    /** Inventory menu ops (config opcodes 35-39) — "Wear", "Wield", "Drop",
     *  "Eat". OPHELD<n> carries the 1-based index into this array, so the
     *  server can act on the verb the player actually clicked. NULL = absent. */
    const char* if_ops[5];

    /*
     * Combat bonuses, straight out of the obj record's own param table.
     *
     * An OldSchool cache really does carry these: param ids 0..11 are the
     * twelve equipment bonuses in the order ToriRSServerCombatParam names them, 14
     * is the attack rate in ticks. Verified against cache.osrs230 — the bronze
     * scimitar (1321) reads slashattack +7, strengthbonus +6, attackrate 4.
     * This is why the mock can compute a real OldSchool max hit without a
     * hand-written bonus table for every item in the game.
     */
    int bonus[12];
    int attackrate;
    /** 0 stab, 1 slash, 2 crush — which bonus a swing with this weapon uses.
     *  Not in the cache; derived from which attack bonus is largest, which is
     *  right for every weapon whose class is unambiguous and harmless for the
     *  rest. A `param=damagetype,N` in a .obj config overrides it. */
    int damagetype;
    /** Weapon/equipment class from the cache record's own `category` field.
     *  It is what the combat interface's `weapon_category` varbit carries, and
     *  therefore what decides which of the ten button layouts the tab builds:
     *  bronze scimitar 21, abyssal whip 150, unarmed 0. */
    int category;
    /** 1 when the record carried any params at all. A tinderbox has none, and
     *  "no params" has to be distinguishable from "all bonuses zero" — the
     *  first is unarmed, the second is a weapon that happens to be bad. */
    int has_params;
};

/** Decode the whole obj config table once. Returns 0 when the cache is absent,
 *  in which case every lookup reports "not wearable" and the mock still runs. */
int
ToriRSServer_ObjInfoLoad(const char* cache_dir);

void
ToriRSServer_ObjInfoFree(void);

/** Never NULL: unknown ids report a placeholder name and wearpos -1. */
/**
 * One param off an obj record, as the cache stored it.
 *
 * `sval` is non-NULL exactly when the cache marked the entry a string, and that
 * is not the same question as what `configs/all.param` *declares* the param's
 * type to be. A caller pushing onto a typed stack must go by the declaration
 * (`ToriRSServer_ContentParamType`), because that is what the script was compiled
 * against; this struct says what is actually there. When they disagree the
 * record is wrong and saying so beats guessing which to believe.
 */
struct ToriRSServerObjParam
{
    int32_t obj_id;
    int32_t key;
    int32_t ival;
    char* sval;
};

/** The param, or NULL when this obj does not carry it. */
const struct ToriRSServerObjParam*
ToriRSServer_ObjParam(int obj_id, int param_id);

/**
 * Overlay one int param onto an obj (rank-1 `.obj` `param=<name>,<value>`).
 * Same contract as `ToriRSServer_LocInfoParamOverlay` — replaces any prior row
 * for (obj_id, param_id) and re-sorts so `oc_param` can find it.
 */
void
ToriRSServer_ObjInfoParamOverlay(int obj_id, int param_id, int value);

/**
 * Overlay an obj's category (rank-1 `.obj` `category=<name>`). Returns 0 when
 * `obj_id` is outside the decoded table, which is a content bug worth reporting.
 *
 * Why an obj needs this and an npc does not: an npc's category can be *stated*
 * in a `.npc` block because that grammar already carries the cache's own fields,
 * while `.obj` accepted `param=` and nothing else — `obj_config_key` answered
 * every other key with "is the cache's to state, ignored". So a grouping the
 * cache does not make was unauthorable, and only that kind: the tool ladders are
 * not the example, because this cache already groups all nine axes under 35 and
 * all eight pickaxes under 67. `gem_necklace` is — no record in cache.osrs239
 * carries a category for it, which is why `pack/category.pack` had to allocate
 * one (8212) from the server band, and an allocated id nothing can be assigned
 * to is a name with no members.
 *
 * The id space is `pack/category.pack`, shared with npc and loc; 0 is the
 * decoder's "unstated" and is refused rather than stored.
 */
int
ToriRSServer_ObjInfoCategoryOverlay(int obj_id, int category);

const struct ToriRSServerObjInfo*
ToriRSServer_ObjInfo(int obj_id);

/** How many obj records the cache decoded — the exclusive upper bound on a
 *  scan of the table. 0 before `ToriRSServer_ObjInfoLoad`, and 0 without a cache. */
int
ToriRSServer_ObjInfoCount(void);

/**
 * How many obj records carry this category id.
 *
 * The pack's side of the question. `pack/category.pack` is a *derived* table —
 * every line is a claim that this cache groups something under that id — and the
 * only way to check a claim like that is to count the group. Zero means a
 * trigger bound to the name can never fire, which is a content bug the tree can
 * see and a runtime cannot.
 */
int
ToriRSServer_ObjCategoryMembers(int category);

/* ------------------------------------------------------------------ */
/* Equipment requirements                                              */
/* ------------------------------------------------------------------ */

/*
 * The levels you need to wear something.
 *
 * A sparse table rather than a field on ToriRSServerObjInfo: about 1,100 of the
 * 33,747 objs have a requirement at all, so eight (stat, level) pairs on every
 * record would cost 2 MB to say "none" 32,000 times.
 *
 * Two sources feed it and neither is sufficient alone — see
 * docs/torirs_server_content.md §5:
 *
 *   - cache.osrs239's own params 434/436 and 435/437, read at decode time.
 *     Room for exactly two, and only trustworthy for a *combat* skill: the same
 *     pair states the requirement to *make* a record, so a fire battlestaff
 *     reads Crafting 62 and a wearable obj is not enough to tell the two apart.
 *   - `skill_combat/configs/equipment.obj`, which fills in the ~790 the cache is
 *     silent about and can state more than two.
 */
enum
{
    /* Seven is the observed maximum — Void knight gear, and the max capes as
     * Kronos records them. */
    TORIRSSERVER_OBJ_REQUIRE_MAX = 8,
};

struct ToriRSServerObjRequire
{
    int obj_id;
    int count;
    struct
    {
        int stat;
        int level;
    } req[TORIRSSERVER_OBJ_REQUIRE_MAX];
};

/** NULL when the obj has no requirement, which is the overwhelming majority. */
const struct ToriRSServerObjRequire*
ToriRSServer_ObjRequire(int obj_id);

/**
 * Replace an obj's requirements, for the `.obj` config overlay.
 *
 * Replace rather than merge: a config block that names an item is stating the
 * whole requirement for it, the same way an `.npc` block's `hitpoints=` is not
 * added to whatever the cache said. Returns 0 when the table is full or the id
 * is out of range.
 */
int
ToriRSServer_ObjRequireSet(
    int obj_id,
    const int* stats,
    const int* levels,
    int count);

/** How many objs carry a requirement, and how many came from the cache alone.
 *  For ToriRSServer_Pack's report. */
void
ToriRSServer_ObjRequireCounts(
    int* total,
    int* from_cache);

/* ------------------------------------------------------------------ */
/* NPC metadata (torirs_server_npcinfo.c)                                    */
/* ------------------------------------------------------------------ */

struct ToriRSServerNpcInfo
{
    /** Never NULL from ToriRSServer_NpcInfo(); unknown ids report a placeholder. */
    const char* name;
    int combat_level;
    int size;
    /**
     * NpcType.turnspeed (config opcode 103), default 32; 0 = never turns.
     *
     * A client field the server had no reason to read until it turned out the
     * server is what makes turning *happen*: the client only rotates toward a
     * FACE_ENTITY latch, and this end sets that latch every tick an npc is in
     * a player-facing mode. A record that says it does not turn has to be
     * honoured here too, or a fixture npc tracks the player on the wire and
     * only the baked cache stops it on screen.
     */
    int turnspeed;
    /** Menu ops (config opcodes 30-34). "Attack" in one of them is what makes
     *  an npc a valid combat target — the same test the client's minimenu
     *  makes, so the two ends agree without a second attackability list. */
    const char* ops[5];
    /**
     * Config opcode 18 (`dat2_config_npc.c:666`) — the record's own category
     * id, 0 when it states none. The middle rung of the trigger lookup
     * (`SSVM_ProviderGetByTrigger`: exact type, then category, then `_`).
     *
     * A raw cache id, deliberately: content names one through
     * `pack/category.pack` and compares, so no name is ever spelled in C.
     * 9,149 of cache.osrs239's 16,292 npc records carry one, 1,585 of those on
     * a record with no name.
     *
     * Two accessors read this field and each is right for its caller.
     * `ToriRSServer_NpcCategory()` states once the -1 that means "no category
     * rung", because 0 is the decoder's "unstated" *and* would be a legal id.
     * `ToriRSServer_NpcInfoRecord()` is the ungated row, for the callers where a
     * nameless multinpc instance still has to answer — `ToriRSServer_NpcInfo()`
     * gates on the record having a name, which is right for player-facing text
     * and wrong here.
     */
    int category;
    /** Same param table as the obj records, same indices. cache.osrs230's
     *  Goblin (3028) reads strengthbonus -15 and five defences of -15. */
    int bonus[12];
    int attackrate;
    int has_params;
};

/*
 * The category rung for an npc type, or -1 when there is none.
 *
 * Deliberately NOT read off `ToriRSServer_NpcInfo()`. That accessor gates on the
 * record having a *name* — a rule that exists for player-facing text and is
 * right for it — and 1,585 of this cache's 9,149 categorised npc records are
 * nameless (the multinpc instances are all of them). Going through it would
 * answer "no category" for every one of them, silently, which is the exact
 * failure mode the category rung exists to avoid. This reads the decoded row.
 *
 * Zero is the decoder's zeroed-record default and `pack/category.pack` never
 * names it, so 0 answers -1 here for the same reason `interaction_category`
 * does it for objs: binding a trigger to "unstated" would match every
 * uncategorised npc in the cache.
 *
 * **Six npc dispatch sites in torirs_server_world.c still pass a literal -1**, and
 * each is this call with the type they already hold. They were left alone
 * because another change owns those lines, not because the rung does not apply
 * to them:
 *
 *   SS_TRIGGER_AI_OPPLAYER1 + op    npc->type
 *   SS_TRIGGER_AI_APPLAYER1 + op    npc->type
 *   SS_TRIGGER_AI_QUEUE1 + n        npc->type
 *   SS_TRIGGER_AI_TIMER             npc->type
 *   SS_TRIGGER_AI_SPAWN             npc->type
 *   SS_TRIGGER_OPNPC1 + n           srv->npcs[slot].type   (the `::talk` cheat)
 *
 * AI_QUEUE3 was the seventh and has been adopted: it is where the reference
 * leans on categories hardest — 16 of `drop tables/`'s 94 `[ai_queue3]` triggers
 * bind to a category — so a -1 there is the difference between those 16 tables
 * existing and never firing. Measured both ways in `ToriRSServer_WorldNpcDied`.
 *
 * `[opnpc*]`/`[apnpc*]` from a real interaction already reach the rung, through
 * `interaction_category()`. Nothing needs to *guard* the remaining six: a
 * category of -1 and a category nothing binds behave identically, so adopting
 * them is additive and cannot change an existing dispatch.
 */
int
ToriRSServer_NpcCategory(int npc_id);

/** How many npc records carry this category id — see
 *  `ToriRSServer_ObjCategoryMembers`, same question one namespace over. */
int
ToriRSServer_NpcCategoryMembers(int category);

/**
 * One param off an npc record, as the cache stored it.
 *
 * Same shape and same rule as `struct ToriRSServerObjParam`: `sval` is non-NULL
 * exactly when the cache marked the entry a string, and that is a different
 * question from what `configs/all.param` *declares* the param to be. Go by the
 * declaration when choosing a stack, by this when reading the value.
 */
struct ToriRSServerNpcParam
{
    int32_t npc_id;
    int32_t key;
    int32_t ival;
    char* sval;
};

/** The param, or NULL when this npc does not carry it. */
const struct ToriRSServerNpcParam*
ToriRSServer_NpcParam(int npc_id, int param_id);

/** Decode the npc config table once. Returns 0 when the cache is absent, in
 *  which case every lookup reports a placeholder name and the mock still runs. */
int
ToriRSServer_NpcInfoLoad(const char* cache_dir);

void
ToriRSServer_NpcInfoFree(void);

const struct ToriRSServerNpcInfo*
ToriRSServer_NpcInfo(int npc_id);

/**
 * 1 when `ToriRSServer_NpcInfo(npc_id)` answers from the decoded table, 0 when it
 * would hand back the "Someone" placeholder.
 *
 * The accessor gates on the record having a *name*, so a nameless record — the
 * multinpc instances are all of them — hides its whole decoded row, params
 * included, from everything seeded through it. The server-band verifier needs
 * that fact out loud: a band value over such a record has nothing on this side
 * to be compared against, which is different from being wrong.
 */
int
ToriRSServer_NpcInfoKnown(int npc_id);

/** How many npc records the cache decoded — the exclusive upper bound on a scan
 *  of the table, the npc side of `ToriRSServer_ObjInfoCount`. 0 before
 *  `ToriRSServer_NpcInfoLoad`, and 0 without a cache. */
int
ToriRSServer_NpcInfoCount(void);

/**
 * The decoded row, or NULL — **without** the name gate `ToriRSServer_NpcInfo` puts
 * in front of it.
 *
 * The gated accessor exists so a name always renders as something, and that is
 * right for the player-facing text it was written for. It is wrong for reading
 * a *field*: 1,585 of the 9,149 cache.osrs239 npc records that carry a category
 * carry no name, and 177 records declare a menu op without one, so a gated read
 * would report "no category" and "no ops" for every one of them — data that is
 * there, hidden by a question nobody asked.
 *
 * Returns NULL when the id is out of range or the cache never loaded, so a
 * caller has one branch rather than a placeholder that looks like a record.
 */
const struct ToriRSServerNpcInfo*
ToriRSServer_NpcInfoRecord(int npc_id);

/* ------------------------------------------------------------------ */
/* loc and struct configs (torirs_server_locinfo.c, torirs_server_structinfo.c)    */
/* ------------------------------------------------------------------ */

/*
 * Two more config tables, for the `lc_*` / `loc_*` reads and `struct_param`.
 *
 * A `struct` record is a param map and nothing else, so struct exposes only
 * params. Loc exposes params plus the three fields the script host asks for by
 * id — name and footprint — because `lc_name`/`lc_width`/`lc_length` name a loc
 * that need not be anywhere near the scene. Everything else about a loc really
 * is the scene's business. The row type is the shared `struct ToriRSServerParamRow`
 * (torirs_server_paramtable.h) rather than a per-table clone of the same four fields.
 */
struct ToriRSServerParamRow;

/** The param, or NULL when this loc does not carry it. */
const struct ToriRSServerParamRow*
ToriRSServer_LocParam(int loc_id, int param_id);

/**
 * Give a loc a param from the *content overlay*, and re-sort.
 *
 * `torirs_server_content.c` calls this for every `param=` line in a `.loc` overlay
 * block under `server/scripts/`, once, after the whole tree is read. The cache's own
 * params are already in the table by then (`ToriRSServer_LocInfoLoad` runs first in
 * `torirs_server_boot.c`), so an overlay row overwrites a cache row for the same key,
 * which is the direction an overlay is defined to win in.
 *
 * The re-sort is here rather than at the call site because the table refuses a
 * lookup while unsorted, and "the overlay left it unsorted" is a failure that
 * would show up as every `loc_param` in the tree reporting "not set".
 */
void
ToriRSServer_LocInfoParamOverlay(int loc_id, int param_id, int value);

/**
 * The loc's display name, or NULL when the record carries none.
 *
 * Borrowed, and valid until `ToriRSServer_LocInfoFree`. `loc_name` / `lc_name` push
 * the reference's `'null'` in the NULL case; 32,161 of cache.osrs239's 62,194
 * records land there.
 */
const char*
ToriRSServer_LocName(int loc_id);

/**
 * The record's **unrotated** footprint. Writes 1x1 — the decoder's own default
 * — for an id with no override and for an id with no record, so a caller never
 * has to branch on whether the cache loaded.
 *
 * Not the same as `struct ToriRSServerSceneLoc`'s size_x/size_z, which have already
 * been rotated by the placed angle.
 */
void
ToriRSServer_LocFootprint(int loc_id, int* out_width, int* out_length);

/**
 * The loc's category id, or -1.
 *
 * One field, two sources, one accessor, in this order:
 *
 *   1. the authored overlay — a `.loc` block's `category=` under server/scripts,
 *      resolved through `pack/category.pack` (see `ToriRSServerLocDef.category`);
 *   2. the cache's own config opcode 61, on 8,407 of cache.osrs239's 62,194
 *      records;
 *   3. -1.
 *
 * npc needed only source 2 (`ToriRSServer_NpcCategory`) because its categories are
 * 100 % cache-sourced. loc cannot be: **not one of this cache's 776 door records
 * states a category**, and doors are exactly what the reference binds as one
 * (`[oploc1,_door_closed]`). Dropping either source loses half the domain.
 *
 * Ungated on the name for the same reason the npc accessor is: 32,161 of the
 * records carry no name, and those are the ones a group binding exists to reach.
 * 0 is never returned — it is the decoder's "unstated" and `pack/category.pack`
 * refuses to name it.
 */
int
ToriRSServer_LocCategory(int loc_id);

/** How many loc records carry this category id, counting the authored overlay
 *  and the cache — see `ToriRSServer_NpcCategoryMembers`, same question one
 *  namespace over, and `ToriRSServer_Pack`'s `validate_categories` is the caller. */
int
ToriRSServer_LocCategoryMembers(int category);

/**
 * Does the config group hold a record for this id? (The reference's
 * `LocTypeValid`.)
 *
 * Reports 0 for everything when the cache is absent, so a caller that wants to
 * degrade rather than abort tests `ToriRSServer_LocInfoCount()` first — see
 * `check_loc_id` in torirs_server_ops_loc.c.
 */
int
ToriRSServer_LocKnown(int loc_id);

/** The param, or NULL when this struct does not carry it. */
const struct ToriRSServerParamRow*
ToriRSServer_StructParam(int struct_id, int param_id);

/** Decode the loc / struct config groups once. Returns 0 when the cache is
 *  absent, in which case every lookup reports "not carried" and the server
 *  still runs — content then reads the param's declared default. */
int
ToriRSServer_LocInfoLoad(const char* cache_dir);

void
ToriRSServer_LocInfoFree(void);

int
ToriRSServer_StructInfoLoad(const char* cache_dir);

void
ToriRSServer_StructInfoFree(void);

/** Records decoded and rows retained, for the tests and the boot line. */
int
ToriRSServer_LocInfoCount(void);
int
ToriRSServer_LocInfoParamCount(void);
int
ToriRSServer_LocInfoNameCount(void);
int
ToriRSServer_LocInfoSizeCount(void);
int
ToriRSServer_LocInfoCategoryCount(void);
int
ToriRSServer_StructInfoCount(void);
int
ToriRSServer_StructInfoParamCount(void);

/* ------------------------------------------------------------------ */
/* Packet capture (selftest only)                                      */
/* ------------------------------------------------------------------ */

/*
 * Records every packet the tick produces, so the selftest can assert on what
 * actually went out rather than only on the state left behind.
 *
 * The hook sits at the top of ToriRSServer_Send, above its `fd < 0` early return —
 * every encoder has already built its payload by then, so all of them become
 * observable without a single encoder changing. Under the selftest there is no
 * cipher either, so the recorded opcode is the plain one.
 */

struct ToriRSServer;
struct SSVM_Provider;
struct SSVM_Env;
struct SSVM_State;

/*
 * A boolean server flag that is ON unless the environment turns it off —
 * `0`/`no`/`off`/`false` disable; anything else, including unset, leaves it
 * on. Shared by every server-construction site (torirs_server_main.c's two serve
 * loops, torirs_server_embed.c, the selftest's world in
 * torirs_server_world_selftest.c) so a flag like `members_world` defaults the
 * same way regardless of which host built the struct.
 */
int
ToriRSServer_FlagDefaultOn(const char* name);

int
ToriRSServer_SplitInit(
    struct SSVM_State* state,
    const char* text,
    int max_width,
    int lines_per_page,
    int font_id);
const char*
ToriRSServer_SplitGet(struct SSVM_State* state, int page, int line);
int
ToriRSServer_SplitPagecount(struct SSVM_State* state);
int
ToriRSServer_SplitLinecount(struct SSVM_State* state, int page);
struct SSVM_Script;
struct ToriRSServerNpcDef;

enum
{
    TORIRSSERVER_CAPTURE_MAX = 512,
    TORIRSSERVER_CAPTURE_BYTES = 1024,
};

struct ToriRSServerCapturedPacket
{
    int opcode;
    /**
     * The packet's CANONICAL name (`PKT_NAME_*`), beside the revision's number
     * for it.
     *
     * `opcode` is what went on the wire and is therefore a different number in
     * every revision this server can speak. The selftest's assertions are
     * written in rev-230 numbers — that is the contract the wire adapter's own
     * note states — so matching on them directly makes every one of them a
     * rev-230-only assertion, and running the suite at revision 239 turned ~190
     * of them red against a server that was behaving correctly.
     *
     * `ToriRSServer_CaptureFind` translates its rev-230 argument through this, so
     * the assertions keep their numbers and stop being about one revision.
     */
    int name;
    int len;
    uint8_t data[TORIRSSERVER_CAPTURE_BYTES];
};

struct ToriRSServerCapture
{
    struct ToriRSServerCapturedPacket packets[TORIRSSERVER_CAPTURE_MAX];
    int count;
    /** Set when a packet was dropped, so a test cannot silently assert against
     *  a truncated record. */
    int overflow;
};

void
ToriRSServer_CaptureBegin(
    struct ToriRSServer* srv,
    struct ToriRSServerCapture* capture);
void
ToriRSServer_CaptureEnd(struct ToriRSServer* srv);
void
ToriRSServer_CaptureReset(struct ToriRSServerCapture* capture);

/** Index of the next packet with this opcode at or after `from`, else -1. */
int
ToriRSServer_CaptureFind(
    const struct ToriRSServerCapture* capture,
    int opcode,
    int from);

/** The same search by canonical `PKT_NAME_*`, for an assertion that should mean
 *  the packet rather than one revision's number for it. */
int
ToriRSServer_CaptureFindNamed(
    const struct ToriRSServerCapture* capture,
    int pkt_name,
    int from);

/** True when `opcodes` all appear in order. Other packets may interleave. */
int
ToriRSServer_CaptureHasSequence(
    const struct ToriRSServerCapture* capture,
    const int* opcodes,
    int count);

/* ------------------------------------------------------------------ */
/* Game state                                                          */
/* ------------------------------------------------------------------ */

/** Per-slot keyed ints for `inv_setvar` / `inv_getvar` (LostCity commented
 *  signatures). Key is an obj id; empty keys are -1. Cleared when the slot is. */
enum
{
    TORIRSSERVER_ITEM_VAR_MAX = 4
};

struct ToriRSServerItem
{
    int obj_id; /* -1 = empty slot */
    int count;
    int32_t var_key[TORIRSSERVER_ITEM_VAR_MAX];
    int32_t var_val[TORIRSSERVER_ITEM_VAR_MAX];
};

/*
 * One registered container. The registry itself is torirs_server_container.h; the
 * struct is here because `struct ToriRSServerPlayer` embeds a table of them and
 * torirs_server_container.h cannot be included ahead of `struct ToriRSServerItem`.
 *
 * A row is what `container_for` used to be a `case` of. Everything that made
 * the three-case version wrong is a *field* here — where the items live, who
 * owns them, how "changed" is recorded, what the container paints into — so a
 * fourth container is a registration rather than a fourth branch.
 */
struct ToriRSServerContainer
{
    /** 0 for a free row. A zeroed player struct is therefore an empty table,
     *  which matters because `ToriRSServer_WorldPlayerInit` memsets one. */
    uint8_t used;
    /** TORIRSSERVER_CONTAINER_PLAYER / _WORLD. The whole reason resolve does not
     *  take `active_player`: a shared container has no player to ask. */
    uint8_t owner_kind;
    /** The registry calloc'd `items` and must free it. 0 for an adopted array
     *  belonging to the player struct or to ToriRSServer_Bank. */
    uint8_t owns_items;
    /** Dirty is a 32-bit per-slot mask rather than a whole-container flag.
     *  Decided at registration from `slots`, because UPDATE_INV_PARTIAL can
     *  only address 32 of them — 304 of the cache's 1026 invs are larger. */
    uint8_t per_slot;
    /** A write to this container changes how its owner looks, so it also sets
     *  TORIRSSERVER_PMASK_APPEARANCE. True of the worn container and nothing else. */
    uint8_t appearance;
    /** How many entries in `listeners` are live. */
    uint8_t listener_count;

    int32_t inv_id;
    int32_t slots;
    struct ToriRSServerItem* items;

    /** The player this row belongs to, for TORIRSSERVER_CONTAINER_PLAYER rows; NULL
     *  for a world row. Never a copy: `players[]` is a fixed array that is
     *  neither compacted nor moved (see its comment). */
    struct ToriRSServerPlayer* owner;

    /*
     * Dirty state, in one of two places.
     *
     * `*_ref` is set when the flag predates the registry and something outside
     * it still reads the original — `player->inv_dirty` feeds the appearance
     * path and two selftests, `bank.dirty` feeds ToriRSServer_BankFlush. NULL means
     * the row owns its own, which is the case for every container registered
     * from here on. Access goes through the accessors in torirs_server_container.h,
     * so no self-referential pointer is ever stored in the row.
     */
    uint32_t* slot_dirty_ref;
    int* dirty_ref;
    uint32_t slot_dirty_own;
    int dirty_own;

    /*
     * Components listening to this inv — LostCity's `Player.invListeners`,
     * scoped to the row. `inv_transmit` appends; `inv_stoptransmit` removes by
     * component. One inv may paint two panels at once (worn tab + equipment
     * stats); a single `component` field overwrote the login binding and left
     * unequips with no UPDATE_INV after the stats screen closed.
     *
     * `first_seen` is per listener: a new bind gets a full update immediately,
     * without forcing every other listener to re-send.
     *
     * `player` distinguishes viewers because a component id is local to one
     * client: `(300<<16)|16` (shopmain:items) is the same numeric id on every
     * client. That is true for world rows and for a private row exposed
     * read-only to another player, such as a Costume Room collection. The row's
     * `owner` answers who owns the items; this field answers who sees them.
     */
    struct
    {
        int32_t component;
        uint8_t first_seen;
        struct ToriRSServerPlayer* player;
    } listeners[TORIRSSERVER_CONTAINER_LISTENERS_MAX];
};

/*
 * One obj on the floor.
 *
 * A *spawn* (from the content tree's map squares) respawns after it is taken;
 * a drop does not, and expires instead. That is LostCity's distinction between
 * a static obj and a dynamic one, and it is the whole reason both fields exist.
 */
struct ToriRSServerGroundObj
{
    int active;
    int obj_id;
    int count;
    int x, z, level;
    /** Tick this obj vanishes; -1 for a spawn, which stays forever. */
    int despawn_tick;
    /** Tick a taken spawn comes back; -1 when not waiting. */
    int respawn_tick;
    /** 1 when this came from a map square rather than from a drop. */
    int is_spawn;
    /*
     * Bumped every time this slot becomes a *different* obj — a fresh drop
     * claiming it, or a taken spawn coming back.
     *
     * One reader: `ToriRSServer_WorldObjHandle`, which is how a running script
     * holds the obj it is acting on. The reference holds a direct `Obj`
     * reference, so an `obj_takeitem` resumed after a `p_delay` cannot take
     * somebody else's drop; here the handle is an index into a 256-slot array
     * that is reused, and an index alone would resolve to whatever landed in
     * the slot meanwhile. See torirs_server_ops_obj.c.
     */
    int generation;
    /** Player id allowed to see/take this drop during its private window, or -1. */
    int receiver_pid;
    /** Tick the private window ends and the drop becomes public, or -1. */
    int public_tick;
    /* `sent` was here, for the same reason `ToriRSServerNpc.tracked` was: whether a
     * client has been told is a fact about the client. The per-client answer is
     * `ToriRSServerPlayer.loaded_zones` now — see torirs_server_zone.h on why "does this
     * client hold that zone" replaced "has this client seen that obj". */

    /** Packed zone index **plus one** — 0 means "filed nowhere". Maintained by
     *  `ToriRSServer_ZoneSyncObjs`; see `refile` for why it is offset. */
    int zone_index;
};

/**
 * Which queue a `ToriRSServerQueued` belongs to.
 *
 * `PlayerQueueType` in the reference, minus SOFT, which the reference declares
 * and never uses — a kind nothing can put in the queue is a branch no test can
 * reach.
 *
 * NORMAL, LONG, WEAK and STRONG live in one array where the reference keeps
 * `queue` and `weakQueue` as two lists; the kind is what the drain splits on,
 * and the only observable difference between them is *when they are cleared* —
 * see `ToriRSServer_WorldCloseModal` (WEAK) and `ToriRSServer_ScriptsProcessQueues`
 * (STRONG).
 *
 * ENGINE is the exception and lives in `ToriRSServerPlayer.engine_queue`, because
 * the reference keeps it apart too and the separation is load-bearing rather
 * than stylistic: `clearqueue` must not be able to cancel a zone trigger, and
 * an engine entry's delay is forced to 0 rather than taken from content.
 */
enum ToriRSServerQueueKind
{
    TORIRSSERVER_QUEUE_NORMAL = 0,
    /** `longqueue` — like NORMAL, plus a logout action. */
    TORIRSSERVER_QUEUE_LONG,
    /** `weakqueue` — discarded whenever a modal closes. */
    TORIRSSERVER_QUEUE_WEAK,
    /** `strongqueue` — closes whatever modal is up before the drain, so its own
     *  entry passes the access check on the tick it is due. */
    TORIRSSERVER_QUEUE_STRONG,
    /** The zone family. Engine-produced, delay always 0, drained after the
     *  timers from its own array. Content cannot put one here. */
    TORIRSSERVER_QUEUE_ENGINE
};

/** A script waiting for its delay to run out. */
struct ToriRSServerQueued
{
    int active;
    int script_id;
    /** Ticks remaining. Decremented once per tick — unconditionally, as the
     *  reference does, so an entry that came due while the player was busy
     *  fires the moment access returns rather than restarting its wait. */
    int delay;
    /** The script's arguments. `queue` states one; `queue*` states as many as
     *  the target script declares, which is why this is an array — the
     *  reference's QUEUEVARARG passes a whole list and `[queue,combat_damage_player]
     *  (npc_uid $nid, int $damage)` is the shape that needs it. `arg` is
     *  `args[0]`, kept as a name because most callers state exactly one. */
    int32_t args[TORIRSSERVER_QUEUE_ARG_MAX];
    int argc;
    /** enum ToriRSServerQueueKind. */
    int kind;
    /** LONG only: `^accelerate` (0) means "run it early when the player logs
     *  out" rather than discarding it. Stored and not yet read — `phase_logouts`
     *  is empty (osrs230_mockserver.md §3.9). */
    int logout_action;
};

/** Whether a timer runs while the player is busy. */
enum ToriRSServerTimerType
{
    /** `settimer` — needs access, and runs *with* protected access. */
    TORIRSSERVER_TIMER_NORMAL = 0,
    /** `softtimer` — runs while busy, and without protected access. */
    TORIRSSERVER_TIMER_SOFT
};

/** A script that re-runs on an interval. */
struct ToriRSServerTimer
{
    int active;
    int script_id;
    int interval;
    /** The world tick the timer was last armed or fired at — **absolute**, not
     *  a countdown. That is what `gettimer` returns, so a relative counter here
     *  would make the opcode unimplementable rather than merely different. */
    int clock;
    /** enum ToriRSServerTimerType. */
    int type;
};

/** One queued tile of movement, in absolute tile coordinates. */
struct ToriRSServerStep
{
    int16_t x;
    int16_t z;
};

/* ------------------------------------------------------------------ */
/* Interactions                                                        */
/* ------------------------------------------------------------------ */

/*
 * What the player is trying to do to something, and how far away it still is.
 *
 * Before this existed, every op handler walked *and* acted in the same call —
 * `handle_opobj` queued a route to the tile and emptied the ground pile in the
 * same breath, with a comment admitting "the mock has no interaction model, so
 * the player arrives instantly in game terms". That is wrong in a way that is
 * invisible until it matters: a player could take an obj from across Lumbridge,
 * and no `[ap*]` trigger could ever fire, because "at range" was not a state
 * the server could be in.
 *
 * The model is LostCity's. An interaction is latched by the packet handler and
 * resolved in the player's phase, once per tick, until it completes or is
 * replaced:
 *
 *   - within `ap` range  -> run [apnpc<n>] / [aploc<n>] / [apobj<n>]. If content
 *                           bound one, that is the whole interaction and the
 *                           player never closes the distance. This is how
 *                           ranged and magic attacks, and "Talk-to" from two
 *                           tiles away, are expressed.
 *   - adjacent           -> run [opnpc<n>] / [oploc<n>] / [opobj<n>], then —
 *                           only for the kinds that still have one, a list that
 *                           shrinks (§3.18) — the engine's own verb handling if
 *                           nothing was bound. [opobj<n>] has none: a miss is
 *                           `Player.defaultOp`, the message and nothing else.
 *   - otherwise          -> keep walking, try again next tick.
 *
 * Resolution is also attempted immediately by the handler, so clicking a thing
 * you are already standing next to acts on the tick the click arrives rather
 * than the one after — which is both what the reference does and what stops
 * every existing test from having to learn about ticks.
 */
enum ToriRSServerInteractionKind
{
    TORIRSSERVER_INTERACT_NONE = 0,
    TORIRSSERVER_INTERACT_NPC,
    TORIRSSERVER_INTERACT_LOC,
    TORIRSSERVER_INTERACT_OBJ,
    /**
     * Another player — `[applayer<n>]` / `[opplayer<n>]`.
     *
     * Reached from `p_opplayer` (LostCity `PlayerOps`: `setInteraction(SCRIPT,
     * activePlayer2, APPLAYER1 + op)`), not from the wire: rev 230 assigns no
     * OPPLAYER opcode, so a click cannot start one at this revision. What makes
     * it worth having anyway is the AP rung — player-versus-player is the one
     * pairing whose line of sight became **symmetric** in 2019, and this is the
     * kind that selects it (`ToriRSServer_SceneApproachedPvp`).
     */
    TORIRSSERVER_INTERACT_PLAYER,
};

struct ToriRSServerInteraction
{
    enum ToriRSServerInteractionKind kind;
    /** 1-based op index, as the OP<thing><n> packet numbered it. */
    int op;

    /** Pathing-entity slot: the npc slot for TORIRSSERVER_INTERACT_NPC, the player
     *  slot for TORIRSSERVER_INTERACT_PLAYER. Revalidated every tick — an npc can die
     *  or have its slot reused, and a player can log out, while the mover is
     *  still walking over. */
    int npc_slot;
    /** The npc type / loc id / obj id / target player's pid this interaction was
     *  started against, so a slot that changed underneath is detected rather
     *  than acted on. */
    int target_id;
    /** Life/login/object generation captured with the slot-backed target.
     * Zero is reserved for target kinds without a reusable runtime slot. */
    uint32_t target_generation;

    /** South-west tile of the target, and its footprint. A 3x3 npc is reachable
     *  from further out than a 1x1 one, so the range test is against the
     *  rectangle rather than against a point. */
    int x, z, level;
    int size_x, size_z;

    /** The ap trigger has been tried and nothing was bound, so only the
     *  adjacent form is left. Without this the ap lookup runs every tick of a
     *  long walk. */
    int ap_tried;

    /**
     * How far away the `[ap*]` trigger fires, and whether the script asked.
     *
     * `p_aprange(N)` is content saying "I am not finished — call me again when
     * I am within N", and it is how a ranged action states its own reach: the
     * combat scripts open with it, so an attack from ten tiles resolves at the
     * weapon's range rather than by walking into melee. The reference resets
     * both to (10, false) on every `setInteraction`/`clearInteraction`
     * (`PathingEntity.ts:541`), which is what `TORIRSSERVER_AP_RANGE_DEFAULT` is.
     *
     * `ap_range_called` is what stops the interaction from being cleared after
     * the trigger ran: an ap script that called `p_aprange` did NOT act, so the
     * walk has to continue and the trigger has to be allowed to fire again.
     * Without it the first `p_aprange` looks like a completed interaction and
     * the attack silently never happens.
     */
    int ap_range;
    int ap_range_called;

    /**
     * "Use <this item> on it" rather than "do op <n> to it".
     *
     * A use-on click is an interaction like any other — it latches, it walks,
     * and it acts on arrival — but it resolves to a *different* trigger: the
     * `u` form, `[aplocu]`/`[oplocu]`, which carries no op number at all. So
     * this selects the trigger rather than changing what the walk does, and
     * `op` is meaningless while it is set.
     *
     * The item itself is NOT here. It is `last_useitem`/`last_useslot` on the
     * player, because that is how the script reads it and because the trigger's
     * subject is the *target*, not the item — see the header comment on
     * `ToriRSServer_ScriptsRunOpheldu`.
     */
    int use_on;

    /**
     * "Cast <this spell> at it" — the `t` form, `[apnpct]`/`[opnpct]`. The spell's
     * component uid, or 0 for an interaction that is not a cast.
     *
     * The spell *is* here, where `use_on`'s item deliberately is not, and the
     * difference is which one the trigger is keyed by. A use-on resolves against
     * the target (`[aplocu,door]`), so the item is only ever data the script
     * reads. A cast resolves against the **spell**: the reference writes
     * `[apnpct,magic:wind_strike]`, one script per spell, and never matches on
     * the npc at all. So this is the trigger's subject, and it has to travel with
     * the interaction rather than sit in a `last_*` latch that the walk could
     * outlive.
     *
     * `op` is meaningless while it is set, as with `use_on`.
     */
    int spell;
};

enum
{
    /**
     * How far away `[ap*]` triggers fire.
     *
     * LostCity lets a script declare its own with `.aprange`, which needs a
     * per-script field the compiler does not carry yet; until it does, this is
     * the reference's default for a script that does not say.
     */
    TORIRSSERVER_AP_RANGE_DEFAULT = 10,
    /*
     * LostCity's `npcmode` numbering, which the compiler already seeds as
     * builtin symbols (`SSC_SymbolsSeedBuiltins`). Restated here rather than
     * shared, because the compiler's table is the *language's* and this is the
     * engine's reading of it — the day a mode is added, one of the two moving
     * without the other should be a mismatch somebody notices, not a silent
     * renumber.
     */
    TORIRSSERVER_NPCMODE_NULL = -1,
    TORIRSSERVER_NPCMODE_NONE = 0,
    TORIRSSERVER_NPCMODE_WANDER = 1,
    TORIRSSERVER_NPCMODE_PATROL = 2,
    TORIRSSERVER_NPCMODE_PLAYERESCAPE = 3,
    TORIRSSERVER_NPCMODE_PLAYERFOLLOW = 4,
    TORIRSSERVER_NPCMODE_PLAYERFACE = 5,
    TORIRSSERVER_NPCMODE_PLAYERFACECLOSE = 6,
    TORIRSSERVER_NPCMODE_OPPLAYER1 = 7,
    TORIRSSERVER_NPCMODE_OPPLAYER5 = 11,
    TORIRSSERVER_NPCMODE_APPLAYER1 = 12,
    TORIRSSERVER_NPCMODE_APPLAYER5 = 16,
};

/** Index 6 of the client's turn-angle table {768,1024,1280,512,1536,256,0,1792}:
 *  angle 0, due south, which is the resting facing the game gives an npc. */
#define TORIRSSERVER_FACE_SOUTH 6

struct ToriRSServerNpc
{
    int active;
    /** Set the instant this npc is despawned (`ToriRSServer_WorldNpcFree`);
     *  cleared by `ToriRSServer_WorldNpcReap` once every player's NPC_INFO for
     *  this tick has already reported it gone. `npc_spawn`'s free-slot scan
     *  treats this exactly like `active` — a slot cannot be handed to a new
     *  npc while a client might still resolve it as the old one. See
     *  docs/torirs_server_npc_slot_reap.md. */
    uint8_t pending_free;
    /** Bumped whenever this pool slot becomes a different NPC. */
    uint16_t generation;
    int type;
    /**
     * The type this npc was stood up as — the reference's `Npc.origType`, and
     * the form a timed `npc_changetype` goes back to.
     *
     * `type` alone cannot answer that: it is the CURRENT form, so by the time
     * the timer fires the original is gone. Written once, at spawn, and
     * deliberately never by `npc_changetype` — a chain of transformations all
     * unwind to the same record, which is what content expects. The rock crab
     * is the clearest case: `horror_rockcrab_inactive` is a rock, and every
     * activation is "be a crab for 1000 ticks, then be a rock again".
     */
    int spawn_type;
    /**
     * Ticks left before this npc reverts to `spawn_type`, or 0 for "this form
     * is permanent".
     *
     * `npc_changetype(<type>, <duration>)` has always taken the duration —
     * engine.rs2 declares it and every caller passes one — but it used to be
     * popped and dropped, so a transformation the content meant to be temporary
     * lasted for the life of the npc. A shorn sheep never grew its wool back, a
     * rock crab never became a rock again, and a cured Mort'ton local stayed
     * cured. `^max_32bit_int` is content's way of saying "never", and a
     * countdown holds it without the overflow the loc revert table had to
     * learn about (see `loc_reverts`).
     */
    int changetype_delay;
    int x, z, level;
    int spawn_x, spawn_z, spawn_level;
    int wander_radius;
    /** The player this runtime npc belongs to. `owner_gen == 0` is unowned;
     *  the generation makes a reused pid fail closed instead of transferring
     *  a familiar to whoever logged into the vacated slot. */
    int owner_pid;
    uint32_t owner_gen;
    /**
     * The player a player-facing `mode` is being held against — the reference's
     * `PathingEntity.target`, narrowed to the only kind of target this mode
     * field can carry.
     *
     * `mode_target_gen == 0` is unbound, same convention as `owner_gen` and for
     * the same reason: pid 0 is a real player, so the generation is what makes
     * a recycled slot fail closed rather than silently re-aiming a standing
     * mode at whoever logged into it.
     *
     * Without this the mode machine asked `srv->active_player` whose turn it
     * was, in a phase where it is nobody's. With one player that reads
     * correctly by accident; with two, an npc mid-conversation with player A
     * measures its range against player B, decides the conversation partner
     * walked off, and resumes wandering away from the person reading its
     * dialogue.
     */
    int mode_target_pid;
    uint32_t mode_target_gen;
    /** Entity poison state. Source identity is generation-guarded so a
     * recycled player slot cannot receive credit for an old poison timer. */
    int poison_severity;
    int poison_clock;
    int poison_source_pid;
    uint32_t poison_source_gen;
    /** Index into the content roster (`ToriRSServer_ContentNpcSpawns`) when this
     *  npc is the world standing one of its spawns up, and -1 when content
     *  npc_added it. Only the first kind is retired when the window moves; an
     *  npc a script created is that script's to remove. */
    int static_spawn;
    /** The content block this npc was spawned from, or the engine defaults.
     *  Never NULL on an active npc; owned by torirs_server_content.c. */
    const struct ToriRSServerNpcDef* def;
    /** RSMod/xrsps parity: idle NPCs try to roam every 15-30 ticks. */
    int next_roam_tick;

    /** Packed zone index **plus one** — 0 means "filed nowhere". Maintained by
     *  `ToriRSServer_ZoneSyncNpcs`, which reconciles rather than hooks because an
     *  npc's tile is written from five places. See `refile` in torirs_server_zone.c.
     *
     *  Named `zone_filed` rather than `zone_index` since players joined the map:
     *  a player has both, they mean opposite things, and two fields with one
     *  name across two structs is how the wrong one gets used. */
    int zone_filed;

    /** Filled in by the tick, consumed by the encoder, then cleared. */
    int step_dir; /* -1 when the npc did not move this tick */
    /**
     * The SECOND tile of a two-tile tick — the npc half of the player's
     * `move_dirs[1]`, and `PathingEntity.runDir` in the reference.
     *
     * NPC_INFO's tracked section has always had the op for it (update type 2,
     * two 3-bit directions; `pkt_npc_info.c` decodes it and
     * `World_NpcPathPushStep(WORLD_PATHSTEP_RUN)` applies it), but nothing on
     * this side ever set it, because every npc mover took exactly one step.
     * `playerfollow` needs two: a follower that walks cannot keep up with an
     * owner who runs, and a summoning familiar left behind one tile per tick is
     * across the region by the time its owner stops.
     *
     * -1 when this tick was a single step or none. Only `playerfollow` fills it
     * — combat pursuit deliberately does not, because outrunning a monster is
     * the mechanic.
     */
    int run_dir;
    /**
     * Tiles this npc takes off its queued route per tick: 0 walks, 1 runs.
     *
     * `Npc.defaultMoveSpeed()` in the reference returns `MoveSpeed.WALK` and
     * nothing ever changes it, so every LostCity npc walks. OldSchool bosses
     * do not: the Pestilent Bloat walks above 60% health, RUNS between 40% and
     * 60%, and alternates below 40% on every attack made against it. Which
     * band means which speed is content's business (`tob_bloat.rs2`); this is
     * only the switch, written by `npc_setmovespeed`.
     *
     * Read by the noMode waypoint drain in `advance_npcs`, which is the mover
     * `npc_walk` feeds. The wander and combat movers ignore it deliberately:
     * a roaming npc has no reason to sprint, and outrunning a monster in
     * pursuit is the mechanic (see `run_dir` above).
     */
    int move_speed;
    /**
     * Which way the npc is FACING, in the same 0..7 space as `step_dir`
     * (0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE).
     *
     * Separate from `step_dir` because it outlives a tick: `step_dir` is -1
     * whenever the npc stood still, and an npc that stops does not turn back to
     * face where it started. This is the value NPC_INFO's low-resolution add
     * carries, and the client applies it only when the npc is NEW -- so it is
     * the orientation the npc is drawn with from the moment it enters view, and
     * nothing later corrects it.
     *
     * Seeded to TORIRSSERVER_FACE_SOUTH rather than 0, which is not a detail: the
     * client's turn-angle table is {768, 1024, 1280, 512, 1536, 256, 0, 1792},
     * so index 0 is NORTH-WEST and index 6 is the south the game treats as the
     * resting direction. Every npc spawned with 0 faces diagonally away.
     */
    int face_dir;
    /**
     * This tick moved the npc somewhere its steps cannot explain — the npc half
     * of `place_dirty`, and `PathingEntity.tele` in the reference.
     *
     * NPC_INFO's tracked section has four movement ops: nothing, one step, two
     * steps, remove. None of them can say "is now over there", so a teleport
     * *is* a remove, and the entering-view scan re-adds the npc at its new tile
     * in the same packet. Without it every client that already holds the npc
     * keeps drawing it where it was, forever: the server routes clicks to the
     * real tile, so the npc answers from somewhere other than where it is
     * standing. Patrol's stuck-teleport made Hans the usual victim.
     *
     * Set only by `ToriRSServer_WorldNpcTeleport`, read only by the encoder, and
     * cleared in phase 11 beside `masks` — for the same reason `masks` is
     * cleared there rather than in the encoder: every observer's NPC_INFO has to
     * have been written first, or whoever is encoded first consumes it.
     */
    int tele;
    int last_step_x, last_step_z; /* tile before this tick's step attempt; seed (x-1,z) on spawn */
    int follow_x, follow_z;       /* snapshot of target's last_step at top of turn */
    struct ToriRSServerStep waypoints[TORIRSSERVER_WAYPOINT_MAX];
    int waypoint_index; /* -1 idle; counts down like player */
    int stuck_counter;
    int size;      /* footprint; from npcinfo at spawn, default 1 */
    int turnspeed; /* NpcType.turnspeed; from npcinfo at spawn. 0 = never turns */
    int blockwalk; /* 0 none, 1 npc, 2 all, 3 player — from def */
    int blocksight; /* 0/1 — sets PROJ_BLOCK_ENTITY when moving */

    /** Which extended-info fields this npc has to send. Cleared in phase 11. */
    uint32_t masks;
    int face_entity;
    char say[80];
    int anim_id;
    int anim_delay;
    int spotanim_id;
    int spotanim_height_delay;
    int change_type;
    int face_x;
    int face_z;
    /** Every splat this npc takes this tick; see struct ToriRSServerHitmark. */
    struct ToriRSServerHitmark hitmarks[TORIRSSERVER_HITMARK_MAX];
    int hitmark_count;
    /** The first of them, mirrored for the single-slot classic writer and for
     *  the selftests that assert on one hit. Meaningless when count is 0. */
    int damage;
    int damage_type;
    int hitpoints;
    int max_hitpoints;

    /**
     * How much a script has drained each stat below the level the content block
     * authored — `npc_statsub` and `npc_statadd` write it, and nothing else.
     *
     * `npc_statadd` writes it *negative*, which is a boost above the authored
     * level: the reference keeps `baseLevels[]` untouched and lets `levels[]`
     * rise (`NpcOps.ts:507`), so a delta below zero is that same state said the
     * other way round. `npc_basestat` keeps answering the authored level either
     * way.
     *
     * A *delta* rather than a copy of the levels, because the authored level is
     * already the base and duplicating it would give an npc two answers to
     * "what is your defence". Zero for every stat on a fresh npc, which is what
     * the spawn memset makes it and which is why an npc nothing has drained
     * reads exactly as it did before this existed.
     *
     * Hitpoints are **not** in here even though they are a stat: they already
     * have `hitpoints` / `base_hitpoints`, which is where every hit lands, and a
     * second place to record hitpoint loss is a second place to forget. The
     * `npc_stat*` opcodes route the hitpoints index to that pair instead.
     */
    int stat_drain[TORIRSSERVER_STAT_COUNT];

    /**
     * Small, script-owned runtime state belonging to this NPC instance.
     *
     * Player varps cannot represent two demons protecting from different
     * styles, and config params belong to the NPC type rather than to one live
     * instance. These slots fill that gap. They are zeroed by the spawn memset
     * and explicitly on respawn, but deliberately survive `npc_changetype` so
     * a phased boss does not lose its encounter state when its visible form
     * changes. Content assigns meanings with named slot constants.
     */
    int32_t script_vars[TORIRSSERVER_NPC_VAR_MAX];

    /**
     * Whether this npc hunts, as `npc_sethuntmode` last left it — seeded from
     * `def->huntmode` at spawn.
     *
     * Per-npc rather than read off `def` because content turns aggression on and
     * off for one npc at a time: the reference's chompy bird is spawned docile
     * and given a hunt mode when it notices you, and a gnome baller has its
     * cleared so it stops chasing during a match. Both are the same npc type as
     * their calm counterparts, so a def field cannot express it.
     *
     * Seeded *explicitly* at spawn and not left to the memset, because 0 is
     * `TORIRSSERVER_HUNT_NONE`: taking the default would quietly make every
     * aggressive npc in the world passive.
     */
    int huntmode;

    /* `tracked` was here — one flag saying "the client knows about this npc",
     * which with two clients is two different answers. It is
     * `ToriRSServerPlayer.npc_tracked[slot]` now, and the sites that used to clear it
     * on despawn/respawn/death no longer need to: the encoder derives the whole
     * set from `active` and range every tick, per player, so an npc that goes
     * away is removed from each client's list on the tick it goes away and
     * re-added as new when it comes back. */

    /** A script parked on this npc by npc_delay, resumed by phase 4. */
    struct SSVM_State* active_script;
    /** Tick at which the npc stops being delayed. */
    int delayed_until;

    /**
     * The tick this npc last changed tile, **plus one** — LostCity
     * `PathingEntity.lastMovement`, written by `Npc.processMovement` as
     * `World.currentTick + 1` whenever `lastTickX/Z` differ from `x/z`.
     *
     * The `+ 1` is not a quirk to normalise away: `npc_arrivedelay` reads this
     * against `srv->tick - 1` and `srv->tick`, so removing the offset shifts
     * both of its arms by a tick. Kept in the reference's own units.
     *
     * Written once per tick in `phase_cleanup`, off `step_dir`, rather than at
     * the five `npc_take_step` call sites: the phase sees the tick's final
     * answer no matter which mover produced it (wander, go-home, combat), and
     * `npc_take_step` is handed an npc and not the server — the same reason
     * `frozen_ticks` is a countdown.
     */
    int last_movement;

    /**
     * Tick at which this npc stops being frozen — `npc_freeze(ticks)`.
     *
     * A freeze stops *movement* and nothing else: a frozen npc still attacks
     * anything already in reach, still retaliates and still runs its queues.
     * That is what OldSchool's Ice spells do, and it is why this is not
     * `delayed_until`, which gates the queue drain and would silently make a
     * frozen npc stop fighting too.
     *
     * Gated in `npc_take_step` rather than at each caller: wandering, going
     * home and closing on a target are three call sites of one step, and a
     * freeze that only covered some of them is the bug this field exists to
     * make impossible.
     *
     * A countdown of ticks remaining, not an absolute expiry tick, because
     * `npc_take_step` is handed an npc and not the server — an absolute clock
     * would mean threading `srv` through five call sites to answer "what tick
     * is it". Decremented once per npc phase, next to the delay it sits beside.
     */
    int frozen_ticks;

    /**
     * What this npc is *doing*, from `npc_setmode` — LostCity's `npcmode`
     * numbering, which `SSC_SymbolsSeedBuiltins` already knows:
     * -1 null, 0 none, 1 wander, 3 playerescape, 4 playerfollow,
     * 5 playerface, 6 playerfaceclose, 7..11 opplayer1..5,
     * 12..16 applayer1..5.
     *
     * The default is *wander* for an npc with a radius and *none* otherwise,
     * not 0 for everybody. That is what keeps `npc_setmode(none)` meaningful:
     * if roaming were the absence of a mode, "stop" would be indistinguishable
     * from "never had one" and every roster npc would carry on roaming through
     * it.
     */
    int mode;

    /** Tick this npc disappears at, or -1 to stay. Set by `npc_add` with a
     *  duration; map-square spawns never carry one. */
    int despawn_tick;

    /**
     * Whether this npc's death is the end of it — the reference's
     * `EntityLifeCycle`, as a boolean because there are exactly two.
     *
     * 0 is `RESPAWN`: the npc is the *world's*, and the world stands it back up
     * at its spawn tile `respawnrate` ticks after it dies (`Npc.turn` ->
     * `World.addNpc`). Every map-square roster spawn is this, and so is every
     * fixture the engine itself creates, because a fixture stands in for one.
     *
     * 1 is `DESPAWN`: the npc belongs to the script that called `npc_add`, and
     * `World.removeNpc` retires it outright. Set at that one opcode and nowhere
     * else, which is the whole of the distinction — a memset slot is the world's
     * by default and a script has to say otherwise.
     *
     * The engine used to respawn both. That is invisible for the ordinary case
     * (content npc_adds a monster, you kill it, nothing looks at that tile
     * again) and wrong for every encounter that spawns its own roster: in the
     * Inferno each killed add stood back up 25 ticks later — "the waves are
     * spawning too fast" — and stood back up inert, because what points an add
     * at the Ancestral Glyph is the timer armed at the `npc_add` site, and a
     * respawn re-runs `[ai_spawn]`, not the spawner.
     *
     * `npc_setrespawn` outranks it either way: content asking for a respawn is
     * content asking for a respawn.
     */
    int despawns_on_death;

    /**
     * `npc_queue`: an `[ai_queue<n>]` waiting to fire on this npc.
     *
     * Four, not sixteen like the player's: the player's queue absorbs a whole
     * session of interactions, an npc's holds the two or three steps of one
     * behaviour. Overflow is reported rather than dropped silently.
     */
    struct
    {
        int active;
        /** 1..20 — the `n` in `[ai_queue<n>]`. */
        int queue;
        int delay;
        int arg;
    } queue[TORIRSSERVER_NPC_QUEUE_MAX];

    /** [ai_timer]: re-runs every `timer_interval` ticks, 0 = stopped. Armed by
     *  `npc_settimer` and drained in phase 4. The script is *not* stored: the
     *  trigger is resolved by npc type when it fires, so an npc that changed
     *  type picks up the new type's `[ai_timer]`. (There used to be a
     *  `timer_script` here as well, written in exactly one place and only ever
     *  to -1, which made the second drain that read it unreachable.) */
    int timer_interval;
    int timer_clock;
    /** Which waypoint of `def->patrol` this npc is walking to, and how many
     *  ticks it still owes the one it just reached. */
    int patrol_index;
    int patrol_pause;
    /**
     * `[ai_spawn]` has not run for this npc yet.
     *
     * Set at spawn and cleared by phase 3, which is the tick *after* the one
     * that created it — the reference's ordering, and the reason phase 3 exists
     * as a named phase of its own. Running the trigger inline at spawn instead
     * would let an `[ai_spawn]` observe a half-built world: the roster is
     * created in one loop at login, so the second npc would not exist yet when
     * the first one's script asked about it.
     */
    int spawn_pending;

    /* Combat. `hitpoints` / `max_hitpoints` are shared with the DAMAGE mask,
     * which carries the health bar the client draws above the hitsplat. */
    int base_hitpoints;
    /** Pool index (pid) of the player this npc is fighting, -1 when it is not.
     *  It was "0 = the player" while there was one, which is the same number
     *  and a different meaning — read `ToriRSServer_CombatNpcTick` for how a
     *  logged-out target is answered. */
    int combat_target;
    /**
     * The npc this npc is fighting, as a pool slot; -1 when it is not.
     *
     * The other half of a combat target, and the reason it is a second field
     * rather than a tagged one: a player pid and an npc slot are both small
     * non-negative integers, so one field carrying either would read correctly
     * in every expression and mean the wrong entity in half of them. At most one
     * of the two is ever set — `ToriRSServer_CombatStopNpc` clears both, and a
     * player's hit takes the target over from an npc
     * (`ToriRSServer_CombatHitNpc`).
     *
     * `combat_target_npc_gen` is the target's `generation` at the moment it was
     * taken. Slots are recycled, so without it a target that dies and is
     * replaced by an unrelated spawn keeps being attacked by whatever was
     * fighting the first one.
     */
    int combat_target_npc;
    uint16_t combat_target_npc_gen;
    /**
     * The tick this npc's next swing is due on — a DEADLINE in `srv->tick`,
     * not a countdown of ticks remaining.
     *
     * It is the reference's `%npc_action_delay` and it is spelled the same way:
     * `%npc_action_delay = add(map_clock, npc_param(attackrate))` to arm it and
     * `if (%npc_action_delay > map_clock) return;` to read it (LostCity
     * skill_combat/scripts/npc/npc_combat_melee.rs2 and the ~60 monster scripts
     * that gate on it). So `attack_clock = srv->tick + attackrate` and
     * `if (srv->tick < npc->attack_clock) return;`, which is also how
     * `poison_clock` and `death_tick` are written two fields down.
     *
     * **It was a countdown and it was off by one.** `if (clock > 0) { clock--;
     * return; }` spends `attackrate` whole ticks and only then lets the tick
     * *after* them swing, so every npc in the game attacked every
     * `attackrate + 1` ticks: measured on the Inferno's Jad, a record saying 8
     * produced swings 9 apart and the melee branch's 4 produced 5. The bug is
     * invisible in isolation — nothing looks wrong about a monster hitting you
     * — and it silently made every attack speed in the tree a lie, including
     * the wiki-sourced ones content had just been corrected to.
     *
     * A deadline rather than a fixed countdown because the ±1 has to live in
     * one place or it lives in all of them: `npc_attackdelay`, the flinch and
     * every `= 0` would each need their own correction, and the one that got
     * it wrong would be a second silent off-by-one.
     *
     * Zero means "no deadline pending", i.e. swing at the first opportunity —
     * tick 0 is always in the past, so the existing `= 0` sites
     * (`npc_attackplayer`, `npc_attacknpc`, `maybe_aggress`,
     * `ToriRSServer_CombatStopNpc`, respawn) keep meaning exactly what they meant.
     */
    int attack_clock;
    /**
     * When the next step of the death sequence is due; -1 while alive.
     *
     * `death_tick >= 0` is the engine's "this npc is dying" gate and is read
     * that way everywhere — hitting it again, engaging it, walking it, picking
     * it out of a click, roaming. That meaning is unchanged. What it no longer
     * means is "the death animation started": a death is four ticks and three
     * waits, and `death_stage` says which wait this deadline belongs to.
     *
     * Set once, at the killing blow, so every gate closes on the tick the
     * hitpoints reach zero rather than a tick later.
     */
    int death_tick;
    /** Which step of the death `death_tick` is counting down to —
     *  `TORIRSSERVER_DEATH_*`. Only meaningful while `death_tick >= 0`; the killing
     *  blow writes both, so the sites that end a death by clearing `death_tick`
     *  alone stay correct. */
    int death_stage;
    /**
     * Who was fighting this npc when it died, by pool slot.
     *
     * The drop table runs on `npc_del`'s tick, three or more ticks after the
     * blow, and by then `combat_target` has been cleared on both sides — so the
     * killers have to be captured while they are still known. Copied into
     * `ToriRSServer.loot_credit_players` around the `[ai_queue3]` run, which is
     * what makes each `obj_add` name its earner to clientscript 7192.
     */
    unsigned char death_credit_players[TORIRSSERVER_PLAYER_MAX];
    /**
     * Who has damaged this npc AT ALL since it spawned, by pool slot.
     *
     * `death_credit_players` beside it is a different fact and cannot serve:
     * that one is filled at the killing blow and answers "who earns the drop".
     * This accumulates from the first hit and answers "has anybody else touched
     * this", which is what the ironman loot restriction turns on -- a question
     * that has to be answerable WHILE the fight is going on, since the whole
     * point of settings 182 and 183 is to warn before the kill rather than
     * after it.
     *
     * Cleared on spawn and on respawn, not on death: a corpse that another
     * player damaged is still a corpse they damaged.
     */
    unsigned char damaged_by_players[TORIRSSERVER_PLAYER_MAX];
    /** Who has already been told this npc's loot is restricted to them (setting
     *  183). The row's word is "occasionally"; once per npc per player is what
     *  that has to mean on a path that runs for every landed hit. Cleared with
     *  `damaged_by_players`. */
    unsigned char noloot_warned_players[TORIRSSERVER_PLAYER_MAX];
    /** Who has already been SHOWN the restriction icon (setting 182). Separate
     *  from the message latch beside it because 182 and 183 are independent
     *  rows: a player may want the icon and not the chat line. */
    unsigned char noloot_iconned_players[TORIRSSERVER_PLAYER_MAX];
    /** Stable tracker identity for a death script that parks on npc_delay. */
    int loot_credit_event_id;
    int loot_credit_npc_type;
    /** Tick to respawn at the spawn tile; -1 when not waiting. */
    int respawn_tick;
    /** Resolved from the cache's sequence names at spawn; -1 = play nothing. */
    int attack_seq;
    int block_seq;
    int death_seq;
    /**
     * 1 once `death_seq` has been played on this life, by anyone.
     *
     * A death animation goes out ONCE per life. The client does not restart a
     * seq it is already on unless the seq's own replyMode says so (rev-239
     * readExtendedInfo; death seqs are mode 2 by default), so a second send
     * while the corpse is parked on the last frame changes nothing on screen
     * — and a corpse whose pose never moves reads as "it just disappeared".
     * Two senders can collide: a script's `npc_anim(<death>)` (a Matomenos
     * absorbed at the Maiden, a red at Verzik) and `npc_death_step` ARRIVE on
     * a hit that lands inside the scripted death. The engine owns the rule
     * rather than every script remembering it. Cleared with the life.
     */
    int death_seq_sent;
    /**
     * The tick `death_seq` was played on, or -1. Paired with `death_seq_sent`
     * so a removal can be held until the corpse has actually outlived its own
     * death animation — see `ToriRSServer_WorldNpcFree`.
     */
    int death_seq_tick;
    /**
     * Non-zero while a SCRIPT has just played `death_seq` on an npc that was
     * alive and not dying — see `torirs_server_world.c`'s phase cleanup. Such a
     * script has one tick to make the npc actually dead; still standing on the
     * next tick is the precondition for a double-booked death.
     */
    int scripted_death_pending;
    /**
     * 1 while a removal is being held back for one phase so the animation
     * queued on this npc THIS tick can reach the wire first.
     *
     * `ToriRSServer_WorldNpcFree` clears `active` immediately, and the npc
     * encoders read `active` to decide keep-vs-remove — so a free issued in
     * the npc phase, on the same tick a script played the npc's death
     * animation, sends the client a REMOVE and never the animation. The mask
     * is set, the seq is right, nothing errors, and the player watches the
     * monster blink out instead of die. Deferring to phase cleanup (which runs
     * after every observer's NPC_INFO has gone out) costs one tick of corpse
     * and makes "a death is always seen" a property of the engine rather than
     * of every script's arithmetic.
     */
    int free_deferred_for_anim;
    /** A removal that was asked for and held back (for the animation, or for
     *  the corpse's death-animation window). Phase cleanup retries it every
     *  tick until it goes through, so a held free is never a lost one. */
    int free_wanted;
    /**
     * The sound that goes with each of the three, as a synth id; -1 = silent.
     *
     * Sound rides animation: an npc's flinch noise and its flinch animation are
     * one event, so they are one field pair set from one record and played on
     * one line. Identified per npc by `tools/gen_npc_combat.py` and documented
     * in docs/DEATH_ATK_DEF_ANIMS.md.
     *
     * -1 and not 0. Sound effect 0 is a real clip, so silence cannot be spelled
     * as zero — the mistake that made all 1,083 weapons play the same noise on
     * every swing (docs/WEAPON_FX.md 6.6). Most npcs are legitimately -1: no
     * public source describes npc combat sound for a modern cache.
     */
    int attack_sound;
    int block_sound;
    int death_sound;
};

/*
 * Point an npc's FACE_ENTITY latch at a player, by pid.
 *
 * The single seam every "the npc turns to face somebody" site goes through, and
 * the reason it exists is that all five of them already *had* the pid — from
 * `npc->combat_target`, from a `player` in scope, from `srv->active_player` —
 * and threw it away in favour of a constant that meant "whoever is reading
 * this". NPC_INFO is one encode read by every observer, so a value that depends
 * on the reader is a value that is wrong for all but one of them.
 *
 * Mirrors `PathingEntity.setFaceEntity()`: the absolute pool slot plus 32768,
 * and the mask set only when the latch actually changes — the mask is per-tick,
 * and re-sending an unchanged latch is pure wire noise.
 */
static inline void
ToriRSServer_NpcFacePlayer(
    struct ToriRSServerNpc* npc,
    int pid)
{
    int id = TORIRSSERVER_FACE_PLAYER_BASE + pid;

    /* No target, nothing to face. A `retaliate=no` npc reaches the combat
     * facing site with `combat_target` still -1, and `BASE + -1` is a latch
     * naming a player that does not exist. */
    if( pid < 0 )
        return;

    /* `turnspeed = 0` means the record never turns, so there is nothing for a
     * facing latch to do. Gated at the seam rather than at the five callers:
     * every one of them is some flavour of "a mode is holding this player as a
     * target", which stays true of a fixture npc, and the answer to all five
     * is the same. Without it the latch still goes out every tick and the only
     * thing stopping the rotation is the client's own copy of this field. */
    if( npc->turnspeed == 0 )
        return;
    if( npc->face_entity == id )
        return;
    npc->face_entity = id;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
}

/*
 * Point an npc's FACE_ENTITY latch at another NPC, by slot — or drop it.
 *
 * The other half of the id space `ToriRSServer_NpcFacePlayer` writes: below
 * `TORIRSSERVER_FACE_PLAYER_BASE` the id is an npc's own pool slot, which is how
 * `PathingEntity.setFaceEntity` has always encoded it and what the client
 * already decodes. Nothing here wrote it until npc-versus-npc combat existed,
 * because until then every facing decision in this server named a player.
 *
 * A monster whose target is another monster is the case it exists for — the
 * Inferno's adds and the Ancestral Glyph, which walks its row while they shoot
 * at it. A tile-facing (`npc_facesquare`) can only name where the glyph was
 * when the script ran, so shooter and shot disagreed by however far it had
 * moved since; the latch tracks it for free.
 */
static inline void
ToriRSServer_NpcFaceNpc(
    struct ToriRSServerNpc* npc,
    int slot)
{
    assert(npc);
    if( npc->turnspeed == 0 ) /* see ToriRSServer_NpcFacePlayer */
        return;
    if( slot < 0 )
        slot = -1;
    if( npc->face_entity == slot )
        return;
    npc->face_entity = slot;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
}

/*
 * LostCity setFaceEntity else-branch for npcs: drop the latch when neither
 * combat nor a player-facing mode is holding a target. Talk-to and similar
 * one-shot faces then clear on the next npc turn instead of sticking forever.
 */
static inline void
ToriRSServer_NpcFaceClearIfIdle(struct ToriRSServerNpc* npc)
{
    assert(npc);
    /* Either kind of combat target holds the latch — the npc one for the same
     * reason as the player one, and `ToriRSServer_CombatStopNpc` is what drops it
     * when the fight ends. */
    if( npc->combat_target >= 0 || npc->combat_target_npc >= 0 )
        return;
    if( npc->mode >= TORIRSSERVER_NPCMODE_PLAYERESCAPE )
        return;
    if( npc->face_entity == -1 )
        return;
    npc->face_entity = -1;
    npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
}

/*
 * ── Two zone maps, and the difference between them ───────────────────
 *
 * `ToriRSServerZoneMap` (torirs_server_zone.c) is the WORLD's, and it is the authority:
 * every npc, obj and player in the game is filed in it, keyed by packed zone
 * index. Nothing about it is sized to what one client can see, and nothing
 * should be — a roster of 23,139 npcs is a fact about the world, not about
 * anybody's screen.
 *
 * `ToriRSServerPlayerZoneMap` is one CLIENT's, and it is a *subscription*: the zones
 * that client is being kept up to date on, plus what it has already been told
 * about each. It holds no entities. Asked who is standing nearby, it walks its
 * own zone list against the world map and answers from the authority — so the
 * answer cannot be stale, which a materialised copy maintained by push could
 * be, silently.
 *
 * The split is what makes the two limits independent. The world's size is a
 * memory question; what reaches a client is bounded by a 7x7 zone window and by
 * the stream's own TORIRSSERVER_TRACKED_NPC_MAX, and neither of those grows when the
 * world does.
 *
 * Before this existed there was no per-client structure at all. A client's view
 * was four flat arrays and each of the three area packets re-derived its own
 * candidates — a pool walk, a tile box, a zone window — which disagreed at the
 * build area's edge, where only the zone window is clipped.
 */

/** One zone as a client sees it. */
struct ToriRSServerPlayerZone
{
    /** Packed zone index — the same key the world map is keyed by, so a client
     *  zone and a world zone are always talking about the same 8x8 tiles. */
    int index;
    /**
     * 1 once this client has been sent the zone's full state.
     *
     * On the entry rather than in a second parallel array, which is what it
     * used to be. Two arrays that have to be added to and removed from together
     * are two arrays that can disagree, and the disagreement here is
     * invisible: a zone marked loaded but no longer subscribed is a zone the
     * client is never re-told about and never updated on.
     */
    uint8_t loaded;
};

/*
 * World npc slot <-> this client's npc slot.
 *
 * NPC_INFO's low-resolution ADD names an npc by a 16-bit index, and the client
 * keys a map on it (RuneLite deob rev 239, `Statics.java`: `byte var20 = 16;`
 * … `map.get(var22)`). Nothing in the client requires that index to mean
 * anything globally — it only has to be stable for as long as THIS client is
 * being told about the npc, and unique among the npcs it currently holds.
 *
 * So it is a per-client name, and translating at the wire is what stops the
 * world's pool size from being a protocol question. The server keeps its own
 * slot everywhere — the pool, the ZoneMap, every interaction — and only the two
 * places that write an id on the wire go through here.
 *
 * The alternative, and what this replaces, was reusing the world slot as the
 * client's index. That works and costs nothing right up until the world pool
 * outgrows the field, at which point two npcs share an id and the client draws
 * one of them in both places, silently.
 */
struct ToriRSServerPlayerSlotMap
{
    /** Client slot -> world npc slot, -1 when the client slot is free. */
    int16_t world_of[TORIRSSERVER_CLIENT_NPC_SLOTS];
    /** World npc slot -> client slot, -1 when this client has no name for it. */
    int16_t client_of[TORIRSSERVER_NPC_MAX];
    /** Rotating allocation hint. Rotating rather than lowest-free on purpose:
     *  reusing a just-released slot for a different npc inside one packet is
     *  the one ordering the client cannot absorb, and spreading allocation
     *  makes that vanishingly rare instead of the common case. */
    int next;
};

struct ToriRSServerPlayerZoneMap
{
    struct ToriRSServerPlayerZone zones[TORIRSSERVER_ZONE_ACTIVE_MAX];
    int count;

    /**
     * The build-area origin this window was clipped against.
     *
     * The window is a function of TWO things — the player's zone and the build
     * area it is clipped to — and only the first is obvious. Re-centring the
     * scene moves the clip under a standing player, so a move that only watched
     * `zone_index` left the subscription describing a build area that no longer
     * exists. Every path that re-centres happens to call
     * `ToriRSServer_ZonePlayerReset`, which forced a recompute by accident; this
     * makes the dependency the thing that is checked.
     */
    int built_zone_x, built_zone_z;
};

struct ToriRSServerPlayer
{
    /**
     * Has a v5 PLAYER_INFO gone out since the init block?
     *
     * Selects which low-resolution section the untracked crowd is skipped in:
     * section 4 on the first tick, section 3 thereafter, because the client
     * sets a cycle bit on everyone it skips. Per player rather than per world,
     * since it is a fact about what one client has been told.
     */
    int v5_playerinfo_sent;

    /**
     * The position this client was last told, for the v5 stream's DELTA.
     *
     * Seeded by the init block. Kept per player because it is a fact about what
     * one client has been sent, not about where the player is.
     */
    int v5_last_x;
    int v5_last_z;
    int v5_last_level;

    /*
     * The world this player is in, and where its bytes go.
     *
     * Both used to live on `struct ToriRSServer` — the session as a field, the
     * world implicitly by being the same struct. They are here because a packet
     * is addressed to *a player*: with more than one, "send the inventory" has
     * to know whose. `session` is NULL for a player with no client attached,
     * which is what the selftest runs and what makes every encoder exercisable
     * without a socket.
     */
    struct ToriRSServer* world;
    struct ToriRSServerSession* session;

    /**
     * 1 while this slot holds a player.
     *
     * The pool is never compacted, so this — not `player_count` — is what says
     * whether `players[i]` is anybody. A logout clears it and leaves the hole.
     */
    int active;

    /** Set the instant this pid is freed (`ToriRSServer_WorldPlayerFree`);
     *  cleared by `ToriRSServer_WorldPlayerReap` once every observer's
     *  PLAYER_INFO for this tick has already reported it gone.
     *  `ToriRSServer_WorldAddPlayer`'s free-slot scan treats this exactly like
     *  `active` — a pid cannot be handed to a new login while a client might
     *  still resolve it as the departed player. Same hazard, same fix, as
     *  `ToriRSServerNpc.pending_free` — see docs/torirs_server_npc_slot_reap.md. */
    uint8_t pending_free;

    /**
     * Index in the world's pool, and the pid the wire carries.
     *
     * One number, the same on every stream. `UPDATE_PID` carries this — not the
     * 2047 sentinel — because the client uses it to decide which entity in the
     * PLAYER_INFO stream is itself, and every *other* absolute reference to a
     * player (an npc's FACE_ENTITY, most visibly) has to resolve against the
     * same space. Sending 2047 to everybody made each client's self-pid a
     * different player's, so "face pid 34815" meant a different person on each
     * screen. The reference sends the real slot too (`Player.onLogin` ->
     * `new UpdatePid(this.slot, this.members)`).
     *
     * 2047 stays reserved on the wire as the 11-bit add-list terminator, which
     * `TORIRSSERVER_PLAYER_MAX` keeps unreachable rather than the pid allocator
     * having to know.
     */
    int pid;

    /** Bumped whenever this pool slot is assigned to a new login. Never zero,
     *  because zero is the unowned sentinel on ToriRSServerNpc. */
    uint32_t login_generation;

    int x, z, level;
    /** Whether this tick's steps are being run rather than walked. Derived
     *  each tick from `run_toggle` and whether any energy is left. */
    int running;
    /** The player's standing preference: the run orb, and the ctrl-held flag
     *  the client puts on a move request. Mirrored into varp 173 so the orb
     *  draws itself lit. */
    int run_toggle;
    /** 0..TORIRSSERVER_RUN_ENERGY_MAX. */
    int run_energy;
    /* No prayer state here. The cache's `prayer_<name>` varbits ARE the
     * state — content writes them, and nothing in the engine reads them. There
     * used to be a `prayer_active` mask beside them, and having two copies with
     * the C one authoritative is what kept every prayer rule in C. */
    /** Set the moment hitpoints reach 0 and cleared when they rise again.
     *  Gates everything a corpse must not do; how long that lasts, and what
     *  happens during it, is `[queue,player_death]`. There was a `death_tick`
     *  here, which meant the engine owned the length of a death. */
    int dying;
    /** One-shot: the next `ToriRSServer_CombatHitPlayer` on this player is
     *  self-inflicted — a script that called `damage()` on its own uid, which
     *  is how an overload, a dwarven rock cake and a poison karambwan all hurt
     *  their drinker. The Nightmare Zone's absorption pool must not soak those
     *  (wiki Absorption: "It will not absorb damage the player inflicts on
     *  themselves"), and the damage funnel has no other way to tell a swing
     *  from a sip. Set by the `damage()` opcode, cleared by the funnel. */
    int hit_self_inflicted;
    /** Debug invulnerability (`::god`). Gates the one player damage funnel,
     *  `ToriRSServer_CombatHitPlayer`, so every source — npc melee, the Inferno's
     *  queued projectile damage, poison, content's own `damage()` — lands as a
     *  block splat instead of a subtraction.
     *
     *  This is engine rather than content on purpose. A cheat here is normally
     *  a `[debugproc]` (see handle_cheat), but "absorb all damage" is not
     *  something a script can express: content reaches hitpoints only *through*
     *  this funnel, so a content-side flag would still need this same gate and
     *  would only add a second copy of the state. It exists so a profiling run
     *  can move around a live encounter without the death sequence rewriting
     *  the scene mid-measurement. */
    int godmode;
    /** Reject the player's own movement/action packets while leaving the
     *  simulation live. Unlike busy/canAccess this does not pause scripts,
     *  queues, timers, or damage; content owns the duration through
     *  player_lock()/player_unlock(). */
    int action_locked;
    /**
     * The tick an inbound packet last carried a player INPUT, or
     * TORIRSSERVER_INPUT_TICK_NEVER when this slot has no client to hear from.
     *
     * Not liveness: the client's own keepalives (NO_TIMEOUT, IDLE_TIMER) and
     * its bookkeeping (window status, scene acks) are exactly what this must
     * not count, or it would say "the player is here" about a client left
     * running in an empty room. `ToriRSServer_WorldHandle` sets it for everything
     * else — a click, a walk, a key, a button, a chat line.
     *
     * What reads it is the anti-AFK rule the wiki states on Auto Retaliate:
     * retaliation follows a player for 20 minutes of no input, "after which
     * players stop attacking all together even if they are attacked by
     * monsters". See TORIRSSERVER_AFK_COMBAT_TICKS and ToriRSServer_CombatEngage.
     *
     * The sentinel is for the session-less player an in-process fixture stands
     * up: there is no keyboard for it to fall silent at, and a clock that ran
     * anyway would stop every fixture fight the moment the suite passed 2000
     * ticks. A test that wants the rule writes a tick here and gets the real
     * clock — which is why the sentinel is INT32_MIN and not -1: a fixture
     * arming the clock 2000 ticks back on tick 300 writes a negative number,
     * and that must be a time rather than an opt-out.
     */
    int32_t last_input_tick;
    /** The overhead-icon bits for the appearance block. Content's, through
     *  HEADICONS_GET/SET — the engine neither knows nor asks which prayer put a
     *  bit here, which is exactly the reference's arrangement
     *  (`Player.headicons` + PlayerOps, and no prayer concept anywhere in its
     *  engine). */
    int headicons;
    /** What is mounted in the gameframe's two modal slots (0 = nothing).
     *
     *  CLOSE_MODAL is the client asking to shut whatever modal is up — the X on
     *  a framed interface, and the Escape key. The server is what unmounts, so
     *  without a record of what it opened the request has nowhere to go: it used
     *  to test `bank.open` and return, which made the bank the only interface in
     *  the game that could be closed. Anything a content script opened with
     *  `if_openmain` stayed on screen forever. */
    int mainmodal_group;
    int sidemodal_group;
    /** What is mounted in the chatbox dialogue slot (0 = nothing).
     *
     *  Tracked for the same reason as the two above, and it is the third slot
     *  the reference keeps (`ModalState.CHAT`). Without it a `~chatnpc` was
     *  invisible to the server: the dialogue lived only as a parked script, so
     *  nothing — not Escape, not walking away — could take it off the screen. */
    int chatmodal_group;
    /** Current gameframe root interface id and its modal/floater slots.
     *  Updated by if_opentop; login starts on toplevel_osrs_stretch. */
    int gameframe_iface;
    int gameframe_mainmodal;
    int gameframe_sidemodal;
    int gameframe_floater;
    /** `<top>:helper_content`, where the helper panel mounts. Bound beside the
     *  three above because every gameframe in this cache carries the slot and
     *  the child ids differ between them (12 on the stretch layouts, 39 on
     *  `toplevel`). */
    int gameframe_helper;
    /** Complete root/mount/event authority used to build IF_RESYNC_V2. The
     * gameframe_* fields above are cached aliases for legacy call sites; every
     * interface encoder mutates this registry before writing the packet. */
    struct ToriRSServerInterfaceState interfaces;
    /** Last Display-panel clientMode (0/1/2) from WINDOW_STATUS.
     *  Persisted in the player save; login restores via ~gameframe_set_mode. */
    int client_layout_mode;
    /** Index last chosen in the All Settings panel's shared dropdown list
     *  (settings:dropdown_buttons), or -1. Not a setting on its own: the panel
     *  reuses one list for every row, so the op says which entry was picked
     *  and not which setting it belonged to. handle_window_status is what
     *  gives it a meaning, and consumes it. Session state, never saved. */
    int settings_dropdown_choice;
    /** Percent / grams last put on the wire, so UPDATE_RUNENERGY and
     *  UPDATE_RUNWEIGHT go out only when the orb would actually change. */
    int run_energy_sent;
    int run_weight_sent;

    /** Absolute destination of the walk in progress, for the arrival check
     *  that clears the client's map flag. -1 when idle. */
    int dest_x, dest_z;

    /**
     * Waypoint queue — LostCity PathingEntity shape.
     *
     * waypoints[waypoint_index] is the tile currently being walked toward;
     * the index counts down to 0 (the final destination). -1 means idle.
     * At most TORIRSSERVER_WAYPOINT_MAX entries; each is a corner (last tile of a
     * straight BFS run), not every tile — advance_player greedily steps toward
     * the current one and re-validates collision each tick.
     */
    struct ToriRSServerStep waypoints[TORIRSSERVER_WAYPOINT_MAX];
    int waypoint_index;
    int last_step_x, last_step_z;
    int follow_x, follow_z;

    /** Steps taken this tick (for "I can't reach that" termination). */
    int steps_taken;

    /** What this walk is *for*, resolved once per tick by phase 5. */
    struct ToriRSServerInteraction interaction;

    /**
     * Bumped by every `ToriRSServer_WorldInteractionSet` / `_clear`, so a caller
     * that ran a script can ask "did it establish an interaction of its own?".
     *
     * This is `Player.nextTarget` in the reference (`Player.tryInteract`), which
     * gets the same answer structurally: it nulls `target` before dispatching
     * and reads back whatever the script left there. That shape is not available
     * here because `ap_range` and `ap_range_called` live *on* the interaction
     * rather than on the player, so clearing it first would throw away the
     * `p_aprange` the ap script is about to call. Comparing a counter is the
     * same question asked from the other side.
     *
     * The field it replaced as a discriminator was `ap_tried`, which happens to
     * work (`interaction_set` zeroes it) and reads as a coincidence.
     */
    unsigned interaction_serial;

    struct ToriRSServerItem inv[TORIRSSERVER_INV_SLOTS];
    struct ToriRSServerItem worn[TORIRSSERVER_WORN_SLOTS];

    /** Per-slot "changed since the last flush" bits, so a tick sends one
     *  UPDATE_INV_PARTIAL with only what moved. Owned by the two registry rows
     *  that adopt them (torirs_server_container.h), not written directly. */
    uint32_t inv_dirty;
    uint32_t worn_dirty;

    /** Every container this player holds, including the backpack, the worn set
     *  and the bank. torirs_server_container.h; freed by
     *  ToriRSServer_ContainerShutdownPlayer before the struct is cleared. */
    struct ToriRSServerContainer containers[TORIRSSERVER_CONTAINER_MAX];

    /** Which extended-info fields to send. Cleared in phase 11. */
    uint32_t masks;
    int anim_id;
    int anim_delay;
    /**
     * Appearance idle/walk/run seqs (LostCity PathingEntity readyanim…runanim).
     * Written by READYANIM, TURNANIM, WALKANIM, RUNANIM; put_appearance encodes
     * the seven as p2s. Defaults match the unarmed human_* set the client
     * already assumes at spawn (808/823/819-822/824).
     */
    int readyanim;
    int turnanim;
    int walkanim;
    int walkanim_b;
    int walkanim_l;
    int walkanim_r;
    int runanim;
    /** NPC type used by p_transmogrify; -1 renders the normal player body. */
    int transmog_npc;
    int face_entity;
    int face_x;
    int face_z;
    /**
     * Pending loc/obj face point in absolute half-tiles (LostCity targetX/Z).
     * Set by interaction_set for NonPathingEntity targets; consumed by
     * reorient after movement when steps_taken==0. Survives interaction_clear
     * so FACE_COORD can ship on the same tick the op clears the interaction.
     * -1 when idle.
     */
    int face_target_x;
    int face_target_z;
    char say[80];
    int spotanim_id;
    int spotanim_height_delay;
    /**
     * `p_exactmove` — the pair `p_locmerge` belongs to. The two tiles the
     * client glides between, in **absolute world** coordinates, plus the cycle
     * window and the reference's 0..3 facing direction
     * (`^exact_north`..`^exact_west`, engine.constant).
     *
     * Stored absolute rather than wire-shaped because the two revisions state
     * the same fact differently and only the encoder knows which one it is
     * writing: the classic block carries scene-local tiles and a direction
     * byte (Client-TS `Client.ts:8202`), the rev-239 block carries signed
     * deltas from the player's own tile and a yaw
     * (`osrs239_entity_info.c` sets `relative` and `facing_is_yaw`).
     * Read only while TORIRSSERVER_PMASK_EXACT_MOVE is set, so phase 11's mask
     * clear is the whole lifetime.
     */
    int exact_start_x;
    int exact_start_z;
    int exact_end_x;
    int exact_end_z;
    int exact_start_cycle;
    int exact_end_cycle;
    int exact_direction;
    /** Every splat this player takes this tick; see struct ToriRSServerHitmark.
     *  The player is the entity that most often takes several at once — in the
     *  Inferno a whole wave lands on them together. */
    struct ToriRSServerHitmark hitmarks[TORIRSSERVER_HITMARK_MAX];
    int hitmark_count;
    /** The first of them, mirrored — see the npc's copy of this field. */
    int damage;
    int damage_type;
    int hitpoints;
    int max_hitpoints;
    int chat_colour_effect;
    int chat_type;
    uint8_t chat_data[80];
    int chat_len;

    /** Set after a teleport or a rebuild: the next PLAYER_INFO must carry an
     *  absolute placement (move op 3) rather than a step direction. */
    int place_dirty;

    /**
     * The `place_dirty` placement is short enough to animate — clear the *jump*
     * bit and let the client glide there on foot.
     *
     * `PathingEntity.jump` in the reference, inverted so that the default (0)
     * is the snap every other `place_dirty` site wants and only `p_teleport`
     * has to opt out. Ash on the pair: "p_teleport() — a command that
     * 'forcibly' moves the player and enables walk animations if the distance
     * is short ... p_telejump() is an alternative command that forcibly moves
     * the player and never plays walk animations" (docs/ASH_MOVEMENT_CORPUS.md
     * §14). The reference reaches the same place from the other side:
     * `teleport()` raises `jump` only on a plane change, and
     * `validateDistanceWalked()` raises it when the tick moved more than two
     * tiles.
     *
     * Firemaking is what this is for. `~push_player` steps you off the fire
     * with `p_teleport(movecoord(coord, -1, 0, 0))`, and with the jump bit
     * nailed to 1 the player blinked one tile sideways instead of walking.
     *
     * Cleared with `place_dirty`, in the same end-of-tick reset — it describes
     * one placement, and a stale glide would animate the next teleport.
     */
    int tele_glide;

    /**
     * The same glide written as the step directions an observer's stream can
     * carry, and how many there are (0, 1 or 2).
     *
     * The local player's section has move op 3 and a jump bit; the tracked
     * section has neither, so a teleport there was a remove plus a re-add and
     * a re-add always snaps. Two tiles or less is one or two steps, so PLAYER_INFO
     * says that instead and never drops the observer's copy — see the note at
     * `SS_OP_P_TELEPORT`, which is also where the reference's own answer
     * (`add(..., other.jump)`) is shown not to port to this client.
     *
     * Zero when the placement is not expressible as steps; observers fall back
     * to the remove/re-add. Cleared with `tele_glide`.
     */
    int tele_glide_steps[2];
    int tele_glide_step_count;

    /*
     * ── What this client has been told about ─────────────────────────
     *
     * Every one of these is a *per-client* view of shared world state, and every
     * one of them lived on the world while the pool held one. The rule they all
     * follow: the world owns the fact, the player owns whether this client knows
     * it yet. Two players standing in different places have different answers,
     * and sharing one set encoded the first player's view to the second.
     */

    /** Ordered list of npc slots this client is tracking — exactly the order
     *  NPC_INFO's tracked section must be written in — plus the membership test
     *  the "entering view" scan needs. Kept as both because the list answers
     *  "in what order" and the flags answer "at all" in O(1). The list is sized
     *  by the wire (an 8-bit count), the flags by the world. */
    int tracked[TORIRSSERVER_TRACKED_NPC_MAX];
    /** `srv->npcs[tracked[i]].generation` as of the tick that npc was last
     *  written into this list. `npc_spawn` can hand the same slot to a
     *  different npc inside one tick (see its own comment on
     *  `ToriRSServer_ZoneNpcRefile`) — the "already tracked" loop below has to
     *  notice that before it treats the new occupant's data as this slot's
     *  continuation, or the new npc's masks (including a same-tick hit) get
     *  spliced onto whatever this client still thinks the slot is. */
    int tracked_generation[TORIRSSERVER_TRACKED_NPC_MAX];
    int tracked_count;
    /* This observer's current npc view radius — see TORIRSSERVER_NPC_VIEW_TILES_MAX.
     * Zero means "not yet initialised"; `npc_view_radius` resolves that to the
     * resting 15 so a player struct that predates this field still works. */
    int npc_view_tiles;
    uint8_t npc_tracked[TORIRSSERVER_NPC_MAX];

    /** The same pair for other players. `tracked_players` holds pool indices
     *  (pids), in the order PLAYER_INFO's tracked section writes them. */
    int tracked_players[TORIRSSERVER_PLAYER_MAX];
    /** `srv->players[tracked_players[i]].login_generation` as of the tick that
     *  pid was last written into this list — the player equivalent of
     *  `tracked_generation`, guarding the same hazard: a pid freed by a
     *  logout and reused by a different login before this client's PLAYER_INFO
     *  caught up would otherwise read as the departed player, still there. */
    uint32_t tracked_player_generation[TORIRSSERVER_PLAYER_MAX];
    int tracked_player_count;
    uint8_t player_tracked[TORIRSSERVER_PLAYER_MAX];

    /*
     * ── This client's area ───────────────────────────────────────────
     *
     * `ground_sent[TORIRSSERVER_GROUND_MAX]` was here: one flag per ground slot per
     * client, rescanned flat every tick. It is gone, and what replaced it is
     * not a smaller flag array — it is the area below. "Has this client been
     * told about that obj" turns out to be the wrong question; the right one is
     * "does this client hold that zone", because the answer covers the locs and
     * the replay too, and because it is the question the wire asks.
     */
    struct ToriRSServerPlayerZoneMap zonemap;
    /** What this client calls each npc it is being told about. See the type. */
    struct ToriRSServerPlayerSlotMap npc_slots;
    /** Packed zone this client was last in, or -1. */
    int zone_index;
    /**
     * Packed zone index **plus one** of where the ZoneMap has this player
     * filed, 0 for nowhere — the same convention the npcs and objs use.
     *
     * NOT `zone_index` above, and the distinction is load-bearing:
     * `zone_index` is the *window* latch and `ToriRSServer_ZonePlayerReset` sets
     * it to -1 on every scene rebuild so the window is recomputed. Filing off a
     * field that is cleared behind your back would unfile the player on every
     * rebuild and never re-file them, so after 88 tiles of walking nobody could
     * see anybody.
     */
    int zone_filed;

    /** The song this client is currently being played, or -1. Held so entering
     *  a second map square that maps to the same track does not restart it --
     *  a track restarting every 64 tiles is the tell that this is missing. */
    int music_track;

    /** The ambient soundscape this client is currently running, or -1 for
     *  none. Same latch discipline as `music_track`: the bed is re-sent only
     *  when the id actually changes. */
    int ambient_scape;

    /**
     * The map square whose ambience `ambientsound` was last called for, or -1.
     *
     * A script that owns its square's bed (the QBD arena stops it, because the
     * arena's noise comes from loc emitters) has to survive the map-square
     * latch firing in the *same tick* as the teleport that put the player
     * there. Recording the square the script spoke about makes the latch's
     * "re-establish the world bed" step conditional on the player having
     * actually left that square, so the two cannot fight and the order they
     * run in stops mattering.
     */
    int ambient_script_map_x;
    int ambient_script_map_z;

    /*
     * ── The two coordinate latches `updateMap` holds ─────────────────
     *
     * `lastZone` and `lastMapZone` in `NetworkPlayer.updateMap`, and they are
     * two latches rather than one because the two halves of the zone trigger
     * family are keyed at different granularities: `[zone]`/`[zoneexit]` off the
     * 8-tile zone *including the level*, `[mapzone]`/`[mapzoneexit]` off the
     * 64-tile map square with the level forced to 0. So climbing a ladder
     * re-enters a zone and does not re-enter a map square.
     *
     * **These are deliberately not `zone_index`**, which looks like exactly the
     * right field and is not: `ToriRSServer_ZonePlayerReset` sets it to -1 on every
     * rebuild and on every climb (four call sites), so a trigger latch hung off
     * it would re-fire `[zone,…]` whenever the world's origin moved under a
     * standing player, and swallow the matching `[zoneexit]`. Nothing else may
     * write these two; only `ToriRSServer_WorldUpdateMap` does.
     *
     * Stored as components rather than a packed coord so no unpack is needed to
     * name the zone being *left*. `last_zone_level` and `last_map_x` are -1
     * until the first transition, which is what suppresses the exit at login —
     * the reference gets the same from a fresh `Player` and never resets them,
     * `cleanup()` included.
     */
    int last_zone_level;
    int last_zone_x;
    int last_zone_z;
    int last_map_x;
    int last_map_z;

    /** REBUILD_NORMAL owed to this client: it walked out of the scene, someone
     *  else moved the world's origin, or it changed level. */
    int rebuild_pending;
    /** Which map-instance build this client's scene is a copy of
     *  (`ToriRSServer_MapInstanceGeneration`); 0 = not in an instance. A mismatch
     *  against the instance the player is standing in means the scene it holds
     *  was assembled from a map that no longer exists, and only a fresh
     *  REBUILD_REGION fixes it. See phase_client_out and
     *  docs/ORANGE_WEDGE.md §18: the allocator reuses handle AND square, so
     *  nothing else distinguishes a re-entry from staying put. */
    int scene_instance_generation;
    /** Revision-239 login barrier. REBUILD/GPI has gone out, but the client
     *  has not yet reported MAP_BUILD_COMPLETE for that scene. While set, no
     *  player tick state or login/UI burst may advance or be discarded. */
    int login_scene_pending;
    /** Revision-239 barrier for an instance rebuild that moves the local
     *  player to a new absolute placement. REBUILD_REGION has gone out, but
     *  MAP_BUILD_COMPLETE has not returned.
     *  Player scripts, timers, movement and dependent output pause here so an
     *  ephemeral zone event cannot expire while the client is still replacing
     *  its WorldView. */
    int rebuild_scene_pending;
    /**
     * A temporary, player-scoped normal-world scene shown without moving the
     * authoritative player (Construction scrying). While active, scene-local
     * entity and zone output is withheld because it belongs to the player's
     * real scene. `remote_view_until` is an absolute world tick and guarantees
     * restoration even if the content script that opened the view is aborted.
     */
    int remote_view_active;
    int remote_view_until;
    int remote_view_saved_action_locked;
    /** Set by the login burst, drained by phase 7, so [login] runs inside the
     *  tick rather than ahead of it — per player, so a second login does not
     *  re-run the first player's. */
    int login_pending;
    /** This client's walk ended, so its map flag comes down. */
    int clear_map_flag;

    /** World map overlay state. `worldmap_tile_sent` is the packed coord the
     *  map's varc was last told about, so the per-tick refresh only fires when
     *  the player actually moved. */
    int worldmap_open;
    int worldmap_tile_sent;

    /** This tick's movement, filled in by the tick and consumed by the
     *  PLAYER_INFO encoder: 0 tiles idle, 1 walking, 2 running. */
    int move_dirs[2];
    int move_count;

    /** Player variables. No dirty list beside them: `ToriRSServer_WorldMarkVarp`
     *  encodes the packet at the point of the write, the way the reference's
     *  `Player.setVar` does, so that a script's own ordering against
     *  `if_opensub` survives. */
    int32_t varps[TORIRSSERVER_VARP_COUNT];

    /** Durable POH model. Active instance coordinates and loc handles never
     *  enter this record; Construction content rebuilds those from these room
     *  and decoration rows whenever the owner enters. */
    struct ToriRSServerPohState poh;

    /** The bank: container 95 plus the settings its interface reads out of
     *  varbits. Heap-allocated, so ToriRSServer_BankShutdown has to run before the
     *  player struct is cleared. See torirs_server_bank.h. */
    struct ToriRSServerBank bank;

    /** The script parked on this player, resumed by phase 5. At most one: a
     *  player is doing one thing at a time, which is also why a new trigger
     *  arriving while one is parked has to be refused rather than queued. */
    struct SSVM_State* active_script;
    /** Tick at which the player stops being delayed. */
    int delayed_until;

    /**
     * Ticks of stun remaining — `p_stun(ticks)`.
     *
     * A stun is not `delayed_until` and not `action_locked`, and the three are
     * deliberately separate:
     *
     *   `delayed_until`  the player's own script is parked. It gates the queue
     *                    drain and the script phases; it does not stop a walk.
     *   `action_locked`  a cutscene-grade time-stop. Everything the player
     *                    could send is refused, interface clicks included.
     *   `stun_ticks`     OldSchool's stun. Movement and every WORLD
     *                    interaction stop; the inventory, the equipment and
     *                    the prayer book keep working.
     *
     * That last distinction is the whole reason this is not just a lock with a
     * timer on it. A stunned player in OldSchool can still eat, drink and flick
     * a protection prayer — it is what makes a stun survivable rather than a
     * death sentence — and `player_action_packet` (which `action_locked` uses)
     * refuses INV_BUTTON and IF_BUTTON along with everything else. Stuns get
     * their own, narrower predicate: `player_stun_blocks_packet`.
     *
     * A countdown rather than an expiry tick, matching `ToriRSServerNpc::frozen_ticks`
     * and for the same reason — the gate is evaluated where the server tick is
     * not always in hand. Decremented once per player phase.
     *
     * The longer stun wins on re-application, exactly as `npc_freeze` does: a
     * second tail swing landing on an already-stunned player must not shorten
     * what the first one bought.
     */
    int stun_ticks;
    /**
     * Script id of the armed `[walktrigger,…]`, or -1.
     * LostCity `PathingEntity.walktrigger` — set by `walktrigger(X)`, cleared
     * when the engine fires it (the script re-arms itself while still frozen).
     */
    int walktrigger;
    /** Candidate tile while an armed walktrigger is executing, packed as a
     *  coord. Zero outside that narrow pre-step window. Encounter controllers
     *  use WALKSTEP_COORD to apply content-owned per-tile traversal rules. */
    int walkstep_coord;

    struct ToriRSServerQueued queue[TORIRSSERVER_QUEUE_MAX];
    /** `Player.engineQueue`. Zone triggers only; see `enum ToriRSServerQueueKind`. */
    struct ToriRSServerQueued engine_queue[TORIRSSERVER_ENGINE_QUEUE_MAX];
    struct ToriRSServerTimer timers[TORIRSSERVER_TIMER_MAX];

    /** Component uids that will release a p_pausebutton wait. Cleared whenever
     *  a script finishes, so a stale button cannot resume the next one. */
    int resume_buttons[TORIRSSERVER_RESUME_BUTTON_MAX];
    int resume_button_count;
    /** The component that released the last wait — what `last_com` returns. */
    int last_com;
    /*
     * The rest of the `last_*` family, which is how a RuneScript interface
     * trigger learns what was clicked: the script is bound to a *component*,
     * and everything about the click that is not the component arrives here.
     *
     * `last_slot` is the grid cell, `last_targetslot` the cell a drag landed
     * on, `last_item` the obj that was in it, `last_verb` the 1-based op index,
     * `last_subop` the rev-239 submenu index (or -1 for a normal op), and
     * `last_int` the number a p_countdialog collected. All -1 / 0 when the
     * last trigger did not carry one, which is the same thing the reference
     * does — a script reading one it was not given gets a sentinel, not a
     * stale value from an unrelated click.
     */
    int last_slot;
    int last_targetslot;
    int last_item;
    int last_verb;
    int last_subop;
    int32_t last_int;
    /*
     * The other half of a use-on: the item the player was *carrying* when they
     * clicked, as opposed to the thing they clicked on.
     *
     * "Use A on B" has two ids and only one of them is the trigger's subject.
     * The subject is B — the loc, npc, obj or (for `opheldu`) the clicked item;
     * A reaches the script only through these two. Every `*u` handler sets them
     * before the dispatch and nothing clears them afterwards, which is the
     * reference's own behaviour (`Player.clearInteraction` leaves them alone):
     * they mean "the last use-on", not "the current one".
     *
     * `opheldu` additionally *swaps* these with `last_item`/`last_slot` on two
     * of its four lookup rungs, so that a script always finds its own subject
     * in `last_item` whichever of the two items the player picked up first.
     * See `ToriRSServer_ScriptsRunOpheldu`.
     */
    int last_useitem;
    int last_useslot;

    /*
     * The db query `db_listall` selects and `db_findnext` walks.
     *
     * **`db_query_table` must be initialised to -1, not memset to 0**, because 0
     * is a real dbtable id — the same trap as `session->pending_opcode`. Zeroed,
     * a `db_findnext` with no query would quietly iterate table 0 instead of
     * reporting that nothing was selected.
     *
     * On the player rather than on the script state (where the reference keeps
     * it) so a script that suspends between two rows resumes on the same query
     * without the park having to carry it.
     */
    int db_query_table;
    int db_query_index;
    /*
     * What `db_find` selected, or column -1 for the whole table (`db_listall`).
     *
     * The reference materialises the matching row ids into a list at find time;
     * this keeps the predicate and re-tests it in `db_findnext`, which is the
     * same walk without the allocation. The difference is visible in exactly one
     * place and it is a place the reference does not reach either: a `.dbrow`
     * edited between the find and the walk. Content cannot do that.
     */
    int db_query_column;
    /** -1 searches every tuple position; otherwise the packed DB column's
     *  selected tuple position. */
    int db_query_tuple;
    int db_query_value;

    /** The name typed at the login screen, which is what `displayname` returns.
     *  Nothing else in the mock has a use for it — there is one player and the
     *  wire never carries a name — but a dialogue that puts the player's words
     *  on screen has to label them with something, and "Player" is a worse
     *  answer than the one the client already sent. */
    char display_name[32];

    /*
     * `display_name` packed base 37 — the key everything social is filed under.
     *
     * Cached here rather than re-packed at each use because it is the *identity*
     * the friend service knows this player by, and the two must not be able to
     * drift: ToriRSServer_WorldSetDisplayName is the one place that writes both.
     * 0 for a player whose name never arrived (the selftest's, before it is
     * given one), which the service rejects as an invalid name.
     */
    int64_t name37;

    /**
     * The reference's `socialProtect`: one social packet per tick, spent by the
     * first of the six that arrives and cleared in phase 11 (the reference
     * clears it in `resetEntity`). Read and written only through
     * ToriRSServer_FriendsSocialGate.
     */
    int social_protect;

    /* Combat. `hitpoints` / `max_hitpoints` live with the DAMAGE mask fields
     * above, since the mask is what carries them to the client. */
    /** Npc slot being fought, or -1. */
    /*
     * This player's FOLLOWER (familiar / pet), as an npc slot plus the
     * generation of the npc that occupied it -- not merely "an npc they own".
     *
     * Ownership (ToriRSServerNpc::owner_pid/owner_gen, set by `npc_setowner`) means
     * "this npc is private to this player", which a minigame spawning private
     * npcs uses legitimately. Following is a different and much narrower fact,
     * and there is at most one. Deriving one from the other is what let
     * `npc_findowned` return the Queen Black Dragon -- owned, and the
     * lowest-numbered owned npc in the arena -- as the player's familiar, so
     * `call familiar` teleported the boss to the player.
     *
     * The generation is what makes the link safe across despawn and slot reuse:
     * a stale slot number resolves to nothing rather than to whoever inherited
     * it. -1 / 0 is "no follower".
     */
    int follower_slot;
    uint16_t follower_gen;
    int combat_target;
    int attack_clock;

    /*
     * The enemy health overlay's session state (torirs_server_hpbar.c).
     *
     * `linger` is what keeps the panel up across the gap between one npc dying
     * and the next click; the last reading is held with it because by then the
     * slot may belong to somebody else, and re-reading it would put a stranger's
     * hitpoints on the bar.
     */
    int hpbar_open;
    int hpbar_linger;
    int hpbar_last_type;
    int hpbar_last_hp;
    int hpbar_last_max;

    /*
     * The helper panel -- see torirs_server_helper.c.
     *
     * `helper_open` is which helper is mounted (a TORIRSSERVER_HELPER_* value,
     * or _NONE), and `helper_arg` the argument its builder was run with. Both
     * are needed rather than a bare open/closed flag: the builders draw once
     * from a RUNCLIENTSCRIPT rather than re-reading a var, so a change of
     * either has to re-run the builder, and only a remembered pair can tell a
     * change from a steady state.
     */
    int helper_open;
    int helper_arg;

    /*
     * Skills. `level` is the base level, `boosted` what a potion or a drain
     * left it at, and `xp` is in tenths of a point — OldSchool's hitpoints
     * award is 4/3 of the damage dealt, which is not an integer, and rounding
     * it every hit loses a third of a point per swing.
     *
     * The hitpoints *stat* and the player's hitpoints are one thing:
     * `boosted[STAT_HITPOINTS]` IS `hitpoints`, kept in step by
     * ToriRSServer_CombatSyncHitpoints, because the client's health orb reads the
     * stat and the hitsplat's health bar reads the DAMAGE mask.
     */
    int stat_level[TORIRSSERVER_STAT_COUNT];
    int stat_boosted[TORIRSSERVER_STAT_COUNT];
    int stat_xp_tenths[TORIRSSERVER_STAT_COUNT];
    /** Stats whose level or xp changed this tick, flushed by phase 10. */
    uint32_t stat_dirty;


    /**
     * 0 male, 1 female. Read by the appearance blob and by `text_gender`.
     *
     * It was a literal `0` in the encoder, labelled "gender: male" by a comment
     * beside it — a constant standing in for state, and `text_gender`
     * cannot be implemented against a comment. Nothing sets it yet (there is no
     * character-design flow here), so every player is male; the difference is
     * that the answer now comes from one place instead of two.
     */
    int gender;

};

/*
 * LostCity PathingEntity.setFaceEntity for players.
 *
 * Face the current pathing target every turn: combat first, else a pending
 * npc/player interaction (so walk-to-attack faces during approach), else
 * clear. Mask only on change — FACE_ENTITY is a latch.
 */
static inline void
ToriRSServer_PlayerSetFaceEntity(struct ToriRSServerPlayer* player)
{
    int want = -1;

    assert(player);
    if( player->combat_target >= 0 )
        want = player->combat_target;
    else if( player->interaction.kind == TORIRSSERVER_INTERACT_NPC )
        want = player->interaction.npc_slot;
    else if( player->interaction.kind == TORIRSSERVER_INTERACT_PLAYER )
        want = TORIRSSERVER_FACE_PLAYER_BASE + player->interaction.npc_slot;

    if( player->face_entity == want )
        return;
    player->face_entity = want;
    player->masks |= TORIRSSERVER_PMASK_FACE_ENTITY;
}

#include "torirs_server_ids.h"

/** Session gameframe slots — prefer the player's live top after if_opentop. */
static inline int
ToriRSServer_PlayerGameframeIface(struct ToriRSServerPlayer const* player)
{
    if( player && player->interfaces.root_interface > 0 )
        return player->interfaces.root_interface;
    if( player && player->gameframe_iface > 0 )
        return player->gameframe_iface;
    return ToriRSServer_Ids()->iface_gameframe;
}

static inline int
ToriRSServer_PlayerMainmodal(struct ToriRSServerPlayer const* player)
{
    if( player && player->gameframe_mainmodal > 0 )
        return player->gameframe_mainmodal;
    return ToriRSServer_Ids()->com_gameframe_mainmodal;
}

static inline int
ToriRSServer_PlayerSidemodal(struct ToriRSServerPlayer const* player)
{
    if( player && player->gameframe_sidemodal > 0 )
        return player->gameframe_sidemodal;
    return ToriRSServer_Ids()->com_gameframe_sidemodal;
}

static inline int
ToriRSServer_PlayerFloater(struct ToriRSServerPlayer const* player)
{
    if( player && player->gameframe_floater > 0 )
        return player->gameframe_floater;
    return ToriRSServer_Ids()->com_gameframe_floater;
}

static inline int
ToriRSServer_PlayerHelper(struct ToriRSServerPlayer const* player)
{
    if( player && player->gameframe_helper > 0 )
        return player->gameframe_helper;
    return ToriRSServer_Ids()->com_gameframe_helper;
}

/**
 * One queued "this slot's occupant is gone" command — the emitted half of
 * the despawn/reap split (ToriRSServer_WorldNpcFree / ToriRSServer_WorldNpcReap).
 * Mirrors PktNpcInfoOp's reader-emits-commands shape, and the real client's
 * own `field956` removal queue (Statics.method13029 in the rev-239 deob):
 * a despawn site doesn't free a slot directly, it emits a command here, and
 * a single reap call later in the tick is what actually acts on it. See
 * docs/torirs_server_npc_slot_reap.md.
 */
struct ToriRSServerNpcFreeCmd
{
    int slot;
    /** npc->generation at the moment this was queued — defensive: lets the
     *  reap refuse to touch a slot that was somehow reused before its own
     *  queued command drained (should be structurally impossible, since
     *  `pending_free` blocks npc_spawn's scan until reap runs, but this
     *  costs nothing and matches every other generation guard in this
     *  file). */
    uint16_t generation;
};

/** The player equivalent of `ToriRSServerNpcFreeCmd` — same reason, same shape,
 *  scaled down to `TORIRSSERVER_PLAYER_MAX` pids instead of npc slots. */
struct ToriRSServerPlayerFreeCmd
{
    int pid;
    uint32_t generation; /* player->login_generation at the moment this was queued */
};

struct ToriRSServer
{
    /*
     * Where this world's bytes go, or NULL for a world with no client at all —
     * which is what the selftest runs, and what makes every encoder testable
     * without a socket.
     *
     * The session owns the transport, both ISAAC ciphers and the login state
     * machine. None of that is world state, and keeping it here is what used to
     * make "the server" and "this connection" the same struct. It is also what
     * an in-process host replaces: the world cannot tell whether the session
     * behind this pointer is a socket or a pair of byte queues.
     */
    /*
     * The players in this world.
     *
     * A pool rather than a single embedded `struct ToriRSServerPlayer`. That field
     * made "the server", "the world" and "this connection" one struct, so a
     * second player was not a change but a rewrite of every signature — and it
     * is also exactly the struct that wants saving, so persistence was blocked
     * behind the same thing.
     *
     * `players[i].active` says whether a slot holds anyone; the pool is *not*
     * compacted, because `pid` is the slot index and the wire carries it — moving
     * a player would rename them mid-session to every client tracking them.
     * `player_count` is therefore a high-water mark to iterate to, not a
     * population count.
     */
    struct ToriRSServerPlayer players[TORIRSSERVER_PLAYER_MAX];
    int player_count;

    /** Pids freed this tick via `ToriRSServer_WorldPlayerFree`, awaiting
     *  `ToriRSServer_WorldPlayerReap` — the player equivalent of
     *  `npc_free_queue`. Sized to the whole pool for the same reason: it
     *  can never overflow. */
    struct ToriRSServerPlayerFreeCmd player_free_queue[TORIRSSERVER_PLAYER_MAX];
    int player_free_queue_count;

    /*
     * World-owned containers — the `scope=shared` half of the registry.
     *
     * Structurally empty today and that is the honest state: `scope` is decoded
     * from LostCity's *server-side* inv.dat, and this tree has no `fields/inv.ini`
     * and no `[namespace:inv]` for it to live in, so ToriRSServer_ContainerScope
     * classifies everything as per-player. The table and the branch exist so
     * that adding the classifier is a one-function change rather than a rewrite
     * of every resolve site — which is what the three-case `container_for` would
     * have forced. See torirs_server_container.h.
     */
    struct ToriRSServerContainer world_containers[TORIRSSERVER_WORLD_CONTAINER_MAX];

    /**
     * World-shared variables — the `vars` half of the `%name` domain.
     *
     * Zeroed with the world, never saved and never transmitted: a shared var is
     * a fact about *this* world's run, and a client is told about it only
     * through whatever a script chooses to write into a varp. There is no
     * per-player copy and no dirty set, which is the whole point — two players
     * reading the same id read the same value in the same tick.
     */
    int32_t vars[TORIRSSERVER_VARS_COUNT];

    /** Whose turn it is — see the header comment. Never "the player". */
    struct ToriRSServerPlayer* active_player;

    int tick;

    /** Whether `map_members` reports this as a members world to content.
     *  Defaults to 1 (members) at construction — content ported from the
     *  reference is written for a members world, and `SS_OP_MAP_MEMBERS`
     *  used to be a hardcoded constant for exactly that reason. Set
     *  `TORIRSSERVER_FREE_WORLD=1` to force it to 0 for free-world testing. */
    int members_world;

    /** 1 once ToriRSServer_WorldInit has built the scene and the entities. Both
     *  hosts call that on every login, so this is what stops the second one
     *  respawning the roster under the first player. */
    int world_built;

    /** 1 once the npc pool exists and the roster may be stood up into it.
     *  `ToriRSServer_WorldSceneRebuild` runs before `ToriRSServer_WorldBuildEntities`
     *  at login — it has to, collision comes first — and that rebuild would
     *  otherwise realise the roster into a pool the build is about to memset. */
    int static_spawns_live;
    /** Roster entries currently standing, for the boot line and for anyone
     *  asking why the world holds fewer npcs than the tree states. */
    int static_npcs_live;

    /** Origin zone of the scene every client currently holds, and the window
     *  `ToriRSServer_SceneBuild` keeps collision for. One per world rather than one
     *  per player: the scene builder is a singleton, so two players far enough
     *  apart would rebuild it under each other. §6.1 step 3 is the fix. */
    int zone_x, zone_z;

    struct ToriRSServerNpc npcs[TORIRSSERVER_NPC_MAX];
    /** One past the highest slot ever spawned into. The per-tick phases walk
     *  this rather than the pool: the cap is a memory ceiling now, not the
     *  roster, and iterating 2048 slots to find 63 npcs would make it read like
     *  one. */
    int npc_slot_max;

    /** Slots freed this tick via `ToriRSServer_WorldNpcFree`, awaiting
     *  `ToriRSServer_WorldNpcReap` (called once, from phase_cleanup, after
     *  every player's NPC_INFO for this tick has already gone out). Sized to
     *  the whole pool: at most one command per currently-active npc can ever
     *  be pending between reaps, so this can never overflow. */
    struct ToriRSServerNpcFreeCmd npc_free_queue[TORIRSSERVER_NPC_MAX];
    int npc_free_queue_count;

    /**
     * The world cut into 8x8 zones — entity lists, loc records and this tick's
     * event buffers. Opaque; owned by torirs_server_zone.c, which is where the whole
     * design is written down. This is the durable record of every loc mutation
     * in the world, because the scene is not: it is re-read from the cache
     * whenever the origin moves.
     */
    struct ToriRSServerZoneMap* zone_map;

    /* The npc tracking set is per *player* — `ToriRSServerPlayer.tracked` — because
     * NPC_INFO's deltas are relative to whoever is being written to. It lived
     * here while the pool held one, and encoded the same npcs for everybody. */

    /* `rebuild_pending`, `login_pending` and `clear_map_flag` were here. All
     * three describe one client's session rather than the world's state, and
     * with a pool they have to: a second player logging in must not re-run the
     * first one's [login], and one player's walk ending must not clear the
     * other's map flag. They are on `ToriRSServerPlayer` now. */

    /** Deterministic per-connection RNG so a session replays identically. */
    uint32_t rng;

    /** The `last_int` the NEXT trigger dispatch must give its script, and
     *  whether one is stated. Set only across `ToriRSServer_ScriptsRunTriggerLastint`
     *  (the npc queue) and restored after — a scalar rather than a parameter
     *  because the value has to survive `run_trigger_impl`'s script lookup and
     *  its two refusal paths without every dispatcher growing an argument it
     *  would pass -1 for. */
    int32_t pending_last_int;
    int pending_last_int_valid;

    int verbose;

    /**
     * May a player's summoned helper swing in a SINGLE-way combat area?
     *
     * Read by content through `combat_assist_singles()`, and by nothing in the
     * engine — this is policy, not mechanism, and the engine's job is only to
     * hold it somewhere a script can reach and an operator can change.
     *
     * ## Why it is a flag at all
     *
     * Pre-EoC, a familiar could not help in a single-way area, because the
     * player and the familiar are two attackers on one victim and single-way
     * means one. That is the rule this tree's Summoning port implements, and
     * `~summoning_familiar_engagement` states it as `map_multiway` on all three
     * parties. It is also, for a modern player, the rule that makes a combat
     * familiar do nothing almost everywhere: the Evolution of Combat made
     * virtually the whole world multi in 2012 and the map this server runs
     * predates that.
     *
     * OldSchool solved the same problem later and differently, with thralls
     * (Arceuus, 2019). A thrall assists in single-way, and the reason it can is
     * a *narrower* rule rather than an exemption: per the OSRS Wiki's Thrall
     * page and Single-way combat page, a thrall attacks the target its owner is
     * attacking and nothing else, generates no aggression of its own, and its
     * damage "counts as damage dealt by the player that summoned it". Nothing
     * about it is a second combatant — it is the owner's damage arriving from a
     * second model — so single-way's one-attacker rule is never actually
     * broken. (Thralls also ignore accuracy and cannot attack players; neither
     * carries over here, and familiars keep rolling their own accuracy.)
     *
     * So the flag selects between two coherent rules, not between correct and
     * lax: off is the pre-EoC rule the port reproduces, on is the thrall rule,
     * and the thrall rule's conditions are enforced either way — content still
     * refuses to let a familiar pick its own target or start a fight its owner
     * is not already in. `npc_combatplayer` is the primitive that makes "my
     * owner is who this npc is fighting" answerable, and without it the on
     * position could not be implemented safely.
     *
     * Default ON, disabled with `TORIRSSERVER_FAMILIAR_SINGLES=0`. Only Summoning
     * content reads it, and that content is compiled out of the ordinary script
     * pack entirely, so "on by default" means "on wherever Summoning is on".
     */
    int familiar_singles_assist;

    /** Non-NULL only under the selftest; see ToriRSServer_CaptureBegin. */
    struct ToriRSServerCapture* capture;

    /**
     * Which revision's bytes this world writes. See torirs_server_wire.h.
     *
     * On the world rather than on the session because a packet is built once
     * and addressed to a player: the encoders reach it as `player->world`, and
     * making it per-session would mean one tick's PLAYER_INFO had to be encoded
     * once per connected revision. Selected at boot by `--rev` / TORIRSSERVER_REV,
     * and NULL is read as revision 230 so nothing that never sets it changes.
     */
    const struct ToriRSServerWire* wire;

    /** Objs on the floor. Flat rather than zone-bucketed: 256 entries scanned
     *  once a tick is nothing, and a zone index would be the only structure in
     *  the mock that has to be kept consistent under a rebuild. */
    struct ToriRSServerGroundObj ground[TORIRSSERVER_GROUND_MAX];

    /**
     * The ground obj the *next* trigger dispatch should make active, as
     * `ToriRSServer_WorldObjHandle` encodes it. 0 means none.
     *
     * One-shot, and set by the dispatch site immediately before it calls
     * `ToriRSServer_ScriptsRunTrigger`. `[opobj<n>]` is the only trigger with an
     * obj subject, so widening the shared `run_trigger` signature — which all
     * nineteen of its call sites would then pass 0 to — buys nothing.
     * `run_trigger_script` consumes and clears it, and the call site clears it
     * again, so a lookup that finds no script cannot leak it into the next
     * trigger. The npc slot rides as a parameter instead only because it
     * predates this.
     */
    intptr_t pending_active_obj;

    /**
     * The npc the *next* trigger dispatch should make the SECONDARY active npc,
     * as `slot + 1`; 0 means none.
     *
     * Same one-shot shape and the same reason as `pending_active_obj` above:
     * `[ai_opnpc<n>]` is the only trigger whose script is about two npcs, so the
     * shared `run_trigger` signature stays as it is and this carries the second
     * one. `run_trigger_script` consumes and clears it.
     */
    int pending_active_npc2;

    /**
     * `trigger_decline`: the script that just ran said the interaction is not
     * its business.
     *
     * Set by the opcode, cleared by the resolver immediately BEFORE it runs each
     * rung and read immediately after, so a script that itself fires another
     * trigger cannot leak a decline outward and a stale one cannot survive into
     * the next dispatch.
     */
    int trigger_declined;

    /**
     * How many chained resolvers are on the stack.
     *
     * `trigger_decline` needs to know whether there is a rung below it to fall
     * to. Called from a `[proc]`, a queue entry or a `[debugproc]` there is no
     * chain at all, so the opcode answers with the message itself rather than
     * leaving the click silently unhandled.
     */
    int trigger_dispatch_depth;

    /**
     * Every rung of the dispatch that just finished declined.
     *
     * The dispatch answers its caller with `TORIRSSERVER_TRIGGER_NONE`, because from
     * the player's side "content looked and said no" and "content binds nothing"
     * are the same event and get the same message. They are NOT the same to
     * `ToriRSServer_ScriptsFallback`, which is the whole reason this is a separate
     * field: an engine fallback stands in for content that is *missing*, and
     * running one here would open a door because content declined to.
     * Set by the resolver, consumed and cleared by `ToriRSServer_ScriptsFallback`,
     * and cleared at the head of every dispatch so it cannot leak.
     */
    int dispatch_declined;

    /**
     * The `find-all then iterate` cursor.
     *
     * `npc_findallany` / `loc_findallzone` / `huntall` fill it and the matching
     * `*_next` walks it, setting the active entity each step so the loop body
     * can read `npc_type` and friends. One cursor, not one per script: the
     * reference has the same single global iterator, and for the same reason
     * there is one script-parking slot per player (§3.10) — two interleaved
     * iterations would walk each other's list. A script that suspends mid-loop
     * and is resumed after another has iterated will see the other's results,
     * which is a real limitation and the reference's too.
     */
    struct
    {
        int slots[TORIRSSERVER_NPC_MAX];
        int count;
        int cursor;
        /** Which `*_next` may read it: SSVM_ENT_NPC, _LOC or _PLAYER. */
        int kind;
    } iterator;

    /**
     * Loc mutations waiting to revert.
     *
     * `loc_change`, `loc_del` and `loc_add` all take a duration in ticks, and
     * every skilling loop in the reference depends on it: a tree becomes a
     * stump for N ticks and then is a tree again, and nothing re-plants it.
     * Without the timer the world is a one-way ratchet — the first player to
     * mine a rock removes it for the session.
     *
     * Reverting is phase 8's job (`zones`), which is where the reference puts
     * loc and obj respawn, and it is a *world* list rather than a player one:
     * the tree is still a stump after the player who cut it logs out.
     *
     * Keyed by `(x, z, level, shape)` — the wire's own key for a loc — rather
     * than by the scene slot it used to hold. A scene slot does not survive a
     * rebuild: `ToriRSServer_SceneBuild` frees the loc array and re-reads it from
     * the cache, so a revert armed before a rebuild used to fire against
     * whatever loc had inherited its index.
     */
    struct ToriRSServerLocRevert
    {
        int active;
        /** Ticks remaining. */
        int delay;
        /** What to put back: -1 means "remove this loc again" (undo a loc_add). */
        int loc_id;
        int shape;
        int angle;
        int x, z, level;
    } loc_reverts[TORIRSSERVER_LOC_REVERT_MAX];

    /**
     * Drops that have left an inventory but have not landed yet.
     *
     * `inv_dropitem_delayed` is the only thing that arms one, and the delay is
     * the point: an arrow that misses is removed from the quiver on the tick it
     * is fired and appears on the floor on the tick it arrives. Dropping it
     * immediately would put it under the target before the projectile got
     * there, which is visible — the pile appears, *then* the arrow flies to it.
     *
     * The obj is described here rather than held as a ground obj with a "not yet
     * visible" flag, because a ground obj is filed in a zone and everything that
     * reads a zone would then need to know about the flag. Nothing exists until
     * the timer fires and `ToriRSServer_WorldObjAdd` is called for real.
     *
     * Drained in phase 8 beside the loc reverts, for the same ordering reason:
     * the reference's `objDelayedQueue` runs in its zone phase, so the drop is
     * on the floor before phase 10 describes the zone that holds it.
     */
    struct ToriRSServerObjDelayed
    {
        int active;
        /** Ticks until it lands. */
        int delay;
        /** Ticks it then lives on the floor, as `ToriRSServer_WorldObjAdd` means it. */
        int duration;
        int obj_id;
        int count;
        int x, z, level;
    } obj_delayed[TORIRSSERVER_OBJ_DELAYED_MAX];

    /** Scripts parked by world_delay, drained by phase 1. */
    struct
    {
        struct SSVM_State* state;
        int delay;
        int active;
    } world_queue[TORIRSSERVER_WORLD_QUEUE_MAX];

    /* Scripts. Opaque so torirs_server.h does not pull the whole VM into every
     * translation unit; owned by torirs_server_scripts.c. */
    struct SSVM_Provider* scripts;
    struct SSVM_Env* script_env;
    /** 0 when no script pack loaded. Every trigger site falls back to its
     *  hardcoded C behaviour, so the mock stays usable without content. */
    int scripts_ok;

    /*
     * Kill-drop credit for the client's loot tracker.
     *
     * While [ai_queue3] runs after a combat death, every public or private
     * ground-object add fires clientscript 7192 (LOOTTRACKER_ADD_LOOT) at the
     * credited player(s). `loot_credit_armed` gates that — bare map/inventory
     * object adds must not attribute loot. Zero-init is idle.
     */
    int loot_credit_armed;
    int loot_credit_npc_type;
    int loot_credit_event_id;
    int loot_credit_seq;
    /** Scripts that showed a death and left the npc alive — see
     *  `ToriRSServerNpc.scripted_death_pending`. Selftest asserts zero. */
    int scripted_death_violations;
    /** Npcs removed at 0 hitpoints whose `death_seq` never reached the wire —
     *  the direct measure of "it vanished instead of dying". Counted in
     *  `ToriRSServer_WorldNpcFree`; selftest asserts zero. */
    int silent_death_removals;
    unsigned char loot_credit_players[TORIRSSERVER_PLAYER_MAX];
};

/* ------------------------------------------------------------------ */
/* World (torirs_server_world.c)                                             */
/* ------------------------------------------------------------------ */

/** Cache the scene reads its map squares from. Set from main() beside the
 *  other cache loaders. */
void
ToriRSServer_WorldSetCacheDir(const char* dir);
const char*
ToriRSServer_WorldCacheDir(void);

/* ------------------------------------------------------------------ */
/* Varbits (torirs_server_varbit.c)                                          */
/* ------------------------------------------------------------------ */

/**
 * A varbit is a named bit range inside a varp, and the range lives in the
 * cache — config group 14, the same records the client reads.
 *
 * The mock needs them because the combat tab is built entirely from two:
 * varbit `combat_weapon_category` (bits 0-5 of varp 843, which the cache calls
 * `randomhitsound`) selects which of the ten button layouts interface 593
 * builds, and `combatlevel_transmit` (bits 24-30 of varp 1105,
 * `wilderness_statistics`) is the number above them. With the category unset,
 * every style button hides and the tab shows nothing but auto-retaliate — which
 * is exactly what it did.
 *
 * Those four names are the cache's, and none of the four is the one you would
 * guess. The varbits were `weapon_category` and `combat_level` here for a
 * while, which are the names of nothing in this cache, and the varps were
 * relabelled to match — which is the mistake this comment is really about. A
 * varp holding a varbit is not that varbit's variable: 1105 also carries
 * `kd_toggle`, `sailing_xp` and `inside_wilderness`. Look the spelling up in
 * `configs/all.varbit` rather than deriving it from what the bit is for.
 *
 * The bit ranges are NOT authored anywhere here. Restating them in a config
 * would be a second source of truth that could disagree with the cache the
 * client reads them from.
 */
int
ToriRSServer_VarbitLoad(const char* cache_dir);
void
ToriRSServer_VarbitFree(void);

/**
 * Number of varp records the active client cache can address.  This is not the
 * highest varp used as a varbit carrier: a cache can contain server-side
 * carrier ids beyond the client's varp array.  Zero means no cache is loaded.
 */
int
ToriRSServer_VarpClientCount(void);

/**
 * The hitsplat id one VIEWER should be sent for a splat somebody dealt.
 *
 * Content names a family — `hitsplat_damage`, `hitsplat_block`,
 * `hitsplat_poison`, `hitsplat_shield` — and this promotes it to the cache
 * WRAPPER that asks the viewer's own All Settings rows:
 *
 *   setting 5   "Hitsplat tinting"    varbit 10236, the me/other pair
 *   setting 279 "Max hit hitsplats"   varbit 14196, the max-hit wrapper
 *   setting 280 "Max hit threshold"   varbit 14195, the floor under it
 *
 * The client resolves the wrapper against the viewer's varbits at draw time
 * (`RS_Hitsplats_ResolveType`), so nothing here reads a setting except the
 * max-hit threshold, which is a comparison against the damage and cannot be
 * expressed as a var selector.
 *
 * A family with no wrapper in this cache — heal, venom, the coloured splats —
 * comes back unchanged, which is the whole of "content decides, this only
 * refines".
 *
 * @param viewer      who is being sent this splat.
 * @param type        what content asked for.
 * @param damage      the number on the splat, for the max-hit test.
 * @param dealer_slot who dealt it, or -1.
 */
int
ToriRSServer_HitsplatForViewer(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* viewer,
    int type,
    int damage,
    int dealer_slot);

/**
 * Is this npc's loot restricted away from this player?
 *
 * The ironman rule, and the whole of what All Settings rows 182 and 183 warn
 * about:
 *
 *   182  Iron loot restriction indicator   varbit 13039
 *   183  Iron loot restriction messages    varbit 13040
 *
 * True when the player is on an ironman account AND somebody else has already
 * damaged the npc. That is the rule this server's own drop model implies rather
 * than one invented here: `death_credit_players` credits everybody still
 * fighting at the killing blow, so an npc another player has engaged is one
 * whose drop an ironman may not have to themselves.
 *
 * False for a non-ironman, always. The rows are Ironman-only by their own
 * wording, and a warning shown to a main is a warning about nothing.
 */
int
ToriRSServer_NpcLootRestrictedFor(
    const struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player);

/**
 * Should this viewer be shown the loot-restriction ICON over this npc?
 *
 * Setting 182, the indicator half of the pair 183 states in the chatbox. True at
 * most once per npc per player — the row's word is "occasionally" and this is
 * asked on every encoded hit — and only when the loot really is restricted and
 * the (inverted) row is on.
 *
 * Answered at ENCODE time rather than where the hit lands, because the icon is
 * one viewer's and an npc's splat list is not: the same list is written once per
 * player watching the fight, and showing everybody a no-entry sign over a
 * creature only one of them may not loot would be worse than showing nobody.
 *
 * Marks the player as shown, so it is not a pure predicate — call it once, in
 * the encoder, and use the answer.
 */
int
ToriRSServer_NpcLootIconWanted(
    const struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* viewer);

/** The hitsplat record that IS the loot-restriction icon, or -1. See the
 *  definition for how it was identified — it is the only record in the cache's
 *  whole hitsplat table that draws a sprite and no number. */
int
ToriRSServer_HitsplatLootRestrictedIcon(void);

/** Read a varbit out of the player's varps. 0 when the id is unknown. */
int
ToriRSServer_VarbitGet(
    const struct ToriRSServerPlayer* player,
    int varbit_id);

/**
 * Write a varbit, patching the bits inside its base varp and marking that varp
 * for transmission. Returns the base varp id, or -1 when the varbit is unknown
 * — which every caller must tolerate: a cache without the record is a cache
 * this content does not fit, not a crash.
 */
int
ToriRSServer_VarbitSet(
    struct ToriRSServer* srv,
    int varbit_id,
    int value);

/** The same write aimed at a NAMED player — what a script broadcasting to a
 *  hunted set needs, since `srv->active_player` is whose turn it is and not who
 *  the script selected. See the definition for the bug this fixes. */
int
ToriRSServer_VarbitSetOn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varbit_id,
    int value);

/** `ToriRSServer_WorldSetVarp` on a named player; side effects stay gated on the
 *  active one. */
void
ToriRSServer_WorldSetVarpOn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varp,
    int value);

/**
 * How many varbits are based on this varp — 0 means writing it whole is safe.
 *
 * The runtime half of docs/LOSTCITY_PORT_TRIAGE.md §7.5. sscompile refuses a
 * whole-varp write to a carrier off `configs/all.varbit`; this is the same fact
 * read out of the cache, for the writers a compiler cannot see — a `::` cheat, a
 * packet handler, C. The two must agree, and they do because both read the
 * `basevar=` key and neither derives anything.
 */
int
ToriRSServer_VarbitCarrierBits(int varp);

/**
 * The highest varp id any cache varbit is based on, or -1 if none is loaded.
 *
 * This, not the varp config group's last file id, is how far the cache's varp
 * namespace actually reaches: a varbit names a varp whether or not the group
 * carries a record for it, and this cache's varbits run twenty ids past the
 * group's end. Anything allocating server varps has to clear this or it lands
 * on packed bits — see `validate_id_bases` and TORIRSSERVER_VARP_CACHE_MAX.
 */
int
ToriRSServer_VarbitMaxBasevar(void);

/** Non-zero while a varbit write is patching its base varp. That write is the
 *  correct way to touch a carrier, so the backstop must not count it. */
int
ToriRSServer_VarbitPatching(void);

/**
 * Whole-varp writes to a carrier varp seen so far, and the last one's varp id.
 *
 * A counter rather than an abort: the engine's job at that point is to keep
 * running and be audited, and the selftest asserts the count is zero. Reset by
 * `ToriRSServer_WorldCarrierWritesReset`.
 */
int
ToriRSServer_WorldCarrierWrites(int* out_last_varp);
void
ToriRSServer_WorldCarrierWritesReset(void);

/** Recompute the two varbits interface 593 builds itself from — the equipped
 *  weapon's category and the player's combat level. Call after anything that
 *  changes either. */
void
ToriRSServer_WorldSyncCombatVarbits(struct ToriRSServer* srv);

/** A varp id by symbol, from content/pack/varp.pack. -1 when unknown, which
 *  every caller must treat as "do not write" — an undeclared varp is not an
 *  error, it is a content tree that does not use that variable. */
int
ToriRSServer_WorldVarp(const char* symbol);

/**
 * The player's attack style, read out of the `com_mode` varp.
 *
 * Not a field on the player: the combat interface writes the style as a varp,
 * content sets the opening one as a varp, and a copy on the side is a second
 * source of truth that can disagree with the one the client is showing. The
 * varp *is* the state; this resolves its id through the content pack so the
 * engine and the scripts name it the same way.
 */
int
ToriRSServer_WorldAttackStyle(const struct ToriRSServer* srv);

/** Set it, and mark it for transmission. */
void
ToriRSServer_WorldSetAttackStyle(
    struct ToriRSServer* srv,
    int style);

/** Write a player variable and queue it for phase 10, skipping the write when
 *  the value is unchanged (a varp that did not change must not be sent — the
 *  client re-runs every script listening on it). */
/*
 * A script wrote a varp directly (SS_OP_POP_VARP), bypassing the setter.
 *
 * `%varp = value` must still trigger whatever engine state hangs off that varp,
 * and it cannot simply call `ToriRSServer_WorldSetVarp`: assignment marks the varp
 * for transmission even when the value is unchanged (the reference's semantics,
 * and what makes `%option_nodef = %option_nodef;` "resync varp" mean anything),
 * while the setter early-returns on an equal write. So the transmission half
 * stays in the opcode and the side-effect half comes through here.
 */
void
ToriRSServer_WorldVarpWritten(
    struct ToriRSServer* srv,
    int varp,
    int value);

void
ToriRSServer_WorldSetVarp(
    struct ToriRSServer* srv,
    int varp,
    int value);

/**
 * Queue a varp for phase 10 without writing it — for a caller that patched
 * `varps[]` itself, which the varbit writers do.
 *
 * Deduping, and loud when the change list is full. The only copy: three
 * subsystems each had one, with different dedupe semantics.
 */
void
ToriRSServer_WorldMarkVarp(
    struct ToriRSServerPlayer* player,
    int varp);

/** The tile a session logs in on, and respawns at. Set from main() before
 *  ToriRSServer_WorldInit; defaults to the Lumbridge castle courtyard. */
void
ToriRSServer_WorldSetHome(
    int tile_x,
    int tile_z);

/**
 * Take a pool slot and hand it its session.
 *
 * **Call before ToriRSServer_WorldLogin**, and before anything encodes: the session
 * exists as soon as the handshake does, the world does not, and an encoder with
 * no player has nowhere to write. `session` may be NULL — a world with no
 * client, which is what the selftest runs.
 *
 * Returns NULL when the pool is full, which a host must treat as "refuse this
 * connection" rather than as a reason to overwrite somebody. The returned player
 * is also left as `srv->active_player`, because everything the caller does next
 * (the login burst, the display name) is that player's.
 */
struct ToriRSServerPlayer*
ToriRSServer_WorldAddPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerSession* session);

/**
 * Release a slot, and take the player out of everyone else's view.
 *
 * The removal is not "stop encoding them": every other client is holding a pid
 * that has to be retired explicitly, or a later player taking the same slot
 * inherits the corpse. `ToriRSServer_SendPlayerInfo` does the retiring; this is
 * what tells it to, via `ToriRSServer_WorldPlayerFree` (see its own comment for
 * why that is not simply clearing `active` inline).
 */
void
ToriRSServer_WorldRemovePlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);

/**
 * The despawn choke point for players — the exact `ToriRSServer_WorldNpcFree`
 * pattern, scaled down to one call site (`ToriRSServer_WorldRemovePlayer` is,
 * per its own doc comment, the only logout path either host has). `active`
 * still clears immediately; what's deferred is the pid's eligibility for
 * `ToriRSServer_WorldAddPlayer`'s free-slot scan, until `ToriRSServer_WorldPlayerReap`
 * drains the queue — once per tick, after every observer's PLAYER_INFO for
 * this tick has already gone out. See docs/torirs_server_npc_slot_reap.md.
 */
void
ToriRSServer_WorldPlayerFree(
    struct ToriRSServer* srv,
    int pid);

/**
 * Once per tick, from phase_cleanup, after every player's PLAYER_INFO for
 * this tick has already gone out: drains `player_free_queue`, clearing
 * `pending_free` on each entry.
 */
void
ToriRSServer_WorldPlayerReap(
    struct ToriRSServer* srv);

/**
 * Say whose turn it is.
 *
 * The single seam for `srv->active_player` — see the file header on why that
 * field is not "the player". Every per-player phase calls this as it iterates,
 * and the session calls it before dispatching a packet, so that a subsystem
 * still reaching through the world reaches the right player rather than
 * whichever one logged in first.
 */
void
ToriRSServer_WorldSetActive(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);

/**
 * Reset one player to a newly-created character, on the home tile.
 *
 * Split out of `ToriRSServer_WorldInit` (which now does the *world*: scene, npcs,
 * ground objs, and runs once) because a second login must not respawn the npc
 * roster or move everyone's scene. Preserves `world`, `session`, `pid` and
 * `active` across the clear, all of which the caller set.
 */
void
ToriRSServer_WorldPlayerInit(struct ToriRSServerPlayer* player);

/**
 * Copy the login name onto the player. **Call after ToriRSServer_WorldPlayerInit**,
 * which memsets the player struct — writing the name before it is what used to
 * make `displayname` report nothing.
 */
void
ToriRSServer_WorldSetDisplayName(
    struct ToriRSServerPlayer* player,
    const char* name);

/**
 * Build the world: the scene at this origin zone, the npc roster and the map
 * squares' ground objs.
 *
 * **Idempotent by design, and it has to be**: both hosts call it on every login
 * because a second player arriving must not respawn the roster, move the scene
 * or return every taken spawn. The second and later calls do nothing and say so
 * under `TORIRSSERVER_VERBOSE`. `ToriRSServer_WorldReset` is what a test uses to get a
 * fresh world deliberately.
 */
void
ToriRSServer_WorldInit(
    struct ToriRSServer* srv,
    int zone_x,
    int zone_z);

/** Drop the world so the next ToriRSServer_WorldInit rebuilds it. For the selftest,
 *  which runs many worlds in one process. */
void
ToriRSServer_WorldReset(struct ToriRSServer* srv);

/** Advance one 600 ms tick: movement, npc roaming, then every packet the tick
 *  produces (rebuild, player info, npc info, container deltas, tick end). */
void
ToriRSServer_WorldTick(struct ToriRSServer* srv);

/*
 * Chambers of Xeric tick harness (torirs_server_cox_sim.c). Enters the raid under
 * `::god`, runs world ticks, and asserts the strategy guide's tick-level
 * timings against a live trace. Returns the number of failed assertions.
 *
 * Lives in its own translation unit because it is a few hundred lines of
 * encounter-specific assertions that have no business growing inside the shared
 * selftest, and because that file is edited by several sessions at once.
 */
int
ToriRSServer_CoxSimRun(struct ToriRSServer* srv);

/**
 * Route one decoded client packet into the game state.
 *
 * `name` is a canonical PKTOUT_NAME_*; `payload`/`len` exclude the opcode and
 * any length prefix. Takes the *player* rather than the world because a packet
 * is something one client said: this is the second half of the encoder pass, on
 * the inbound side, and it is what makes `srv->active_player` correct for the
 * whole of the handler's work.
 */
void
ToriRSServer_WorldHandle(
    struct ToriRSServerPlayer* player,
    int name,
    const uint8_t* payload,
    int len);

/** The on-login burst for one player: scene, gameframe, containers, stats,
 *  first info tick. */
void
ToriRSServer_WorldLogin(struct ToriRSServerPlayer* player);

/**
 * Put `loc_id` on (x, z, level) with this shape, or remove what is there when
 * `loc_id < 0`. Returns 0 when the id is not in the cache or the tile is outside
 * the built scene, in which case nothing changed.
 *
 * This is the one door every runtime loc mutation goes through, and it does
 * three things that have to happen together: move the scene's collision, record
 * the change in the ZoneMap, and queue the zone event. It replaced
 * `ToriRSServer_WorldBroadcastLoc`, which did only the last of the three and did it
 * by walking the player pool — correct for everyone standing there at the time
 * and invisible to everyone else, forever, because a broadcast has no memory.
 *
 * `(x, z, level, shape)` is the key rather than a scene slot because that is
 * what LOC_ADD_CHANGE and LOC_DEL carry, and because a scene slot does not
 * survive a rebuild.
 *
 * `kind` is `loc_add` versus `loc_change`, and the two are not the same
 * mutation even though they can name the same tile. The reference's `loc_add`
 * appends a loc to the zone and leaves the map square's own loc standing
 * (`Zone.addLoc` / `World.addLoc` — neither touches an existing one), where
 * `loc_change` mutates that loc in place (`World.changeLoc` removes its
 * collision and adds the new type's). Collapsing both onto "replace whatever is
 * on this tile" is what made an opened door eat the wall it swung against.
 */
enum ToriRSServerLocSetKind
{
    /** `loc_change`, and every revert: mutate the loc that is on the tile. */
    TORIRSSERVER_LOC_SET_CHANGE = 0,
    /** `loc_add`: a new loc over whatever the map square has here. */
    TORIRSSERVER_LOC_SET_ADD = 1,
};

int
ToriRSServer_WorldLocSet(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind);

/**
 * `ToriRSServer_WorldLocSet` with the placement's own right-click menu attached
 * (struct ToriRSServerLocOps) — the LOC_ADD_CHANGE_V2 opFlags/ops fields.
 *
 * A separate entry point rather than a tenth parameter on the one above,
 * because every existing caller — trees regrowing, crops advancing, the revert
 * queue — places a loc that means exactly what its loctype means, and threading
 * a "no menu" argument through all of them would say the opposite of what those
 * call sites are: they are not declining to override, they have nothing to
 * override. `ToriRSServer_WorldLocSet` is that case spelled once.
 *
 * `ops` is required. It is meaningless on a removal (`loc_id < 0`), which
 * carries no menu on the wire; pass the default there.
 */
int
ToriRSServer_WorldLocSetOps(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind,
    const struct ToriRSServerLocOps* ops);

/** Re-apply every recorded loc change to a scene that has just been rebuilt
 *  from the cache. Without this the server forgets its own doors whenever the
 *  origin moves — which it did, despite a comment claiming otherwise. */
void
ToriRSServer_WorldLocsReapply(struct ToriRSServer* srv);

/** Build the server's collision window for the world's current zone, choosing
 *  the cache or the map-instance descriptors by where that zone is. */
void
ToriRSServer_WorldSceneRebuild(struct ToriRSServer* srv);

/** Re-show an instance whose zones just changed to the players inside it. The
 *  `map_instance_build` half that belongs to the world rather than the registry. */
void
ToriRSServer_WorldMapInstanceBuilt(
    struct ToriRSServer* srv,
    int handle);

/** Release a pooled instance and all world-owned location and floor-object
 *  state in its destination rectangle. Content still owns actor teardown. */
int
ToriRSServer_WorldMapInstanceFree(
    struct ToriRSServer* srv,
    int handle);

/** Put the player on an absolute tile, clearing the walk and rebuilding the
 *  scene if the destination left the current one. */
void
ToriRSServer_WorldTeleport(
    struct ToriRSServer* srv,
    int level,
    int abs_x,
    int abs_z);

/* ------------------------------------------------------------------ */
/* World map (torirs_server_worldmap.c)                                      */
/* ------------------------------------------------------------------ */

/** Arm the orb's and the close button's ops with IF_SETEVENTS. Without this the
 *  client never sends the click — rev 230 has no clickable-by-default. */
void
ToriRSServer_WorldMapLogin(struct ToriRSServer* srv);

/** Mount / unmount interface 595 in the toplevel's floater slot. */
void
ToriRSServer_WorldMapOpen(struct ToriRSServer* srv);
void
ToriRSServer_WorldMapClose(struct ToriRSServer* srv);

/** Claim an IF_BUTTON<op> aimed at the orb or the map's close button. Returns
 *  1 when it was one of those, 0 to let the normal button routing have it. */
int
ToriRSServer_WorldMapHandleButton(
    struct ToriRSServer* srv,
    int uid,
    int op);

/** CLICK_WORLD_MAP: the player clicked a tile on the open map. */
void
ToriRSServer_WorldMapClick(
    struct ToriRSServer* srv,
    int level,
    int abs_x,
    int abs_z);

/** Once per tick: refresh the "you are here" marker while the map is open. */
void
ToriRSServer_WorldMapTick(struct ToriRSServer* srv);

/**
 * Drive the game logic with no socket attached and assert the results.
 *
 * Walking, equipping and slot-dragging are all decided here rather than in the
 * client, so they can be exercised — and regressed — without a client at all.
 * That matters because the rev-230 gameframe cannot currently deliver an
 * inventory item op to a server (see docs/osrs230_mockserver.md), so this is
 * the only thing standing between "the equipment code is written" and "the
 * equipment code works". Returns the number of failures.
 */
int
ToriRSServer_WorldSelftest(void);

/** Absolute tile of scene-local (0,0) for a scene whose origin zone is `zone`.
 *  Entity coordinates in the info streams are relative to this. */
static inline int
ToriRSServer_SceneOrigin(int zone)
{
    return (zone - 6) * 8;
}

/** Direction index for a one-tile step, in the client's World_CoordStep
 *  numbering: 0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE. -1 when (dx, dz) is
 *  not a single tile. */
int
ToriRSServer_StepDirection(
    int dx,
    int dz);

/** The tile delta a direction index moves by — `ToriRSServer_StepDirection`
 *  inverted. (0, 0) for anything outside 0..7. */
void
ToriRSServer_StepDelta(
    int dir,
    int* dx,
    int* dz);

/* ------------------------------------------------------------------ */
/* Sequence names (torirs_server_seqinfo.c)                                  */
/* ------------------------------------------------------------------ */

/**
 * Healthbar widths (config group 33), the denominator a HEADBAR fill is a
 * fraction of. Returns the table size, 0 when the cache has no such group.
 */
int
ToriRSServer_HealthbarInfoLoad(const char* cache_dir);

void
ToriRSServer_HealthbarInfoFree(void);

int
ToriRSServer_HealthbarInfoCount(void);

/** Opcode 14 for a healthbar id, or the client constructor's default. Never 0,
 *  so it is always safe to divide by. */
int
ToriRSServer_HealthbarWidth(int id);

/** class381 var10: the fill denominator when a record states no opcode 14. */
#define TORIRSSERVER_HEALTHBAR_DEFAULT_WIDTH 30

/** Index every sequence's debug name. Returns the count, 0 when absent. */
int
ToriRSServer_SeqInfoLoad(const char* cache_dir);

void
ToriRSServer_SeqInfoFree(void);

/** Sequence id for an exact debug name, or -1. */
int
ToriRSServer_SeqByName(const char* name);

/**
 * A sequence's animation priority — cache opcode 5, `forcedpriority` in the
 * unpacked configs, default 5 for a record that omits it.
 *
 * This is the number `ToriRSServer_AnimPlay_*` compares. It is NOT the record's
 * `priority` field (opcode 10) or its `precedence` (opcode 9), both of which
 * are client-side rendering concerns; the reference's `SeqType.priority`, the
 * one its `playAnimation` gate reads, decodes opcode 5.
 */
int
ToriRSServer_SeqPriority(int seq_id);

/**
 * Whether the LOADED cache's sequence archive actually carries this id.
 *
 * `ToriRSServer_SeqPriority` cannot say: an id past the end of the table answers
 * with the default, which is indistinguishable from a record that really states
 * 5. That matters for content living in a lane cache — a check about a lane
 * animation run against the pristine cache would compare two defaults and read
 * as a genuine result. Callers that mean "skip unless this lane is loaded" ask
 * here first.
 */
int
ToriRSServer_SeqPriorityKnown(int seq_id);

/* ------------------------------------------------------------------ */
/* Animation (torirs_server_combat.c)                                        */
/* ------------------------------------------------------------------ */

/*
 * Play an animation, subject to the priority gate.
 *
 * The reference's rule, from `PathingEntity.playAnimation`: a new sequence
 * replaces the one already queued for this tick only when its priority is
 * greater than or equal to the incumbent's. `anim_id` is cleared to -1 in phase
 * 11 alongside the masks, so the comparison never reaches across ticks.
 *
 * Why it matters here rather than being a nicety: an npc swings in phase 4 and
 * is hit in phase 5, so *every* exchange wrote the attack animation and then
 * overwrote it with the block. Combat looked like it had no attack animation at
 * all — and only for the npcs whose block animation the player's swing actually
 * triggered, which is all of them. goblin_attack_unarmed declares
 * `forcedpriority=6` against goblin_block's default 5 for exactly this reason:
 * the data says a swing outranks a flinch, and nothing was reading it.
 *
 * Returns 1 when the animation was taken.
 */
int
ToriRSServer_AnimPlayNpc(
    struct ToriRSServerNpc* npc,
    int seq_id,
    int delay);

int
ToriRSServer_AnimPlayPlayer(
    struct ToriRSServerPlayer* player,
    int seq_id,
    int delay);

/* ------------------------------------------------------------------ */
/* Combat (torirs_server_combat.c)                                           */
/* ------------------------------------------------------------------ */

/** Is this npc type a valid combat target? Decided by the cache's own menu ops,
 *  the same test the client's minimenu makes. */
int
ToriRSServer_CombatAttackable(int npc_type);

/**
 * Stop fighting, and tell the client to stop facing.
 *
 * The two halves are one action: dropping `combat_target` without sending a
 * FACE_ENTITY of -1 leaves the client turned toward a corpse forever, because
 * the mask is a latch — nothing un-faces an entity except being told to. That
 * is the whole of "facing never clears after clicking away".
 */
void
ToriRSServer_CombatStopPlayer(struct ToriRSServer* srv);

/** The same for a named player, which the npc-death path needs: it has to end
 *  the fight for everyone attacking the corpse, not only for whoever's turn it
 *  is. */
void
ToriRSServer_CombatStopPlayerAt(struct ToriRSServerPlayer* player);
void
ToriRSServer_CombatStopNpc(
    struct ToriRSServer* srv,
    int slot);

/** The player's combat level, by OldSchool's melee formula. Shared by the
 *  aggression check and the combat tab's `combat_level` varbit. */
int
ToriRSServer_CombatLevel(const struct ToriRSServerPlayer* player);

/** Mark a stat as changed so phase 10 flushes it. */
void
ToriRSServer_CombatStatMark(
    struct ToriRSServerPlayer* player,
    int stat);

/** Keep the hitpoints stat and the player's hitpoints in step. Call after any
 *  change to either; they are one number in two places. */
void
ToriRSServer_CombatSyncHitpoints(struct ToriRSServerPlayer* player);

/** Base level for a whole-XP total (LostCity `getLevelByExp`). */
int
ToriRSServer_CombatLevelForXp(int experience);

/** Whole XP at the start of `level` (LostCity `getExpByLevel`). Level 1 is 0. */
int
ToriRSServer_CombatXpForLevel(int level);

/** Set base, boosted, and XP for a skill to a clean level (cheat / ::setlevel). */
void
ToriRSServer_CombatSetLevel(
    struct ToriRSServerPlayer* player,
    int stat,
    int level);

/** Clamp an experience total, in tenths, into [0, TORIRSSERVER_XP_MAX_TENTHS]. Takes
 *  64 bits so a caller can hand it a sum that has already left `int` range. */
int
ToriRSServer_CombatClampXp(long long tenths);

/** Move experience, in tenths of a point. A negative amount takes it away; the
 *  total is clamped either way. Re-levels and marks the stat. */
void
ToriRSServer_CombatAddXp(
    struct ToriRSServer* srv,
    int stat,
    int tenths);

/** Engage an npc in melee: face it, walk beside it, and start swinging. */
void
ToriRSServer_CombatEngage(
    struct ToriRSServer* srv,
    int slot);

/** True once TORIRSSERVER_AFK_COMBAT_TICKS have passed with no player input, which
 *  is when OldSchool stops the character fighting. See its definition. */
int
ToriRSServer_CombatPlayerAfk(const struct ToriRSServerPlayer* player);

/** Apply damage and the hitsplat that carries it. A zero amount is a block
 *  splat, not nothing — otherwise a miss looks like a dropped swing. */
void
ToriRSServer_CombatHitNpc(
    struct ToriRSServer* srv,
    int slot,
    int type,
    int amount);
/** Arm the active NPC's 30-tick poison timer. A stronger existing timer wins;
 * equal severity refreshes its source, matching ContentAPI.applyPoison. */
void
ToriRSServer_CombatPoisonNpc(
    struct ToriRSServer* srv,
    int slot,
    const struct ToriRSServerPlayer* source,
    int severity);
/**
 * The same hit with the ATTACKER named, for the splat the victim is shown.
 *
 * `ToriRSServer_CombatHitPlayer` records dealer -1 ("nobody"), which is right
 * for every hit an npc lands and wrong for player-versus-player: setting 5
 * tints "damage that you did not deal", and a hit from another player is the
 * whole of what that row is about. The plain form cannot answer it, because it
 * runs with the VICTIM as `srv->active_player` -- the attacker is not on the
 * stack by the time it is called.
 *
 * `dealer_slot` is a player pool index, or -1 for damage no player owns.
 */
void
ToriRSServer_CombatHitPlayerFrom(
    struct ToriRSServer* srv,
    int type,
    int amount,
    int dealer_slot);

/** The same hit with no attacker: an npc's swing, poison, a trap. Setting 5
 *  leaves those untinted, which is what "damage that you did not deal" means. */
void
ToriRSServer_CombatHitPlayer(
    struct ToriRSServer* srv,
    int type,
    int amount);
/** Display a player hitsplat without mutating Hitpoints or causing death. */
void
ToriRSServer_CombatHitmarkPlayer(
    struct ToriRSServer* srv,
    int type,
    int amount);
/**
 * Display an NPC hitsplat, and the overhead health bar that rides with it,
 * without mutating Hitpoints or provoking retaliation.
 *
 * The cosmetic twin of `ToriRSServer_CombatHitNpc`, for a splat whose health
 * effect the caller owns separately -- Xarpus absorbing an exhumed's orb shows
 * the heal as a splat and does the healing with `npc_statheal`. Zero is a real
 * amount here and stays the caller's chosen type rather than becoming a block
 * splat: a heal for nothing is still a heal, and the damage path's
 * zero-means-block rule is about a swing that missed.
 */
void
ToriRSServer_CombatHitmarkNpc(
    struct ToriRSServer* srv,
    int slot,
    int type,
    int amount);
void
ToriRSServer_CombatNpcPoisonTick(struct ToriRSServer* srv, int slot);

/**
 * Re-path the player to its combat target, before phase 5 moves it.
 *
 * The reference's `pathToTarget()`, in the reference's position: a step aimed
 * after the move is a step aimed at where the target used to be, and against a
 * target that moves that is enough to stop a fight ever starting.
 */
void
ToriRSServer_CombatPlayerApproach(struct ToriRSServer* srv);

/** Called from tick phases 5, 4 and 1 respectively. */
void
ToriRSServer_CombatPlayerTick(struct ToriRSServer* srv);
void
ToriRSServer_CombatNpcTick(
    struct ToriRSServer* srv,
    int slot);
void
ToriRSServer_CombatRespawnTick(struct ToriRSServer* srv);

/* ------------------------------------------------------------------ */
/* Shared world helpers (torirs_server_world.c)                              */
/* ------------------------------------------------------------------ */

/** Deterministic roll in [lo, hi]. */
int
ToriRSServer_Random(
    struct ToriRSServer* srv,
    int lo,
    int hi);

/** An npc reached zero hitpoints: run its drop table and leave the loot. */
/** Spawn an npc and return its slot, or -1. `npc_add`'s entry point. */
int
ToriRSServer_WorldNpcSpawn(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z,
    int level);

/**
 * The despawn choke point. Every real despawn site calls this instead of
 * writing `npc->active = 0` directly: it still clears `active` immediately
 * (same-tick game logic — `npc_find`, `huntall`, `npc_hastarget` — must keep
 * seeing the npc as gone right away), but defers the slot's eligibility for
 * `npc_spawn`'s free-slot scan by queuing a free command instead of letting
 * it be reused same-tick. See docs/torirs_server_npc_slot_reap.md. Safe to call on
 * an already-inactive slot (no-op).
 */
void
ToriRSServer_WorldNpcFree(
    struct ToriRSServer* srv,
    int slot);

/**
 * Once per tick, from phase_cleanup, after every player's NPC_INFO for this
 * tick has already gone out: drains `npc_free_queue`, clearing `pending_free`
 * on each entry. This is the only place a slot becomes eligible for
 * `npc_spawn`'s scan again.
 */
void
ToriRSServer_WorldNpcReap(
    struct ToriRSServer* srv);

/** Resolve an npc's owner, rejecting a logged-out or reused player slot. */
struct ToriRSServerPlayer*
ToriRSServer_WorldNpcOwner(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc);

/** Whether this player may observe/address an npc. Unowned npcs are public;
 * owned npcs are private to the exact login generation that owns them. */
int
ToriRSServer_WorldNpcVisibleTo(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player);

/** Bind an npc to this exact player login. NULL clears the relation. */
void
ToriRSServer_WorldNpcSetOwner(
    struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player);

/** Add (1) or remove (0) this npc's occupancy flags from the collision map. */
void
ToriRSServer_WorldNpcOccupancy(
    struct ToriRSServerNpc* npc,
    int add);

/**
 * Move an npc without walking it there — `PathingEntity.teleport()`.
 *
 * The one entry point for a discontinuous move, because four things have to
 * happen together and every site that open-coded them forgot at least one: the
 * collision stamp moves with the npc, the route it was walking is abandoned,
 * `step_dir` is cleared (a teleport is not a step, and leaving it set makes the
 * client glide the npc across the map), and `tele` is raised so NPC_INFO
 * re-adds it rather than leaving every client's copy behind. See `tele`.
 */
void
ToriRSServer_WorldNpcTeleport(
    struct ToriRSServerNpc* npc,
    int x,
    int z,
    int level);

/**
 * Queue a single walk destination — `PathingEntity.queueWaypoint()`, which is
 * all `npc_walk` is in the reference (`NpcOps.ts` NPC_WALK). The route is not
 * computed here: the npc phase's greedy stepper advances one tile a tick toward
 * the waypoint, so a script that wants a path re-queues each tick the way
 * `[ai_timer,inferno_moving_safespot]` does.
 *
 * Distinct from `ToriRSServer_WorldNpcWalkTo`, which runs the naive pathfinder
 * *and* takes a step immediately: that is the mode/chase mover, and calling it
 * from a script would take two steps on the tick the script ran.
 */
void
ToriRSServer_WorldNpcQueueWaypoint(
    struct ToriRSServerNpc* npc,
    int x,
    int z);

void
ToriRSServer_WorldNpcDied(
    struct ToriRSServer* srv,
    int slot);

/**
 * The mode a *fresh* npc of this record stands in: patrol when it has a route,
 * else a content `defaultmode=`, else wander when it has a radius and none when
 * it has not.
 *
 * Shared rather than restated, because "what was this npc doing before anything
 * happened to it" is asked at three different moments — spawn, the escape mode
 * giving up, and a death — and the three answers have to agree. They did not:
 * the escape fallback omitted the patrol clause, so Hans, shoved into
 * `playerescape` and stuck, came back a wanderer.
 */
int
ToriRSServer_WorldNpcDefaultMode(const struct ToriRSServerNpc* npc);

/*
 * `Npc.resetDefaults()` — stop whatever standing mode is running and go back to
 * what this record does when nothing is being done to it.
 *
 * One function because the target has to be dropped with the mode. Leaving
 * `mode_target_gen` set behind a `none`/`wander` mode is a dangling aim: the
 * next `npc_setmode(playerface)` from a script with no active player would pick
 * up the previous conversation's partner.
 */
static inline void
ToriRSServer_NpcResetDefaults(struct ToriRSServerNpc* npc)
{
    assert(npc);
    npc->mode = ToriRSServer_WorldNpcDefaultMode(npc);
    npc->mode_target_pid = 0;
    npc->mode_target_gen = 0;
}

/** Bind a player-facing mode to the player it is being held against. A NULL or
 *  logged-out player unbinds, and the mode machine treats that as an invalid
 *  target and resets. */
static inline void
ToriRSServer_NpcSetModeTarget(
    struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    assert(npc);
    if( !player || !player->active )
    {
        npc->mode_target_pid = 0;
        npc->mode_target_gen = 0;
        return;
    }
    npc->mode_target_pid = player->pid;
    npc->mode_target_gen = player->login_generation;
}

/**
 * `Npc.changeType()` — become `type`, and for `duration > 0` become
 * `spawn_type` again that many ticks later.
 *
 * The one door into a transformation, because it is two things that have to
 * happen together: the new record has to be resolved into the live npc (combat
 * definition, footprint, turnspeed, animations) *and* the client has to be told
 * with a CHANGE_TYPE. A caller that set `npc->type` itself would get an npc
 * fighting with the old form's stats, or a client still drawing the old model.
 *
 * `duration <= 0` is permanent, and so is a change to `spawn_type` itself.
 */
void
ToriRSServer_NpcChangeType(
    struct ToriRSServerNpc* npc,
    int type,
    int duration);

/** When a just-appeared npc may first consider roaming, staggered so a room
 *  spawned on one tick does not step in unison. Spawn and respawn both use it. */
void
ToriRSServer_WorldNpcRoamStagger(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc);


/** Drop an obj on the floor. `duration` is ticks, or -1 for a permanent spawn.
 *  Returns the ground slot, or -1 when the floor is full. */
int
ToriRSServer_WorldObjAdd(
    struct ToriRSServer* srv,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration);

/** Drop an obj visible only to `owner` for `private_ticks`, then to
 * everyone. A non-positive private window is the same as obj_add. */
int
ToriRSServer_WorldObjAddPrivate(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* owner,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration,
    int private_ticks);

/** Whether this player may currently see and take the ground-object slot. */
int
ToriRSServer_WorldGroundVisibleTo(
    const struct ToriRSServer* srv,
    int slot,
    int pid);

/** The first active ground obj of `obj_id` on that tile, or -1. */
int
ToriRSServer_WorldGroundFind(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int obj_id);

/**
 * Remove a ground obj and tell every client that can see it.
 *
 * The removal half of the reference's `World.removeObj(obj, duration)`: the
 * zone event is queued while the obj is still filed in its zone (so the packet
 * is addressed to somewhere), the obj is then unfiled (so a client that loads
 * the zone afterwards is sent state that no longer contains it), and a map
 * *spawn* is armed to come back. Every removal path goes through this — the
 * engine's own take, and `obj_del` / `obj_takeitem` from a script.
 */
void
ToriRSServer_WorldGroundTake(
    struct ToriRSServer* srv,
    int slot);

/**
 * How a script holds a ground obj across a suspension.
 *
 * `slot + 1` in the low bits, the slot's `generation` above them, so that a
 * resumed script either finds the obj it was acting on or finds none —
 * `ToriRSServer_WorldGroundSlot` is the other half. Never 0 for a valid slot,
 * because the VM's active-entity pointer uses NULL to mean "no obj".
 *
 * The `+ 1` matches the npc and loc conventions; the generation does not exist
 * for those two and is the difference between a scene slot (whose contents a
 * rebuild replaces wholesale, which `active` already catches) and a 256-entry
 * free list that hands the same index to the next drop.
 */
intptr_t
ToriRSServer_WorldObjHandle(
    struct ToriRSServer* srv,
    int slot);

/** The ground slot a handle names, or -1 when that obj is gone. */
int
ToriRSServer_WorldGroundSlot(
    struct ToriRSServer* srv,
    intptr_t handle);

/** Drop the player's queued route. */
void
ToriRSServer_WorldStepsClear(struct ToriRSServerPlayer* player);

/**
 * Suppress or restore player-originated movement and action input.
 *
 * Locking immediately abandons pathing, interaction, and outgoing combat.
 * It deliberately does not make the player busy: queued scripts/timers and
 * incoming combat keep advancing. Both functions act on `active_player`.
 */
void
ToriRSServer_WorldPlayerLock(struct ToriRSServer* srv);
void
ToriRSServer_WorldPlayerUnlock(struct ToriRSServer* srv);

/**
 * Abandon what a landing stun takes away: the route, the facing, outgoing
 * combat, and the latched interaction.
 *
 * Called by `p_stun` when the stun lands and again on every stunned tick, so
 * an interaction that arrived through some path the inbound gate does not
 * cover cannot sit waiting for the stun to lift. Acts on the player passed,
 * not on `active_player`, because the tick phase already holds one.
 */
void
ToriRSServer_WorldStunInterrupt(struct ToriRSServerPlayer* player);

/** The player's plane changed: drop entity/zone tracking for the old plane so
 *  the new one is FULL_FOLLOWSed. Does **not** queue a scene rebuild — LOC_*
 *  packets carry no plane, so other-level dynamic locs must survive the return
 *  trip (Kronos Inferno flank spawn). Called by `p_teleport` when the level
 *  moved. */
void
ToriRSServer_WorldPlayerLevelChanged(struct ToriRSServerPlayer* player);

/** Queue a route to an absolute destination (ground click). Clears any prior
 *  waypoints and fills from a collision-aware BFS. */
void
ToriRSServer_WorldWalkTo(
    struct ToriRSServer* srv,
    int x,
    int z);

/** Queue a route to a target under an approach predicate (loc/npc/obj). */
void
ToriRSServer_WorldWalkToApproach(
    struct ToriRSServer* srv,
    int x,
    int z,
    struct CollisionApproach const* approach);

/**
 * One tile of an npc's walk toward (target_x, target_z). Returns 1 when it
 * moved.
 *
 * It does NOT route around anything, and that is the contract rather than a
 * shortfall. LostCity builds every `Npc` with `MoveStrategy.NAIVE`
 * (engine/src/engine/entity/Npc.ts:78) and only `MoveStrategy.SMART` reaches
 * the BFS, which no npc ever has — so an npc takes one greedy step, slides
 * along a wall when a diagonal has an open leg, and otherwise stalls. That
 * stall is what safespotting is.
 *
 * This is the reference's `pathToTarget()` + `updateMovement()` at one tile a
 * tick, and the only mover an npc has: the chase, the follow modes and the walk
 * home all go through it. It said "the npc half of `ToriRSServer_SceneRoute`" until
 * 2026-08-08, which stopped being true at e410a84c when the BFS here was
 * replaced by `ToriRSServer_SceneNaivePath`; two selftest checks went on demanding
 * the old behaviour for five days on the strength of this paragraph.
 */
int
ToriRSServer_WorldNpcWalkTo(
    struct ToriRSServerNpc* npc,
    int target_x,
    int target_z);

/**
 * One tile of an npc's walk toward a target under an approach predicate.
 * Used when closing on a player (size-aware).
 */
int
ToriRSServer_WorldNpcWalkToApproach(
    struct ToriRSServerNpc* npc,
    int target_x,
    int target_z,
    struct CollisionApproach const* approach);

/* ------------------------------------------------------------------ */
/* Interactions (torirs_server_world.c)                                      */
/* ------------------------------------------------------------------ */

/** Latch what the player is trying to do. The walk is the caller's; resolving
 *  it is ToriRSServer_WorldProcessInteraction's. */
void
ToriRSServer_WorldInteractionSet(
    struct ToriRSServer* srv,
    enum ToriRSServerInteractionKind kind,
    int op,
    int npc_slot,
    int target_id,
    int tile_x,
    int tile_z,
    int level,
    int size_x,
    int size_z);

/** Abandon it. Anything that means "the player changed their mind" — a ground
 *  click, a teleport, p_stopaction — must call this, or the op fires later when
 *  the player happens to wander back into range. */
void
ToriRSServer_WorldInteractionClear(struct ToriRSServer* srv);

/** The same operation for a player that is not necessarily active_player.
 *  Death and logout cleanup walk the player pool, so routing those through the
 *  active-player-only wrapper can clear the wrong interaction. */
void
ToriRSServer_WorldInteractionClearAt(struct ToriRSServerPlayer* player);

/**
 * Shut whatever modal is up: the chatbox dialogue, the main slot and the side
 * slot, plus the script parked on the dialogue's `p_pausebutton`.
 *
 * The reference's `Player.closeModal()`. `[if_close,<iface>:0]` still gets first
 * refusal on the main slot, which is how the bank closes itself.
 */
void
ToriRSServer_WorldCloseModal(struct ToriRSServer* srv);

/** `Player.closeModal(clearWeakQueue)`. The `false` form has exactly one caller
 *  in the reference and exactly one here: the automatic chat close when a parked
 *  script finishes (`Player.executeScript`), which must not discard a weak queue
 *  the finished script may have just filled. */
void
ToriRSServer_WorldCloseModalEx(
    struct ToriRSServer* srv,
    int clear_weak_queue);

/**
 * "The player changed their mind": drop the pending interaction, the combat
 * target and any open dialogue, but leave the walk queue alone.
 *
 * The reference's `Player.clearPendingAction()`, and it is called from the same
 * places — the walk request and every OP<thing><n> handler, before the new
 * interaction is latched. The walk queue is deliberately untouched because the
 * caller is usually about to install one.
 */
void
ToriRSServer_WorldClearPendingAction(struct ToriRSServer* srv);

/**
 * Resolve the pending interaction if it is already in range, else recover a
 * stalled walk.
 *
 * Packet handlers call this so a click on something adjacent acts immediately.
 * Per-tick chase lives in phase_player (try → pathToPathingTarget → move → try),
 * matching LostCity processInteraction — not a post-move-only pass.
 */
void
ToriRSServer_WorldProcessInteraction(struct ToriRSServer* srv);

/* ------------------------------------------------------------------ */
/* Scripts (torirs_server_scripts.c)                                         */
/* ------------------------------------------------------------------ */

/**
 * Load a compiled script pack.
 *
 * Returns the number of scripts, or 0 when there is no pack — which *is* an
 * error now, and says so in a banner. It used to be a supported mode on the
 * grounds that "every trigger site falls back to the C behaviour it had before
 * scripts existed"; that promise is what `enum ToriRSServerFallback` withdraws. A
 * server with no pack does not answer triggers out of C, because a whole
 * parallel implementation of the game running silently is worse than a server
 * that visibly does nothing.
 *
 * Build the pack with `make -C src torirsserver-scripts`. A fresh checkout has none:
 * the compiler's output is gitignored.
 */
int
ToriRSServer_ScriptsLoad(
    struct ToriRSServer* srv,
    const char* dir);

void
ToriRSServer_ScriptsFree(struct ToriRSServer* srv);

/**
 * List every opcode the loaded content uses that nothing implements, and return
 * how many there are.
 *
 * Runs at load, which is the point: the VM's own complaint arrives only when a
 * player triggers the offending script, and content behind a quest step may
 * never be reached at all. This turns "155 of 396 opcodes are implemented" into
 * "this tree needs these eleven, and here is the first script wanting each" —
 * which is the work queue for moving the remaining C behaviour into content.
 */
int
ToriRSServer_ScriptsReportGaps(struct ToriRSServer* srv);

/**
 * Check every `settimer`/`queue`/`walktrigger` argument in the pack against the
 * script it points at, and report the ones that point at the wrong kind.
 *
 * The compiler resolves those arguments from a *name*, and the script namespace
 * is not the only one that name could belong to — `settimer(poison, 30)` found
 * obj 273 and armed a timer on `[label,woman_im_looking_for_a_lady]`, which then
 * opened a quest's dialogue box on a player nowhere near it. `ssc_compile.c`'s
 * `arg_is_script_name` is the fix and `script_kind_allowed` is the runtime
 * guard; this is the *static* one, and it is the only one of the three that
 * catches a site nobody happens to trigger. Nine of the twenty-three sites at
 * the time of the fix were quest-completion queues that fire once per account.
 *
 * Only fully-constant argument lists can be read (see the definition); the
 * skipped count is printed under `TORIRSSERVER_VERBOSE` so the hole is visible.
 *
 * Returns the number of mismatches; the selftest pins it at 0.
 */
int
ToriRSServer_ScriptsReportScriptIdArgs(struct ToriRSServer* srv);

/**
 * What a trigger dispatch did — three answers where there used to be two.
 *
 * `0` and `1` keep their meanings, so `if( run_trigger(...) )` still reads
 * "content claimed it". What is new is the third: a script that *aborted* no
 * longer answers 0. It used to, and that made a content bug indistinguishable
 * from "content binds nothing here" — the two cases whose difference is the
 * entire point of inverting the fallback. One is a gap the engine may stand in
 * for; the other is a defect the engine must not paper over.
 */
enum ToriRSServerTriggerResult
{
    /** Nothing bound at any rung — type, category, or the `_` wildcard. */
    TORIRSSERVER_TRIGGER_NONE = 0,
    /** A script ran, and either finished or parked. */
    TORIRSSERVER_TRIGGER_RAN = 1,
    /** A script was bound and did not complete: it aborted, declared arguments
     *  a trigger cannot supply, or had nowhere to park. */
    TORIRSSERVER_TRIGGER_FAILED = 2
};

/**
 * The engine behaviours that still answer a trigger nothing is bound to.
 *
 * Every one is C standing in for something LostCity states in content, and every
 * one is here only because the ServerScript surface cannot yet say it — the
 * ~3,200 lines `osrs230_mockserver.md` §6.1 step 5 calls blocked. They are
 * *enumerated* rather than written inline at their call sites so that the set is
 * countable: `ToriRSServer_ScriptsReportFallbacks` names them at boot, and the
 * selftest pins how many there are. One added quietly is the failure this enum
 * exists to prevent, in the same spirit as the ten named hooks in
 * `torirs_server_scripts.c` (PORTING_GUIDE §2.4 item 5).
 *
 * Adding to this list is not a design choice available to a content port. The
 * order is: widen the opcode surface until a script can say it, move it, delete
 * the entry.
 *
 * **A row's `blocked_on` is as much of the row as its name is, and it decays.**
 * The rows are only useful if somebody reading the boot log can tell what is
 * left; a reason that expired reads exactly like one that has not, so it is
 * worse than an empty one. This happened: `[ai_queue3]` printed "drop tables
 * need npc categories" at every boot for two stages after categories, the
 * category rung and 69 drop-table files had all landed. Two rules came out of
 * it, and both are enforced rather than advised:
 *
 * - Each string cites something **checkable in one command** — a symbol as its
 *   header spells it, a `file:line`, a `wc -l`, a reference path. Where the
 *   blocker is partly cleared the string says which part, because "NOT the
 *   loc_* family, both landed" is the sentence that stops the next reader
 *   re-deriving it.
 * - The opcode-shaped half of each blocker is **machine-checked**:
 *   `ToriRSServer_ScriptsStaleBlockers` fails the moment a cited opcode is
 *   implemented. Before touching a row, re-check its blocker against the tree;
 *   assume nothing in it is still true.
 */
enum ToriRSServerFallback
{
    /** `[opnpc<n>]` → "Attack" engages combat, anything else greets
     *  (`interaction_engine_npc`; the greeting itself is content already). */
    TORIRSSERVER_FALLBACK_OPNPC = 0,
    /** `[inv_button<n>]` on a bank component → the bank's own router, reached
     *  through the quantity ladder `ToriRSServer_BankQuantityForOp`. */
    TORIRSSERVER_FALLBACK_INV_BUTTON,
    /** `[if_button]` → the bank's settings/deposit router,
     *  `ToriRSServer_BankHandleButton`. */
    TORIRSSERVER_FALLBACK_IF_BUTTON,
    /*
     * `[if_close]` is deliberately not here. It was, in the sense that the
     * engine's unmount ran only when no script was bound — and that is not what
     * the trigger means. `Player.closeModal` runs the close script *and then*
     * unmounts unconditionally; an interface the player closed closes. See
     * `ToriRSServer_WorldCloseModal`.
     */
    /*
     * `[ai_queue3]` was here — "the npc's `death_drop` param", what an npc with
     * no bound drop table left behind. It is the first row this list has lost.
     *
     * Worth reading beside the `[if_close]` note above, because they are
     * different failures. That one was never a fallback. This one was a real
     * one whose *blocker* (npc categories) had been cleared two stages before
     * the row went, while `k_engine_fallbacks[]` still printed "drop tables
     * need npc categories" at every boot — which is the failure mode the
     * `blocked_on` text exists to make visible and did not, because a
     * stale-but-plausible reason reads exactly like a live one.
     *
     * The behaviour is `[ai_queue3,_]` in skill_combat/npc_combat.rs2, which is
     * where the reference puts it; the selftest section "the death drop is
     * content's" is what proves it moved rather than vanished.
     */
    /*
     * `[oploc<n>]` was here — doors, bank booths, stairs and ladders,
     * `interaction_engine_loc` (84 lines) plus `climb` (34). Second row lost,
     * 2026-08-02, and the one that took the most to lose: a `SSVM_ENT_LOC`
     * binding, a loc category rung, four opcodes and two content generators
     * across three stages. `torirs_server_world.c` says what each half became.
     *
     * The thing worth carrying forward is that its *last* blocker was not any of
     * those. Doors and ladders had been content and unreachable in C for a day
     * while the row stood, because 77 of the 78 loc records whose cache menu
     * says "Bank" had nothing bound to them and the C reached all 78 for free.
     * A row can be one unglamorous list away from going.
     */
    TORIRSSERVER_FALLBACK_COUNT
};

/**
 * Run the script bound to a trigger, resolving it the way the reference does.
 *
 * `ScriptProvider.getByTrigger`: the exact `type`, then the `category`, then the
 * bare `_` wildcard, and nothing after that. Pass -1 for a subject that does not
 * apply. Returns an `enum ToriRSServerTriggerResult`.
 *
 * `npc_slot` is the npc the trigger is about, or -1. It becomes the script's
 * active npc, which is what `npc_say` and friends operate on.
 *
 * A miss reports itself under `TORIRSSERVER_VERBOSE`, in the reference's own words
 * (`no trigger for [opnpc2,goblin]`), because a trigger that does nothing is now
 * the *designed* outcome and is otherwise indistinguishable from a dropped
 * packet.
 */
int
ToriRSServer_ScriptsRunTrigger(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot);

/** Trigger dispatch that also states the script's `last_int`. The npc queue is
 *  the only caller: `npc_queue(<n>, $arg, $delay)` carries a value and a queued
 *  npc script has no player whose `last_int` could stand in for it, so without
 *  this every `[ai_queue<n>]` in the tree read 0. Reference: `Npc.ts`
 *  `state.lastInt = request.lastInt`. */
int
ToriRSServer_ScriptsRunTriggerLastint(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int32_t last_int);

/** Trigger dispatch that also arms a SECONDARY active npc — the thing the
 *  subject npc is acting on. `[ai_opnpc<n>,<attacker>]` is the only caller:
 *  npc-versus-npc combat is the one engine event whose script needs to name two
 *  npcs, and `.npc_` is how a script addresses the second one. */
int
ToriRSServer_ScriptsRunTriggerNpc2(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int npc2_slot);

/** Trigger dispatch with string arguments (friend login/logout display name). */
int
ToriRSServer_ScriptsRunTriggerSv(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    const char* const* strv,
    int strc);

/**
 * The same, for a trigger whose subject is a **loc**: the scene slot becomes the
 * script's active loc.
 *
 * A sibling rather than a sixth parameter on the call above, and the reason is
 * merge cost rather than taste — `ToriRSServer_ScriptsRunTrigger` has nineteen call
 * sites and exactly two of them are about a loc.
 *
 * Why it exists at all: `SSVM_ENT_LOC` had three writers in the whole tree before
 * 2026-08-02, all of them inside the VM (`loc_find`, `loc_add`, the iterator), so
 * every `[oploc<n>]`/`[aploc<n>]` script started with no active loc and the first
 * `loc_coord`/`loc_param`/`loc_change` in it aborted. `handle_oploc` had computed
 * the slot and thrown it away since the day it was written. That — not the
 * `loc_*` opcode family, which landed long ago — is what the `oploc` fallback row
 * was waiting on.
 *
 * -1 for "no loc", and a slot whose scene entry is gone is treated as -1 rather
 * than as an error: the interaction resolves a tick or more after the click, and
 * a loc another player already changed is an ordinary race, not a bug.
 */
int
ToriRSServer_ScriptsRunTriggerOnLoc(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int loc_slot);

/**
 * Unpark and free one script state, wherever it is parked.
 *
 * The logout path's only caller: a `[logout]` script that suspends has parked
 * itself on a player whose slot is about to be reused, and the reference's
 * answer (defer the logout until `canAccess()`) is not available to a socket
 * that has already closed.
 */
void
ToriRSServer_ScriptsReleaseState(
    struct ToriRSServer* srv,
    struct SSVM_State* state);

/**
 * Run a script by id on the active player. The walktrigger's firing path; see
 * the definition for why it is not a general script-call hook.
 */
void
ToriRSServer_ScriptsRunScriptId(struct ToriRSServer* srv, int script_id);

/**
 * One rung, no chain — `ScriptProvider.getByTriggerSpecific`.
 *
 * `type` if it is not -1, else `category` if it is not -1, else the global form.
 * The reference uses this where a wildcard would be actively wrong: `[login,_]`
 * is global by construction, and an `[if_button,_]` that swallowed every click
 * on every interface is not a fallback anyone wants.
 */
int
ToriRSServer_ScriptsRunTriggerSpecific(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot);

/**
 * A targeted cast's trigger — the `t` family, keyed by the SPELL's component
 * uid rather than by what it was aimed at.
 *
 * By key then by name, because a spell's uid is above `ssc_compile.c`'s
 * `1 << 21` ceiling and every spell trigger in the tree therefore compiled
 * name-addressed. See the definition.
 *
 * `loc_slot` is the loc the cast was aimed at, or -1. Being keyed by the spell
 * says which script runs; the target still has to be the script's active entity,
 * or `[aploct,<spell>]` cannot say anything about the loc it was aimed at. The
 * ground obj rides in on `srv->pending_active_obj`, the same one-shot the
 * `[opobj<n>]` arm uses.
 */
int
ToriRSServer_ScriptsRunSpellTrigger(
    struct ToriRSServer* srv,
    int trigger,
    int spell_component,
    int npc_slot,
    int player_slot,
    int loc_slot);

/**
 * May this call site run its engine fallback for `result`?
 *
 * 1 only when `result` is `TORIRSSERVER_TRIGGER_NONE` *and* a script pack is loaded.
 * Both halves are the inversion:
 *
 * - A `TORIRSSERVER_TRIGGER_FAILED` is not a gap in content, it is a bug in content.
 *   Running the C body would hide it behind a plausible-looking game.
 * - With no pack at all, nothing is a gap because everything is. A server that
 *   answered every trigger out of C would be a second implementation of the
 *   game, silently different from the one the content tree describes, and the
 *   only way to notice would be to find a behaviour that disagreed. It says so
 *   at boot and then does nothing.
 *
 * Reports which fallback ran, and why one did not, under `TORIRSSERVER_VERBOSE`.
 */
int
ToriRSServer_ScriptsFallback(
    struct ToriRSServer* srv,
    enum ToriRSServerFallback which,
    int result);

/** Name every live engine fallback and what it is blocked on. Returns the
 *  count, which is `TORIRSSERVER_FALLBACK_COUNT` and shrinks as content grows.
 *  Also runs `ToriRSServer_ScriptsStaleBlockers` and prints any hit. */
int
ToriRSServer_ScriptsReportFallbacks(struct ToriRSServer* srv);

/**
 * How many fallback rows name an opcode that is implemented now — 0, always.
 *
 * The list's failure mode is not a wrong row, it is a row that *stops* being
 * right without changing. `[ai_queue3]` printed "drop tables need npc
 * categories" at every boot for two stages after categories landed; a stale
 * reason and a live one are indistinguishable by reading, which is the entire
 * problem. So each row's blocker names its opcodes in a form the machine can
 * check, and this is the check: an opcode a row is waiting for, that has
 * arrived, means the row can go or its reason has to be rewritten.
 *
 * It only catches opcode-shaped blockers, which is most of them but not all —
 * `opnpc` waits on a volume of C and `if_button` on two component lists that
 * disagree. Those are cited in the text with the command that settles them.
 */
int
ToriRSServer_ScriptsStaleBlockers(void);

/**
 * The cache menu verb this engine answers itself for `[<trigger>,<subject>]`,
 * or NULL if it answers none.
 *
 * The inverse question to `ToriRSServer_ScriptsFallback`'s. That one asks "did
 * content claim this at runtime"; this asks, of a binding, "was there an engine
 * behaviour here to claim" — which is answerable at load, before any player has
 * clicked anything. `subject` is an exact type id; a category or wildcard
 * binding names no record and gets NULL.
 */
const char*
ToriRSServer_WorldEngineClaimedVerb(
    int trigger,
    int32_t subject);

/** Name every trigger content binds over a verb the engine answers itself and
 *  does not re-issue. Returns the count; 0 is the state to keep. Triage §7.7. */
int
ToriRSServer_ScriptsReportShadowedOps(struct ToriRSServer* srv);

/**
 * Dispatch a click on component `uid`, op `op_num`, to its `[if_button*]` script.
 *
 * Four lookups, in order: `[if_button<n>,<uid>]` by key, `[if_button<n>,<name>]`
 * by name, then the same two for the unnumbered `[if_button,...]`.
 *
 * Both spellings, because a rev-230 component uid is `(interface << 16) | child`
 * and the compiled trigger key has 21 bits for its subject — enough for
 * interface 12's bank panel, not for interface 160's orbs. Those scripts compile
 * name-addressed (see ssc_compile.c), so the engine asks by key first and by name
 * second. The name comes from the same component pack the compiler read, so the
 * two cannot drift.
 *
 * Numbered first, because a rev-230 component carries up to ten ops and the
 * packet says which one was clicked: `stats:attack` has "Toggle Attack XP" on op
 * 1 and "View Attack guide" on op 2, and there is no way to distinguish them
 * from `[if_button,stats:attack]`. `op_num` outside 1..10 skips that rung.
 *
 * Every key lookup is `getByTriggerSpecific`, matching `IfButtonHandler`: an
 * interface button has no category and must not fall through to a wildcard.
 */
int
ToriRSServer_ScriptsRunIfButton(
    struct ToriRSServer* srv,
    int uid,
    int op_num);

/**
 * Dispatch a coordinate-subject trigger — the `zone`/`mapzone` family — by name.
 *
 * These four are the only triggers whose subject is a *place*, and a place is
 * not a type id: the reference formats `[zone,<level>_<mx>_<mz>_<lx>_<lz>]` and
 * `[mapzone,0_<mx>_<mz>]` and asks `ScriptProvider.getByName`
 * (`NetworkPlayer.updateMap` → `Player.ts`). `ToriRSServer_ScriptsRunTrigger`
 * takes an integer subject and cannot express that, which is the whole reason
 * this exists — the same reason `ToriRSServer_ScriptsRunIfButton` has a
 * name-addressed rung.
 *
 * It differs from that one in taking *no* keyed rung, and that is a correctness
 * requirement rather than an optimisation: a 5-part coord packs to a subject far
 * past the compiled key's 21 bits. Measured over the reference's 427 of them, 78
 * compile to a negative key (which `ssvm_provider.c` deliberately keeps out of
 * the index) and 349 to a wrapped one no runtime lookup could reproduce. Name is
 * the only address these have, and `ssc_compile.c` writes -1 for a coord subject
 * so that is true by construction.
 *
 * Components, not a packed coord, because the two granularities disagree about
 * level: `mapzone` packs it as a literal 0 (`CoordGrid.packCoord(0, …)`), so a
 * climb inside one map square does not re-fire it. Passing one packed number
 * would make the two indistinguishable here.
 *
 * A miss is **silent**. Every tile in the world is a miss for at least three of
 * the four, which is the `[ai_spawn]`-across-2,197-npcs argument
 * (osrs230_mockserver.md §3.18) — so this does not borrow
 * `trigger_is_player_initiated`'s report.
 */
int
ToriRSServer_ScriptsRunTriggerAt(
    struct ToriRSServer* srv,
    int trigger,
    int level,
    int x,
    int z);

/**
 * The same lookup, but *enqueued* on the engine queue rather than run.
 *
 * This is what the tick uses, and `run_trigger_at` above is what the selftest
 * uses. The split is the reference's: `triggerZone` and its three siblings all
 * end in `enqueueScript(trigger, PlayerQueueType.ENGINE)`, never a direct call.
 *
 * Two reasons it has to be a queue, both of which a direct call gets wrong in a
 * way that looks like a content bug:
 *
 *  - detection happens in phase 10, after PLAYER_INFO has been encoded, so a
 *    zone script that teleports would move a player the client has already been
 *    told is somewhere else. The drain is phase 5 of the following tick.
 *  - `run_or_park` allows one parked script per player, so a zone crossed
 *    mid-dialogue would have its script *refused* with a message rather than
 *    held. The engine queue's `canAccess()` gate holds it instead.
 *
 * Returns TORIRSSERVER_TRIGGER_RAN when a script was found and queued (it has not
 * run yet), TORIRSSERVER_TRIGGER_NONE when nothing is bound — the same silent miss.
 */
int
ToriRSServer_ScriptsQueueTriggerAt(
    struct ToriRSServer* srv,
    int trigger,
    int level,
    int x,
    int z);

/**
 * Dispatch "use item A on item B" — `[opheldu,…]` — over its four lookup rungs.
 *
 * The one member of the use-on family that is not an interaction: there is
 * nothing to walk to, so `OpHeldUHandler` resolves and runs it in the packet
 * handler. It is also the only trigger in the engine whose lookup **mutates the
 * player while it searches**, which is why it cannot be expressed as a call to
 * `ToriRSServer_ScriptsRunTrigger` (that resolves one `(type, category)` pair).
 *
 * The rungs, in the reference's order, none of them chaining to `_`:
 *
 *   1. `(OPHELDU, b.id, -1)`        — the item that was *clicked*
 *   2. `(OPHELDU, a.id, -1)`        — the item that was *dragged*, then SWAP
 *   3. `(OPHELDU, -1, b.category)`
 *   4. `(OPHELDU, -1, a.category)`  — then SWAP again
 *
 * **The swap is the contract, not an implementation detail.** It exchanges
 * `last_item`↔`last_useitem` and `last_slot`↔`last_useslot`, so that a script
 * bound to one of the two items always finds *itself* in `last_item` and the
 * other item in `last_useitem` — whichever order the player clicked them in.
 * Content relies on it: the reference's `[opheldu,ball_of_wool]` and
 * `[opheldu,unstrung_sapphire_amulet]` are both written reading `last_useitem`,
 * and only one of them can be the clicked item on any given click.
 *
 * **Rung 2's swap is outside its null check, and that is deliberate.** After a
 * failed rung 2 the state is left swapped, so a rung-3 (clicked item's
 * *category*) hit runs with `last_item` naming the *other* item — an inversion
 * relative to rungs 1 and 2. It is the reference's behaviour, 53 category
 * bindings in the reference tree observe it, and content is written against what
 * it observes. Ported deliberately; the selftest pins it so that "fixing" it
 * goes red.
 *
 * Categories are passed in rather than looked up here for the same reason the
 * rest of the dispatch takes them: the obj record lives on the world side.
 * Pass -1 for an obj with no category. Returns `enum ToriRSServerTriggerResult`; a
 * miss is the caller's to answer, and the answer is content's
 * `[proc,nothing_interesting_message]` — never an engine fallback, because
 * there is no engine use-on behaviour for one to fall back to.
 */
int
ToriRSServer_ScriptsRunOpheldu(
    struct ToriRSServer* srv,
    int obj_type,
    int obj_category,
    int use_obj_type,
    int use_obj_category);

/**
 * Run a `::command` as `[debugproc,<name>]`, with the words after it as its
 * declared arguments. Returns `enum ToriRSServerTriggerResult`: NONE when there is
 * no matching debugproc, RAN when it completed or parked, and FAILED when a
 * matching debugproc aborted.  Keeping FAILED distinct prevents a broken
 * command from falling through as though it never existed.
 *
 * The reference's own arrangement — see ClientCheatHandler — and the reason a
 * cheat need not be an engine change. `::pray 18` reaches Protect from Melee
 * through the same `~prayer_toggle` the button does.
 */
int
ToriRSServer_ScriptsRunDebugproc(
    struct ToriRSServer* srv,
    const char* line);

/** Resume anything parked whose wait is over. Called by tick phases 1, 4 and 5. */
void
ToriRSServer_ScriptsResumeWorld(struct ToriRSServer* srv);
void
ToriRSServer_ScriptsResumeNpc(
    struct ToriRSServer* srv,
    int slot);
void
ToriRSServer_ScriptsResumePlayer(struct ToriRSServer* srv);

/**
 * Run due queue entries and tick the timers, on `srv->active_player`.
 *
 * Phase 5's own order, which is the reference's (`World.processPlayers`):
 * resume, queues (strong-close, normal, weak), then timers (normal, then soft).
 *
 * Both are gated on the reference's `canAccess()` — see `player_can_access`.
 * That gate is the difference between a queued script and an immediate one, and
 * without it a `[queue]` armed by a dialogue ran *through* the dialogue.
 */
void
ToriRSServer_ScriptsProcessQueues(struct ToriRSServer* srv);
void
ToriRSServer_ScriptsProcessTimers(struct ToriRSServer* srv);
/**
 * Fire the player's armed walktrigger if any (LostCity `processWalktrigger`).
 * Called by advance_player immediately before each planned tile; the candidate
 * is exposed as WALKSTEP_COORD for that call and cleared afterwards.
 */
void
ToriRSServer_ScriptsProcessWalktrigger(struct ToriRSServer* srv);
/**
 * Drain the engine queue — the zone family — on `srv->active_player`.
 *
 * Runs *after* the timers, which is where `World.processPlayers` calls
 * `processEngineQueue()`. Entries carry delay 0, so an entry queued by phase 10
 * of tick N runs in phase 5 of tick N+1 unless the player is busy, in which case
 * it waits rather than being dropped.
 */
void
ToriRSServer_ScriptsProcessEngineQueue(struct ToriRSServer* srv);

/** Discard every WEAK queue entry. Called by `ToriRSServer_WorldCloseModal`, which
 *  is the only thing that clears them (`Player.closeModal`). */
void
ToriRSServer_ScriptsClearWeakQueue(struct ToriRSServerPlayer* player);

/**
 * Release a p_pausebutton wait.
 *
 * Returns 1 when the click matched a registered resume button and the script
 * continued, 0 otherwise — an unmatched click must leave the script parked.
 */
int
ToriRSServer_ScriptsResumeButton(
    struct ToriRSServer* srv,
    int component_uid);

/**
 * Release a p_countdialog wait with the number the client sent.
 *
 * Separate from ToriRSServer_ScriptsResumeButton: the two waits are released by
 * different packets and neither may release the other — a click arriving while
 * a count dialog is up must leave the script parked.
 */
int
ToriRSServer_ScriptsResumeCountdialog(
    struct ToriRSServer* srv,
    int32_t value);

/** Release a p_namedialog wait with the decoded, non-NUL reply bytes. */
int
ToriRSServer_ScriptsResumeNamedialog(
    struct ToriRSServer* srv,
    const uint8_t* text,
    int len);

/**
 * End a script parked on a dialogue, because the interface it is blocked on is
 * being taken away. Returns 1 when one was actually discarded.
 *
 * Only a `p_pausebutton`, `p_countdialog`, or `p_namedialog` wait: these are
 * the waits whose
 * only way forward is a click on an interface that is about to stop existing,
 * so leaving them parked would wedge the player's single script slot until
 * logout. A `p_delay` wait survives — its clock is still running, and the
 * reference draws the line in the same place (`Player.closeModal()`).
 *
 * The execution test is also what makes this safe to call from a host command:
 * a script that is *running* reads SSVM_RUNNING, never one of these two, so it
 * can never free the state under its own feet.
 */
int
ToriRSServer_ScriptsCloseDialogue(struct ToriRSServer* srv);

/** Start a script by id on behalf of the player. For the selftest and for
 *  anything the engine reaches by id rather than by trigger. Returns 1 when a
 *  script ran or parked. */
int
ToriRSServer_ScriptsRunScript(
    struct ToriRSServer* srv,
    int script_id);

/** Run a named content proc immediately with int arguments (`name` includes the
 *  brackets, e.g. "[proc,give_combat_experience]"). The seam for keeping policy
 *  the reference expresses as a proc in content instead of as a C switch.
 *  Returns 1 when the proc ran; 0 for a missing script or an arity mismatch. */
int
ToriRSServer_ScriptsRunProc(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc);

/** Run a named proc with an npc made active, so `npc_stat`/`npc_param`/
 *  `npc_damage` inside it resolve to that npc. The combat swing needs it. */
int
ToriRSServer_ScriptsRunProcOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot);

/** Named NPC-context proc with integer arguments, for deterministic encounter
 * branches whose public trigger normally supplies the values indirectly. */
int
ToriRSServer_ScriptsRunProcArgsOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    const int32_t* args,
    int argc);

/** Value-returning counterpart for rules that need both the active player and
 * an active NPC (target-selection/caps in focused encounter tests). */
int
ToriRSServer_ScriptsRunProcIntOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    const int32_t* args,
    int argc,
    int32_t* out);

/** Say a content-owned message: runs `[proc,<name>]`, optionally with one
 *  string argument (an obj or skill name the engine had to look up). Silent
 *  when content does not define it — never a C fallback, which is how two
 *  copies of a sentence come to disagree. */
void
ToriRSServer_Say(
    struct ToriRSServer* srv,
    const char* name,
    const char* arg);

/** As above, with string arguments too — for a message content should word but
 *  only the engine knows a name for. `strv` entries are copied by the VM's
 *  string pool, so a caller may pass a stack buffer. */
int
ToriRSServer_ScriptsRunProcSv(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc);

/** Value-returning, with string arguments. */
int
ToriRSServer_ScriptsRunProcIntSv(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out);

/** Run a named content proc and read one int back off its stack. For rules the
 *  reference states as a value-returning `[proc,...]` — a priority chain, a
 *  table lookup — which cannot be a single param read. Returns 1 and writes
 *  *out when the proc answered; 0 for a missing script, a bad arity, an abort
 *  or a suspend, in which case the caller keeps its own default. */
int
ToriRSServer_ScriptsRunProcInt(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    int32_t* out);

/**
 * Ask content whether it wants to consume an interaction, before the engine
 * dispatches it.
 *
 * `name` is a `[proc,…]` that takes `args` and returns a boolean. It runs with
 * the clicked npc or loc bound as the primary active entity, so it can answer
 * in dialogue rather than in a floating message. Returns 1 if the proc ran and
 * `out` holds its answer; 0 if the tree does not define it, if it parked, or if
 * it aborted — all of which mean "no claim".
 *
 * See the definition in torirs_server_scripts.c for why this is not a trigger.
 */
int
ToriRSServer_ScriptsRunClaim(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    int loc_slot,
    const int32_t* args,
    int argc,
    int32_t* out);

/** Queue a named script (`name` includes the brackets, e.g.
 *  "[queue,playerhit_n_retaliate]") on the player, `delay` ticks out, carrying
 *  one int argument. Lets the engine start an exchange content finishes.
 *  Returns 1 when queued; 0 for a missing script or a full queue. */
int
ToriRSServer_ScriptsQueueNamed(
    struct ToriRSServer* srv,
    const char* name,
    int delay,
    int32_t arg);

/* ------------------------------------------------------------------ */
/* Script primitives (torirs_server_scripts.c)                               */
/* ------------------------------------------------------------------ */

/*
 * Run a resolved Script* — the building block for `run_proc*`, `queue_named`,
 * and trigger dispatch. A NULL script is a no-op returning 0.
 */
int
ToriRSServer_ScriptsRunHook(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc);

int
ToriRSServer_ScriptsRunHookSv(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc);

int
ToriRSServer_ScriptsRunHookInt(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    int32_t* out);

int
ToriRSServer_ScriptsRunHookIntSv(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out);

int
ToriRSServer_ScriptsRunHookOnNpc(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot);

int
ToriRSServer_ScriptsQueueHook(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int delay,
    int32_t arg);

/** The host command seam: every opcode the VM does not implement itself. */
int
ToriRSServer_ScriptCommand(
    struct SSVM_State* state,
    int opcode,
    int dot);

/*
 * The active-loc handle, both kinds (torirs_server_scripts.c).
 *
 * Positive is `scene slot + 1`; negative names a ZoneMap record by key, which
 * is how a script addresses a loc the scene window does not cover — the
 * reference's `World.getLoc` reaches every zone and content leans on that.
 * `ToriRSServer_ScriptLocResolve` decodes either, re-validating against the live
 * scene / ZoneMap, and returns NULL when the loc is gone. A zone-backed result
 * is a borrowed view valid until the next resolve; consumers copy what they
 * need, and mutations re-key on coordinates anyway.
 */
struct ToriRSServerSceneLoc*
ToriRSServer_ScriptLocResolve(
    struct ToriRSServer* srv,
    void* handle_ptr);

/** A handle for the ZoneMap record at this key, for SSVM_SetActive. */
void*
ToriRSServer_ScriptZoneLocHandle(
    int x,
    int z,
    int level,
    int shape);

/*
 * Per-domain opcode handlers (`ToriRSServer_Ops_*.c`).
 *
 * `ToriRSServer_ScriptCommand` offers each the opcode in turn; each returns 1 when it
 * handled it and 0 to pass. That is the same "1 means handled" contract the VM's
 * host callback already uses, so a domain file is a host callback in miniature
 * rather than a new mechanism — and `gen_opcode_coverage.py` globs these files, so
 * adding one cannot silently under-report coverage.
 *
 * The split is docs/osrs230_mockserver.md §6.1 step 2's remaining half. It is
 * being done by *adding* domains rather than by moving the existing switch, so
 * every step stays verifiable against a green selftest.
 */
int
ToriRSServer_OpsDb(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `*_param` family. See torirs_server_ops_param.c. */
int
ToriRSServer_OpsParam(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `loc_*` / `lc_*` config reads. See torirs_server_ops_loc.c. The loc family's
 *  *mutating* half stays in torirs_server_scripts.c's switch, with the scene and the
 *  revert queue it needs. */
int
ToriRSServer_OpsLoc(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `npc_*` / `nc_*` config reads and the hunt iterators. See
 *  torirs_server_ops_npc.c. Addressing, lifecycle and the mode machine stay in
 *  torirs_server_scripts.c's switch, with the world state they mutate. */
int
ToriRSServer_OpsNpc(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `obj_*` family — the reads and the removal of the *active ground obj* —
 *  plus the `oc_wearpos*` config reads. See torirs_server_ops_obj.c. `obj_add` stays
 *  in torirs_server_scripts.c's switch: it takes a coord rather than an active obj
 *  and is already implemented there. */
int
ToriRSServer_OpsObj(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The inv *moves* — `inv_movefromslot`, `inv_dropslot` and the `inv_moveitem`
 *  family. See torirs_server_ops_inv.c. The rest of the inv family stays in
 *  torirs_server_scripts.c's switch; new inv opcodes land in the domain file. */
int
ToriRSServer_OpsInv(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `p_*` ops that re-issue the player's own interaction. See
 *  torirs_server_ops_player.c, whose header states why a re-issue is not a call into
 *  the engine's handler and what goes wrong when it is. */
int
ToriRSServer_OpsPlayer(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** Versioned POH storage operations. Construction policy remains content. */
int
ToriRSServer_OpsPoh(
    struct SSVM_State* state,
    int opcode,
    int dot);

/*
 * Push a param onto the stack its *declaration* calls for — the shared seam of
 * the whole `*_param` family, defined in torirs_server_ops_param.c.
 *
 * It is shared rather than copied because the choice of stack is the entire
 * difficulty of the `runtime_typed` family and it does not vary by table — only
 * the lookup does. Two copies would be two places for the declared-vs-stored
 * disagreement to be handled differently, and that disagreement is the one
 * thing in this family worth being loud about.
 *
 * `sval`/`ival` are the stored value and `present` says whether the record
 * carried the param at all; a caller with no row passes present = 0 and the
 * other two are ignored. `record_kind`/`record_id` name the record in the abort
 * message and nothing else.
 */
void
ToriRSServer_PushTypedParam(
    struct SSVM_State* state,
    int param_id,
    const char* sval,
    int32_t ival,
    int present,
    const char* record_kind,
    int record_id);

/* ------------------------------------------------------------------ */
/* Encoders (torirs_server_encode.c)                                         */
/* ------------------------------------------------------------------ */

/**
 * Frame + ISAAC-scramble one packet and write it to the socket. `var` is 0
 * (fixed), 1 (var-u8) or 2 (var-u16).
 *
 * `pkt_name` is a **canonical** GameProtoPktName, not a wire opcode: the
 * revision's number comes from `world->wire` here, so one build can serve
 * either. A name this revision does not carry is dropped and reported once
 * (torirs_server_wire.h). The packet capture still records the RESOLVED opcode, so
 * selftest assertions written against wire numbers keep their meaning.
 */
void
ToriRSServer_Send(
    struct ToriRSServerPlayer* player,
    int pkt_name,
    const uint8_t* payload,
    int len,
    int var);

/**
 * LoginResponse.ReconnectOk — the answer to GAMERECONNECT.
 *
 * Sent raw (login responses are not ISAAC-framed) and carrying the
 * player-info init block, because the rebuild that follows a reconnect is a
 * REBUILD_NORMAL and states nothing about the player table. Marks the local
 * slot tracked so that rebuild does not repeat the block.
 *
 * Returns 1 when it was sent; 0 on a revision whose player stream is not v5,
 * or when the player has no session to send it to.
 */
int
ToriRSServer_SendReconnectOk(struct ToriRSServerPlayer* player);

void
ToriRSServer_SendRebuildNormal(struct ToriRSServerPlayer* player);
/** Player-scoped normal-world view at an arbitrary centre. Does not move the
 *  player, mutate the shared scene origin, or emit the login GPI init block. */
void
ToriRSServer_SendRebuildNormalAt(
    struct ToriRSServerPlayer* player,
    int zone_x,
    int zone_z);
/** REBUILD_REGION: the instanced scene, built from the map-instance descriptor
 *  window rather than from the squares under it. */
void
ToriRSServer_SendRebuildRegion(struct ToriRSServerPlayer* player);
/** Whichever of the two the player's current tile calls for. Prefer this. */
void
ToriRSServer_SendRebuild(struct ToriRSServerPlayer* player);

/** Temporarily show one player a normal-world scene without moving them. */
void
ToriRSServer_WorldRemoteViewStart(
    struct ToriRSServerPlayer* player,
    int x,
    int z,
    int level,
    int ticks);
/** Restore the player's authoritative normal or instanced scene. Idempotent. */
void
ToriRSServer_WorldRemoteViewEnd(struct ToriRSServerPlayer* player);
void
ToriRSServer_SendIfOpentop(
    struct ToriRSServerPlayer* player,
    int group);
void
ToriRSServer_SendIfOpensub(
    struct ToriRSServerPlayer* player,
    int parent,
    int child,
    int group,
    int type);
/** IF_MOVESUB: move the sub at source_uid onto dest_uid. */
void
ToriRSServer_SendIfMovesub(
    struct ToriRSServerPlayer* player,
    int source_uid,
    int dest_uid);
/**
 * Open `group` as the gameframe root and remount HUD/tabs from the content
 * enum named after that interface (gameframe.enum). Updates the player's
 * gameframe_* slot uids from `<name>:mainmodal` etc.
 */
void
ToriRSServer_GameframeOpentop(
    struct ToriRSServerPlayer* player,
    int group);
void
ToriRSServer_SendIfSetevents(
    struct ToriRSServerPlayer* player,
    int uid,
    int from,
    int to,
    int events);
/** Explicit revision-239 event words. The classic wrapper above moves old
 * op bits 1..10 into events2 before calling this. */
void
ToriRSServer_SendIfSeteventsV2(
    struct ToriRSServerPlayer* player,
    int uid,
    int from,
    int to,
    uint32_t events1,
    uint32_t events2);
/** Send one authoritative snapshot. Revision 230 is deliberately a no-op: its
 * IF_RESYNC layout is different and its existing traffic remains unchanged. */
void
ToriRSServer_SendIfResyncV2(struct ToriRSServerPlayer* player);
/** Clear the old-style item array held directly by one interface component.
 * Revision 239 IF_CLEARINV; no-op on the unchanged revision-230 wire. */
void
ToriRSServer_SendIfClearinv(
    struct ToriRSServerPlayer* player,
    int uid);
/** Cause the golden client to run every onDialogAbort listener. Emitted before
 * any IF_CLOSESUB belonging to an interrupted dialogue. */
void
ToriRSServer_SendTriggerOndialogabort(struct ToriRSServerPlayer* player);
/** RUNCLIENTSCRIPT: run a CS2 clientscript on the client with int arguments.
 *  The world map's `worldmap_transmitdata` is the one the mock needs — it is
 *  how the server tells the map where the player is standing. */
void
ToriRSServer_SendRunClientscript(
    struct ToriRSServerPlayer* player,
    int script_id,
    int const* args,
    int argc);

/**
 * RUNCLIENTSCRIPT with mixed int and string arguments.
 *
 * `types` is one character per argument in the reference's own alphabet — `'s'`
 * for a string, anything else for an int — and is what goes on the wire ahead of
 * the values. `strv[i]` is read for an `'s'`, `intv[i]` otherwise; the unused
 * side of each index is ignored, so a caller passes whichever array it has.
 *
 * The multi-choice dialogue is what this exists for: rev 230's option list is
 * built by clientscript `chatbox_multi_init(string title, string options)`,
 * where `options` is the rows joined with `|`. Its rows are `cc_create`d, so
 * there is nothing for `if_settext` to address and no other way to fill them in.
 */
void
ToriRSServer_SendRunClientscriptMixed(
    struct ToriRSServerPlayer* player,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc);

/**
 * Revision-239 RUNCLIENTSCRIPT with the complete golden wire alphabet.
 *
 * `s`, `W`, `X` and byte 0xcf select string, int-array, string-array and long
 * respectively; every other type byte is a scalar int. This is intentionally
 * rev239-only because the classic client has no array/long decoder. Returns 1
 * when the payload was valid and queued, 0 for another revision or invalid /
 * oversized input.
 */
int
ToriRSServer_SendRunClientscriptTyped(
    struct ToriRSServerPlayer* player,
    int script_id,
    const char* types,
    const struct Mock239ClientScriptArg* args,
    int argc);
/* Interface setters. `uid` is the packed (interface << 16) | child. */
void
ToriRSServer_SendIfSettext(
    struct ToriRSServerPlayer* player,
    int uid,
    const char* text);
void
ToriRSServer_SendIfSetnpchead(
    struct ToriRSServerPlayer* player,
    int uid,
    int npc_id);
void
ToriRSServer_SendIfSetplayerhead(
    struct ToriRSServerPlayer* player,
    int uid);
void
ToriRSServer_SendIfSetanim(
    struct ToriRSServerPlayer* player,
    int uid,
    int anim_id);
void
ToriRSServer_SendIfSetcolour(
    struct ToriRSServerPlayer* player,
    int uid,
    int colour);
void
ToriRSServer_SendIfSethide(
    struct ToriRSServerPlayer* player,
    int uid,
    int hide);
void
ToriRSServer_SendIfSetmodel(
    struct ToriRSServerPlayer* player,
    int uid,
    int model_id);
void
ToriRSServer_SendIfSetobject(
    struct ToriRSServerPlayer* player,
    int uid,
    int obj_id,
    int value);
void
ToriRSServer_SendIfSetposition(
    struct ToriRSServerPlayer* player,
    int uid,
    int x,
    int y);
void
ToriRSServer_SendIfSetscroll(
    struct ToriRSServerPlayer* player,
    int uid,
    int position);
/** Revision-239 model-component setters. The authoritative protocol has no
 * compatible packet for these on this project's revision-230 wire, so all
 * seven are deliberate no-ops there rather than guessed legacy encodings. */
void
ToriRSServer_SendIfSetrotatespeed(
    struct ToriRSServerPlayer* player,
    int uid,
    int x_speed,
    int y_speed);
void
ToriRSServer_SendIfSetangle(
    struct ToriRSServerPlayer* player,
    int uid,
    int zoom,
    int angle_x,
    int angle_y);
void
ToriRSServer_SendIfSetnpcheadActive(
    struct ToriRSServerPlayer* player,
    int uid,
    int index);
void
ToriRSServer_SendIfSetplayermodelBasecolour(
    struct ToriRSServerPlayer* player,
    int uid,
    int index,
    int colour);
void
ToriRSServer_SendIfSetplayermodelBodytype(
    struct ToriRSServerPlayer* player,
    int uid,
    int body_type);
void
ToriRSServer_SendIfSetplayermodelObj(
    struct ToriRSServerPlayer* player,
    int uid,
    int obj_id);
void
ToriRSServer_SendIfSetplayermodelSelf(
    struct ToriRSServerPlayer* player,
    int uid,
    int copy_objs);
void
ToriRSServer_SendCamReset(struct ToriRSServerPlayer* player);
/**
 * Move / point the camera at a WORLD coordinate.
 *
 * World, not scene-local, and the distinction is the packet's whole content at
 * revision 239: CamMoveToV2 and CamLookAtV2 carry 16-bit ABSOLUTE coordinates
 * ("a specific coordinate in the root world"), where the classic packet carries
 * a byte each of scene-local 0..103. Passing scene-local into the V2 body puts
 * the camera near the world origin, thousands of tiles from the scene -- which
 * renders as a black viewport with the subject a dot in the distance, not as
 * anything that looks like a camera bug.
 *
 * So the caller states the coordinate and the encoder decides how to say it;
 * the scene-local subtraction that used to be at the call site now lives beside
 * the classic writer that needs it.
 */
void
ToriRSServer_SendCamMoveto(
    struct ToriRSServerPlayer* player,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2);
void
ToriRSServer_SendCamLookat(
    struct ToriRSServerPlayer* player,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2);
void
ToriRSServer_SendCamShake(
    struct ToriRSServerPlayer* player,
    int axis,
    int jitter,
    int amplitude,
    int frequency);
void
ToriRSServer_SendIfClosesub(
    struct ToriRSServerPlayer* player,
    int uid);

/**
 * Keep the enemy health overlay (interface `hpbar_hud`) in step with what this
 * player is fighting: open it, feed it, close it. Once per tick, after the
 * combat tick has settled the target. See torirs_server_hpbar.c.
 */
void
ToriRSServer_HpBarTick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);

/** No helper is wanted. Not a helper id -- see `ToriRSServer_HelperWantedFor`. */
#define TORIRSSERVER_HELPER_NONE 0
/** The clue-step infobox, All Settings row 275. Builder: clientscript 6631. */
#define TORIRSSERVER_HELPER_CLUE 1
/** The Agility helper, row 163. Builder: clientscript 5170. */
#define TORIRSSERVER_HELPER_AGILITY 2
/** The Slayer helper, row 184. Builder: clientscript 5317. */
#define TORIRSSERVER_HELPER_SLAYER 3

/**
 * Which helper this player's state calls for, and with what argument.
 *
 * The whole decision, factored out of the tick so the selftest can ask it
 * directly: an empty helper panel and a helper that never opened are the same
 * picture, so the choice has to be observable without one.
 *
 * Returns a TORIRSSERVER_HELPER_* value, or TORIRSSERVER_HELPER_NONE. `*out_arg`
 * is the builder's argument -- only the clue helper takes one -- and is written
 * on every return.
 */
int
ToriRSServer_HelperWantedFor(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int* out_arg);

/**
 * Keep the helper panel (`helper_generic`) and the Blast Furnace coffer HUD in
 * step with what this player is doing: open, build, close. Once per tick,
 * beside the enemy health overlay. See torirs_server_helper.c.
 */
void
ToriRSServer_HelperTick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);
/** Record a mount into (or out of) the gameframe's modal slots. Called by the
 *  IF_OPENSUB / IF_CLOSESUB encoders, so no opener has to remember to; `group`
 *  is 0 for a close. CLOSE_MODAL is what reads it back. */
void
ToriRSServer_NoteModalMount(
    struct ToriRSServer* srv,
    int uid,
    int group);
/** Open the "Enter amount" prompt. Zero payload; the answer arrives as
 *  RESUME_P_COUNTDIALOG. */
void
ToriRSServer_SendIfOpencountdialog(struct ToriRSServerPlayer* player);

void
ToriRSServer_SendVarpSmall(
    struct ToriRSServerPlayer* player,
    int id,
    int value);
/** p2 id, p4 value — for anything VARP_SMALL's signed byte cannot hold. */
void
ToriRSServer_SendVarpLarge(
    struct ToriRSServerPlayer* player,
    int id,
    int value);

void
ToriRSServer_SendStat(
    struct ToriRSServerPlayer* player,
    int stat,
    int level,
    int xp,
    int boosted);
/** Tell the client which player index it is. Sent once, in the login burst. */
void
ToriRSServer_SendUpdatePid(
    struct ToriRSServerPlayer* player,
    int local_pid);

void
ToriRSServer_SendRunEnergy(
    struct ToriRSServerPlayer* player,
    int percent);
void
ToriRSServer_SendRunWeight(
    struct ToriRSServerPlayer* player,
    int kilograms);
/**
 * The map square whose *region* rules (music, ambient bed) apply to a player.
 *
 * Not `player->x >> 6` inside an instance: an instanced player's own square is
 * out past the edge of the real map and describes nothing, so this resolves
 * through the square the instance was copied from. See the long note at the
 * definition for why every instanced encounter was silent before it existed.
 */
void
ToriRSServer_RegionSquareFor(
    struct ToriRSServerPlayer* player,
    int* out_map_x,
    int* out_map_z);

void
ToriRSServer_SendAmbientsoundStart(
    struct ToriRSServerPlayer* player,
    int id,
    int fade);

void
ToriRSServer_SendAmbientsoundStop(
    struct ToriRSServerPlayer* player,
    int fade);

/** Revision-adapted MIDI_SONG; rev 239 writes the 10-byte V2 envelope. */
void
ToriRSServer_SendMidiSong(
    struct ToriRSServerPlayer* player,
    int id);
/**
 * Send MIDI_SONG with its complete V2 envelope. All four timing arguments are
 * client cycles (20 ms), exactly as the reference scheduler receives them:
 * outgoing delay/duration, then incoming delay/duration. The ordinary sender
 * retains script9630's default profile; region music uses this entry point for
 * its explicit fade-in/fade-out profile.
 */
void
ToriRSServer_SendMidiSongEnvelope(
    struct ToriRSServerPlayer* player,
    int id,
    int fade_out_delay,
    int fade_out_speed,
    int fade_in_delay,
    int fade_in_speed);
/* MIDI_SONG_STOP. Silence is its own packet: MIDI_SONG's id field cannot say
 * "nothing", only "this track", and the 239 client's 65535 sentinel starts
 * nothing rather than stopping what is playing. */
void
ToriRSServer_SendMidiSongStop(
    struct ToriRSServerPlayer* player,
    int fade_out_delay,
    int fade_out_speed);
/* MIDI_JINGLE. `length_ms` is the jingle's own duration (see
 * `SS_OP_MIDI_LENGTH`) -- the client decodes it but does not act on it, so a
 * wrong value is invisible by ear and only the wire-layout tests catch it. */
void
ToriRSServer_SendMidiJingle(
    struct ToriRSServerPlayer* player,
    int id,
    int length_ms);
/* SYNTH_SOUND. Declared here because two callers want it: `SS_OP_SOUND_SYNTH`,
 * where a script asks for a noise, and torirs_server_combat.c, where an npc makes one
 * without a script being involved. */
void
ToriRSServer_SendSynthSound(
    struct ToriRSServerPlayer* player,
    int id,
    int loops,
    int delay);
void
ToriRSServer_SendMessage(
    struct ToriRSServerPlayer* player,
    const char* text);
/*
 * HINT_ARROW's `type` byte. The reference's own values; the client mirrors them
 * as APP_HINT_ARROW_* in src/app.h.
 */
enum
{
    TORIRSSERVER_HINT_ARROW_COORD = 1,
    TORIRSSERVER_HINT_ARROW_NPC = 2,
    TORIRSSERVER_HINT_ARROW_PLAYER = 10,
    TORIRSSERVER_HINT_ARROW_CLEAR = 255
};

/**
 * HINT_ARROW: point the player at an npc, a player, or an absolute tile.
 *
 * `type` is the wire's own selector -- 1 coord, 2 npc, 10 player, 255 clear --
 * and the remaining three fields mean different things per type. The four
 * `hint_*` script opcodes are the intended callers; see the definition.
 */
void
ToriRSServer_SendHintArrow(
    struct ToriRSServerPlayer* player,
    int type,
    int id,
    int z,
    int height);

void
ToriRSServer_SendUnsetMapFlag(struct ToriRSServerPlayer* player);
/** SET_MAP_FLAG with scene-local (x, z). Server-owned yellow cross. */
void
ToriRSServer_SendSetMapFlag(
    struct ToriRSServerPlayer* player,
    int local_x,
    int local_z);
void
ToriRSServer_SendTickEnd(struct ToriRSServerPlayer* player);

/*
 * Social. The five server->client packets of the friend / ignore / private-chat
 * feature; docs/FRIENDS_PRIVATE_CHAT.md §3.2 is the wire table and
 * src/torirsserver/torirs_server_friends.h is the service that decides what goes in them.
 *
 * These encoders decide nothing. In particular `world` below is already the
 * answer `isVisibleTo` gave — 0 means "offline OR not visible to this viewer",
 * and the encoder cannot tell which, deliberately, because the client cannot
 * either.
 */

/** One friend-list entry: p8 name37, p1 world. Both the login dump (one packet
 *  per friend) and every delta afterwards use this. */
void
ToriRSServer_SendUpdateFriendlist(
    struct ToriRSServerPlayer* player,
    int64_t name37,
    int world);

/** Empty UPDATE_FRIENDLIST: required to promote rev-239's social store from
 * FRIENDLIST_LOADED's "loading" state when the account has no entries. */
void
ToriRSServer_SendUpdateFriendlistEmpty(struct ToriRSServerPlayer* player);

/** The whole ignore list: p8 name37 * count. The client replaces its store
 *  wholesale, so there is no single-entry form. */
void
ToriRSServer_SendUpdateIgnorelist(
    struct ToriRSServerPlayer* player,
    const int64_t* names37,
    int count);

/** 0 loading, 1 connecting, 2 online. Sent once, after the login dump. */
void
ToriRSServer_SendFriendlistLoaded(
    struct ToriRSServerPlayer* player,
    int status);

/** An incoming private message. `message_id` must be non-zero — the client
 *  dedupes against a zero-filled ring and drops a 0. */
void
ToriRSServer_SendMessagePrivate(
    struct ToriRSServerPlayer* player,
    int64_t from37,
    int32_t message_id,
    int staff_mod,
    const char* text);

/** The three chat filter modes, echoed back so the client's UI agrees with the
 *  server's copy. */
void
ToriRSServer_SendChatFilterSettings(
    struct ToriRSServerPlayer* player,
    int public_mode,
    int private_mode,
    int trade_mode);

/** Whole container. `component` is the packed (interface << 16 | child) uid the
 *  container binds to. */
void
ToriRSServer_SendInvFull(
    struct ToriRSServerPlayer* player,
    int component,
    int container,
    const struct ToriRSServerItem* slots,
    int slot_count);

/** Only the slots whose bit is set in `dirty`. No-op when `dirty` is 0. */
void
ToriRSServer_SendInvPartial(
    struct ToriRSServerPlayer* player,
    int component,
    int container,
    const struct ToriRSServerItem* slots,
    int slot_count,
    uint32_t dirty);

/** Stop the client's global transmit listener for `container`. Revision 230
 *  identifies the component; revision 239 identifies the inventory id. */
void
ToriRSServer_SendInvStopTransmit(
    struct ToriRSServerPlayer* player,
    int component,
    int container);

/*
 * Zone updates.
 *
 * A zone sub-packet carries no coordinate — only `pos`, the tile's offset inside
 * an 8x8 zone. Which zone that is comes from the UPDATE_ZONE_* packet before it,
 * and the client keeps it as state, so the header and the sub-packets are only
 * ever correct together. Callers are torirs_server_zone.c and nothing else.
 */
/** Name a zone as the target of the sub-packets that follow. `full` picks
 *  UPDATE_ZONE_FULL_FOLLOWS, which also resets the client's memory of the zone;
 *  otherwise UPDATE_ZONE_PARTIAL_FOLLOWS, which does not. Coordinates are in
 *  zone units. */
void
ToriRSServer_SendZoneHeader(
    struct ToriRSServerPlayer* player,
    int zone_x,
    int zone_z,
    int level,
    int full);

/** One sub-packet, applied to whichever zone was last named. */
void
ToriRSServer_SendZoneSub(
    struct ToriRSServerPlayer* player,
    const struct ToriRSServerZoneEvent* event);

/**
 * Can this revision send this zone event as a packet of its own?
 *
 * At revision 230 every zone sub-packet is also a top-level opcode, so a
 * receiver-scoped event (loot only its killer may see) goes out on its own
 * after a PARTIAL_FOLLOWS header. At 239 the obj family and MAP_PROJANIM_V2
 * have NO top-level prot -- they exist only inside
 * UPDATE_ZONE_PARTIAL_ENCLOSED -- so the same send resolves to opcode -1 and
 * is dropped, silently, for exactly the events that matter to one player.
 *
 * The answer is per revision and per event, which is why it is a function and
 * not a flag on the event.
 */
int
ToriRSServer_ZoneSubStandalone(
    const struct ToriRSServerWire* wire,
    int kind);

/** The same sub-packet, encoded into a caller-owned buffer instead of sent —
 *  this is what makes a zone's shared blob shared. Returns the bytes written. */
int
ToriRSServer_EncodeZoneSub(
    const struct ToriRSServerWire* wire,
    uint8_t* dst,
    int max,
    const struct ToriRSServerZoneEvent* event);

/** Set or clear one of the five right-click ops on other players. A NULL or
 *  empty `text` clears the slot. */
void
ToriRSServer_SendSetPlayerOp(
    struct ToriRSServerPlayer* player,
    int slot,
    int primary,
    const char* text);

/** A whole shared blob as one UPDATE_ZONE_PARTIAL_ENCLOSED. */
void
ToriRSServer_SendZoneEnclosed(
    struct ToriRSServerPlayer* player,
    int zone_x,
    int zone_z,
    int level,
    const uint8_t* blob,
    int len);

/** Phase 8: put expired loc mutations back. */
void
ToriRSServer_WorldLocReverts(struct ToriRSServer* srv);

/** Schedule a revert `duration` ticks out. `duration <= 0` means never, and
 *  `loc_id < 0` means "remove it again", which is how a `loc_add` expires. */
int
ToriRSServer_WorldLocRevertQueue(
    struct ToriRSServer* srv,
    int duration,
    int loc_id,
    int shape,
    int angle,
    int x,
    int z,
    int level);

/** Phase 8: drop the objs whose flight time has run out. */
void
ToriRSServer_WorldObjDelayed(struct ToriRSServer* srv);

/**
 * Schedule a drop `delay` ticks out, to then live `duration` ticks.
 *
 * `delay <= 0` drops it now rather than next tick, so a caller that computed a
 * zero delay does not get a one-tick pause it did not ask for. Returns 0 with
 * nothing scheduled when the table is full — the obj is *lost* in that case,
 * which is the honest failure: the caller has already taken it out of the
 * inventory, and there is no way back into a container from here.
 */
int
ToriRSServer_WorldObjDelayedQueue(
    struct ToriRSServer* srv,
    int delay,
    int duration,
    int obj_id,
    int count,
    int x,
    int z,
    int level);

void
ToriRSServer_SendPlayerInfo(struct ToriRSServerPlayer* player);
void
ToriRSServer_SendSetActiveWorld(struct ToriRSServerPlayer* player);
void
ToriRSServer_SendNpcInfo(struct ToriRSServerPlayer* player);

/**
 * How many NPC TRANSFORMATION blocks have been written since the process
 * started, on either wire.
 *
 * For the selftest. A transformation makes the client rebuild the npc's model
 * and re-apply its `readyanim`, cancelling whatever it was animating — so one
 * written for an npc that never transformed reads, in the game, as "npcs have
 * no attack, defend or death animation" while every server-side check still
 * passes. See the counter's definition.
 */
long
ToriRSServer_EncodeNpcTransformationWrites(void);

/* Per-client npc names — see struct ToriRSServerPlayerSlotMap. */
void
ToriRSServer_SlotMapReset(struct ToriRSServerPlayer* player);

int
ToriRSServer_SlotMapAcquire(
    struct ToriRSServerPlayer* player,
    int world_slot);

int
ToriRSServer_SlotMapWorld(
    const struct ToriRSServerPlayer* player,
    int client_slot);

int
ToriRSServer_SlotMapClient(
    const struct ToriRSServerPlayer* player,
    int world_slot);

/** As ToriRSServer_SlotMapRelease, recording WHY for TORIRSSERVER_NPC_TRACE. The reason
 *  is the diagnostic: a released slot is a despawn on the client, and the
 *  re-add gets a different slot. */
/** Record/clear this player's follower. NULL clears. */
void
ToriRSServer_WorldNpcSetFollower(
    struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc,
    int slot);

/** The player's follower npc, or NULL when there is none or it has died. */
struct ToriRSServerNpc*
ToriRSServer_WorldNpcFollower(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int* out_slot);

void
ToriRSServer_SlotMapReleaseWhy(
    struct ToriRSServerPlayer* player,
    int world_slot,
    char const* why);

void
ToriRSServer_SlotMapRelease(
    struct ToriRSServerPlayer* player,
    int world_slot);

#endif
