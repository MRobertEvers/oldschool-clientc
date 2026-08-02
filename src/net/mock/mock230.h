#ifndef SRC_NET_MOCK_MOCK230_H
#define SRC_NET_MOCK_MOCK230_H

/*
 * OSRS rev-230 game server: shared state, and the seam between its files.
 *
 * NOT a mock any more, whatever the filenames say. It is the server this
 * project runs, and the `mock230_` prefix is a misnomer twice over — it also
 * reads a rev-239 cache while speaking the rev-230 wire. Renaming is on the
 * roadmap (docs/osrs230_mockserver.md §6.1) and has not happened yet.
 *
 *   mock230_main.c       the listening socket and the 600 ms tick loop
 *   mock230_transport.c  where bytes come from — a socket, or an in-process queue
 *   mock230_ws.c         the socket byte stream: raw TCP or WebSocket, sniffed
 *   mock230_session.c    login handshake + ISAAC + inbound framing, as a state machine
 *   mock230_embed.c      the server hosted inside another process, no socket at all
 *   mock230_save.c       player persistence, one ini per player
 *   mock230_boot.c       the loader order, which is load-bearing
 *   mock230_world.c      game state — movement, NPCs, containers, interactions
 *   mock230_encode.c     every server->client packet
 *   mock230_scripts.c    the ServerScript host seam (166 of 396 opcodes)
 *   mock230_content.c    the LostCity content tree: packs, configs, map spawns
 *   mock230_objinfo.c    obj metadata (name / wearpos / stackable) from the cache
 *   mock230_friends.c    friend / ignore / private-chat state, keyed by name
 *
 * ── How the three structures relate ──────────────────────────────────
 *
 *   Mock230Server   the WORLD. npcs, ground objs, scene, tick, scripts, and a
 *                   pool of players. One per process today.
 *   Mock230Player   a PLAYER. Its own inventory, stats, varps, interaction —
 *                   plus `world`, `session` and `pid`, so it is addressable on
 *                   its own. This is the struct that gets saved.
 *   Mock230Session  a CONNECTION. Transport, both ISAAC ciphers, the login
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
 * (`mock230_world_set_active`), the session sets it before dispatching a packet,
 * and it is meaningless outside those. A subsystem that reads it is asking "who
 * am I doing this for", which is the right question; a subsystem that reads it
 * to mean "the player in this world" is a bug, and the field is named the way it
 * is so that reads like one. It was called `player` while the pool held one, and
 * every such read looked correct.
 *
 * The reference keeps the same thing on its script state (`activePlayer`); what
 * is different here is that the engine paths read it off the world rather than
 * being handed a player, which is the residue of the single-player era and is
 * removed one subsystem at a time by giving the function a `Mock230Player*`.
 *
 * What is genuinely per-player now: the entity streams (PLAYER_INFO tracks
 * other players, NPC_INFO tracks npcs per player), the scene rebuild flag, the
 * ground-obj "already told them" set, and every encoder. What is still shared:
 * the *scene origin* — one 104x104 build area for the whole world, so players
 * further apart than it can cover would fight over it (`mock230_scene_build` is
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
 * Wire encoding lives entirely in mock230_encode.c and goes through
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
#define MOCK230_CACHE_REVISION 239
#define MOCK230_CACHE_DIR_DEFAULT "cache.osrs239"

#include "mock230_bank.h"
#include "mock230_zone.h"

#include <stdint.h>

struct Mock230Conn;
struct Mock230Session;

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
 * mock230_scripts.c's four file-local statics, mock230_db.c, mock230_worldmap.c,
 * mock230_world.c and ssc_lex.c — and a per-domain opcode file that needs a
 * coord had no way to reach any of them. Adding a sixth private copy is the
 * drift this repo has paid for before, so the shared form lives in the header
 * the domain files already include. **The statics in mock230_scripts.c should
 * be deleted in favour of these**; that is a pure deletion and it needs the
 * one-time mock230_scripts.c exception this lane does not hold.
 */
static inline int32_t
mock230_coord_pack(int level, int x, int z)
{
    return (int32_t)(((uint32_t)level << 28) | ((uint32_t)x << 14) | (uint32_t)z);
}

static inline int
mock230_coord_level(int32_t coord)
{
    return (int)(((uint32_t)coord >> 28) & 0x3);
}

static inline int
mock230_coord_x(int32_t coord)
{
    return (int)(((uint32_t)coord >> 14) & 0x3fff);
}

static inline int
mock230_coord_z(int32_t coord)
{
    return (int)((uint32_t)coord & 0x3fff);
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
     * about the *tracked* count and says nothing about the world: the stream's
     * slot field is 14 bits, so the wire's own ceiling on how many npcs may
     * exist is 16383, and the reference runs at exactly that
     * (`NODE_MAX_NPCS`, default 16383). A protocol constant was standing in for
     * a world capacity, which is the shape of bug where raising the roster
     * silently corrupts a packet instead of failing.
     *
     * So: MOCK230_NPC_MAX is a memory decision (336 bytes per npc, statically
     * allocated in the world), MOCK230_TRACKED_NPC_MAX is the wire's. Lumbridge
     * itself is 63 npcs.
     *
     * What made the world cap load-bearing was the encoder scanning every slot
     * in the world for every client every tick; NPC_INFO asks the ZoneMap for
     * the npcs within 15 tiles now (mock230_zone_npcs_near), so the two numbers
     * are free to be different sizes.
     */
    MOCK230_NPC_MAX = 2048,
    MOCK230_TRACKED_NPC_MAX = 255,
    /* The client sends at most 25 waypoints per move request; a walk can then
     * be interpolated over many tiles, so the step queue is larger. */
    MOCK230_STEP_MAX = 256,
    MOCK230_INV_SLOTS = 28,
    MOCK230_WORN_SLOTS = 14,

    /*
     * Rows in a container registry table — see mock230_container.h.
     *
     * A storage ceiling, not a container count: the cache names 1026 invs and
     * every one of them is registrable, so this bounds how many a single player
     * (or the world) can hold *at once*. The reference has no equivalent because
     * `Player.invs` is a Map; a fixed table is what a fixed player struct can
     * carry, and overflowing it is reported rather than absorbed.
     */
    MOCK230_CONTAINER_MAX = 16,
    MOCK230_WORLD_CONTAINER_MAX = 16,

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
     * the streams became per-player — see `Mock230Player.tracked_*` and
     * `mock230_send_player_info`.
     */
    MOCK230_PLAYER_MAX = 8,

    /*
     * Container ids, interface ids and component uids are NOT here. They are
     * the cache's numbers, not this server's, so they come from the content
     * tree by name — see mock230_ids.h. What stays in this file is what sizes
     * an array or what the protocol means.
     */

    /* Scene is 104x104 tiles based at (zone - 6) * 8. Rebuild once the player
     * comes within 16 tiles of an edge, the same margin the reference uses. */
    MOCK230_SCENE_TILES = 104,
    MOCK230_REBUILD_MARGIN = 16,

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
     * 5000 up was silently dropped: mock230_world_set_varp bounds-checks and
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
    MOCK230_VARP_CACHE_MAX = 5705,
    MOCK230_VARP_SERVER_HEADROOM = 512,
    MOCK230_VARP_COUNT = MOCK230_VARP_CACHE_MAX + MOCK230_VARP_SERVER_HEADROOM,
    /* Ground items. Lumbridge's own spawns are a dozen; a busy fight adds a
     * handful per kill, and they expire. */
    MOCK230_GROUND_MAX = 256,
    /** Pending `[ai_queue<n>]` entries per npc. */
    MOCK230_NPC_QUEUE_MAX = 4,
    /** Loc mutations that can be waiting to revert at once. Generous: a busy
     *  mining site is a dozen, and the cost is 40 bytes each. */
    MOCK230_LOC_REVERT_MAX = 128,
    /* `MOCK230_LOOT_TICKS` was here — 200 ticks on the floor, annotated
     * "LostCity's ^lootdrop_duration", beside a content tree already stating
     * `^lootdrop_duration = 200`. Naming the constant you are duplicating does
     * not stop it being a duplicate: the engine reads the real one now, through
     * `mock230_ids()->lootdrop_duration`. */

    /* Parked script bookkeeping. */
    MOCK230_QUEUE_MAX = 16,
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
     * `mock230_scripts_queue_hook`'s is.
     */
    MOCK230_ENGINE_QUEUE_MAX = 8,
    MOCK230_TIMER_MAX = 8,
    MOCK230_WORLD_QUEUE_MAX = 16,
    MOCK230_RESUME_BUTTON_MAX = 8,
    /*
     * Highest sub-id `if_addresumebutton` arms on the component it registers.
     *
     * A resume button on a *container* is the multi-choice dialogue: its rows
     * are `cc_create`d children with sub-ids 1..5, and arming only slot 0 arms
     * the empty container. 15 is comfortably past the five the reference's
     * widest choice uses, and a plain component has no sub-ids for the extra
     * range to reach.
     */
    MOCK230_RESUME_SUB_MAX = 15,

    /*
     * Arguments `runclientscript*` will carry.
     *
     * Three numbers bound this and they are not the same number: the compiler
     * builds at most SSC_MAX_VARARG_TYPES (16) of them, the wire reader stops
     * at PKT_RUNCLIENTSCRIPT_ARG_MAX (20, `src/net/rev/revpacket.h`), and this
     * is the one in the middle. It is set to the compiler's, so a call that
     * compiles always fits — and the host case *aborts* on a longer type
     * string rather than truncating, because a clientscript run with three of
     * its four arguments does not fail, it draws the wrong panel.
     */
    MOCK230_RUNCLIENTSCRIPT_ARG_MAX = 16,

    /*
     * Combat: the unarmed attack interval, and nothing else.
     *
     * Four ticks, which is what a weapon with no `attackrate` param and a player
     * with no weapon swing at. Everything an *npc* does is on its record instead
     * — the attack rate, the reach, how long its corpse lies there and how long
     * until it comes back — because those are per-npc and a content author owns
     * them. `MOCK230_DEATH_TICKS`, `MOCK230_RESPAWN_TICKS` and
     * `MOCK230_ATTACK_RANGE` used to sit here saying otherwise; they are
     * `death_delay`, `respawnrate` and `attackrange` on `struct Mock230NpcDef`,
     * and the engine reads them through `mock230_content_npc_default()`.
     *
     * `MOCK230_PLAYER_MAX_HP` was here too and had no readers at all: the
     * player's maximum is `stat_level[hitpoints]`, which the levels decide.
     */
    MOCK230_ATTACK_SPEED = 4,

    /*
     * Run energy is kept in hundredths of a percent, which is the unit
     * OldSchool's own drain formula is written in: one running step off an
     * unencumbered player costs 67 of these, so a percent is not a fine enough
     * grain to hold the remainder. The wire and the orb carry the percent.
     */
    MOCK230_RUN_ENERGY_MAX = 10000,

    /* IF_SETEVENTS bit 0: the component accepts a plain click, answered with
     * IF_BUTTON. Mirrors RS_MINIMENU_EVENT_CLICK on the client. */
    MOCK230_EVENT_CLICK = 0x1,

    /* Wire sentinels in the classic info streams. */
    MOCK230_PLAYER_TERMINATOR = 2047,
    MOCK230_NPC_TERMINATOR = 16383,

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
     * There is deliberately no `MOCK230_FACE_LOCAL_PLAYER` constant. There used
     * to be — `MOCK230_FACE_PLAYER_BASE + 2047`, "the local player" — and it is
     * a *self-alias*: a value whose meaning depends on who is reading it,
     * written into a field every observer reads. With one client that was
     * invisible; with two it made a goblin fighting alice turn to face bob on
     * bob's screen. The face id is `MOCK230_FACE_PLAYER_BASE + player->pid`
     * now, through `mock230_npc_face_player()` (below the npc struct), and the
     * constant is deleted so no site can reach for "whoever happens to be
     * watching" again.
     */
    MOCK230_FACE_PLAYER_BASE = 32768,

    /* New-npc record field widths. These MUST match what the rev-230 table
     * declares (GameProtoRevTable.npc_slot_bits / .npc_type_bits) — the two
     * ends of the same bitstream. A mismatch does not fail the decode, it
     * shifts every field after the offending one. */
    MOCK230_NPC_SLOT_BITS = 14,
    MOCK230_NPC_TYPE_BITS = 14,
    MOCK230_NPC_TYPE_MAX = (1 << MOCK230_NPC_TYPE_BITS) - 1,
};

/*
 * Extended-info mask bits, mirroring the client's readers exactly
 * (src/net/rev/packets/pkt_player_info.h and pkt_npc_info.h). The writers must
 * emit fields in ascending bit order, because that is the order the reader
 * tests them in — the mask says which fields are present, never where.
 */
enum
{
    MOCK230_PMASK_APPEARANCE = 0x001,
    MOCK230_PMASK_SEQUENCE = 0x002,
    MOCK230_PMASK_FACE_ENTITY = 0x004,
    MOCK230_PMASK_SAY = 0x008,
    MOCK230_PMASK_DAMAGE = 0x010,
    MOCK230_PMASK_FACE_COORD = 0x020,
    MOCK230_PMASK_CHAT = 0x040,
    /* Not a field: it says the mask itself is two bytes. See put_player_mask. */
    MOCK230_PMASK_BIG_UPDATE = 0x080,
    MOCK230_PMASK_SPOTANIM = 0x100,
    MOCK230_PMASK_EXACT_MOVE = 0x200,
    MOCK230_PMASK_DAMAGE2 = 0x400,
};

/* The npc mask is a single byte — there is no widening bit. */
enum
{
    MOCK230_NMASK_DAMAGE2 = 0x01,
    MOCK230_NMASK_ANIM = 0x02,
    MOCK230_NMASK_FACE_ENTITY = 0x04,
    MOCK230_NMASK_SAY = 0x08,
    MOCK230_NMASK_DAMAGE = 0x10,
    MOCK230_NMASK_CHANGE_TYPE = 0x20,
    MOCK230_NMASK_SPOTANIM = 0x40,
    MOCK230_NMASK_FACE_COORD = 0x80,
};

/* Appearance/equipment slot numbering. The cache's wearpos fields, the worn
 * container's slots, and the 12-entry appearance blob all index the same
 * space, so one enum serves all three. 6/8/11 hold body kits rather than
 * items; an item claims them through wearpos_2 / wearpos_3 to hide the kit
 * underneath (a full helm hides hair + jaw, a platebody hides arms). */
/*
 * Skills. The index is the protocol's: UPDATE_STAT carries it, the client's own
 * stat table uses it, and content/pack/stat.pack names it. Only the six combat
 * skills are used, but the array is the full 23 so a stat id from the wire is
 * never out of range.
 */
enum
{
    MOCK230_STAT_ATTACK = 0,
    MOCK230_STAT_DEFENCE = 1,
    MOCK230_STAT_STRENGTH = 2,
    MOCK230_STAT_HITPOINTS = 3,
    MOCK230_STAT_RANGED = 4,
    MOCK230_STAT_PRAYER = 5,
    MOCK230_STAT_MAGIC = 6,
    MOCK230_STAT_AGILITY = 16,
    MOCK230_STAT_COUNT = 23,
};

/*
 * Attack styles, in the order the combat interface lists them. OldSchool folds
 * the style into the effective level before the roll: accurate is +3 attack,
 * aggressive +3 strength, defensive +3 defence, controlled +1 to all three.
 */
enum Mock230AttackStyle
{
    MOCK230_STYLE_ACCURATE = 0,
    MOCK230_STYLE_AGGRESSIVE = 1,
    /* The cache's order, not intuition's: DBTable 78's per-layout style rows
     * put controlled at 2 and defensive at 3 (`combat_interface_hacksword`:
     * 2,Lunge,(Controlled) / 3,Block,(Defensive); unarmed skips 2 entirely,
     * which is why its Block writes com_mode=3). These were transposed once
     * and every Block click trained shared XP. */
    MOCK230_STYLE_CONTROLLED = 2,
    MOCK230_STYLE_DEFENSIVE = 3,
};

enum Mock230WearPos
{
    MOCK230_WEAR_HEAD = 0,
    MOCK230_WEAR_CAPE = 1,
    MOCK230_WEAR_AMULET = 2,
    MOCK230_WEAR_WEAPON = 3,
    MOCK230_WEAR_BODY = 4,
    MOCK230_WEAR_SHIELD = 5,
    MOCK230_WEAR_ARMS = 6,
    MOCK230_WEAR_LEGS = 7,
    MOCK230_WEAR_HAIR = 8,
    MOCK230_WEAR_HANDS = 9,
    MOCK230_WEAR_FEET = 10,
    MOCK230_WEAR_JAW = 11,
    MOCK230_WEAR_RING = 12,
    MOCK230_WEAR_AMMO = 13,
};

/* ------------------------------------------------------------------ */
/* Obj metadata (mock230_objinfo.c)                                    */
/* ------------------------------------------------------------------ */

struct Mock230ObjInfo
{
    const char* name;
    /**
     * The examine text — obj config opcode 3, `RSCache_Dat2ConfigObj.examine`,
     * `desc=` in the content tree's export.
     *
     * NULL when the record states none, which is what a placeholder or an
     * unnamed id looks like. It is the only string a rev-230 "Examine" op can
     * print, and it was decoded by the cache and dropped here until `oc_desc`
     * needed it: the config-query note in mock230_scripts.c said "the obj
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
    /*
     * Bank notes ("certificates"), both directions.
     *
     * A *note* record carries `noted_template` — the shared template obj it
     * borrows its model from — and `noted_id`, the item it stands for. A plain
     * item carries neither, and nothing in the cache points from an item to its
     * note. So un-noting reads straight out of the record, and noting needs the
     * reverse index mock230_objinfo_load builds by walking the whole table.
     *
     * All three are -1 when absent, which is also what "this obj has no note
     * form" means — the case a bank has to report rather than assume.
     */
    int noted_id;
    int noted_template;
    /** The note form of this item, or -1. */
    int cert_id;
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
     * twelve equipment bonuses in the order Mock230CombatParam names them, 14
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
mock230_objinfo_load(const char* cache_dir);

void
mock230_objinfo_free(void);

/** Never NULL: unknown ids report a placeholder name and wearpos -1. */
/**
 * One param off an obj record, as the cache stored it.
 *
 * `sval` is non-NULL exactly when the cache marked the entry a string, and that
 * is not the same question as what `configs/all.param` *declares* the param's
 * type to be. A caller pushing onto a typed stack must go by the declaration
 * (`mock230_content_param_type`), because that is what the script was compiled
 * against; this struct says what is actually there. When they disagree the
 * record is wrong and saying so beats guessing which to believe.
 */
struct Mock230ObjParam
{
    int32_t obj_id;
    int32_t key;
    int32_t ival;
    char* sval;
};

/** The param, or NULL when this obj does not carry it. */
const struct Mock230ObjParam*
mock230_obj_param(int obj_id, int param_id);

const struct Mock230ObjInfo*
mock230_objinfo(int obj_id);

/** How many obj records the cache decoded — the exclusive upper bound on a
 *  scan of the table. 0 before `mock230_objinfo_load`, and 0 without a cache. */
int
mock230_objinfo_count(void);

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
mock230_obj_category_members(int category);

/* ------------------------------------------------------------------ */
/* Equipment requirements                                              */
/* ------------------------------------------------------------------ */

/*
 * The levels you need to wear something.
 *
 * A sparse table rather than a field on Mock230ObjInfo: about 1,100 of the
 * 33,747 objs have a requirement at all, so eight (stat, level) pairs on every
 * record would cost 2 MB to say "none" 32,000 times.
 *
 * Two sources feed it and neither is sufficient alone — see
 * docs/mock230_content.md §5:
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
    MOCK230_OBJ_REQUIRE_MAX = 8,
};

struct Mock230ObjRequire
{
    int obj_id;
    int count;
    struct
    {
        int stat;
        int level;
    } req[MOCK230_OBJ_REQUIRE_MAX];
};

/** NULL when the obj has no requirement, which is the overwhelming majority. */
const struct Mock230ObjRequire*
mock230_obj_require(int obj_id);

/**
 * Replace an obj's requirements, for the `.obj` config overlay.
 *
 * Replace rather than merge: a config block that names an item is stating the
 * whole requirement for it, the same way an `.npc` block's `hitpoints=` is not
 * added to whatever the cache said. Returns 0 when the table is full or the id
 * is out of range.
 */
int
mock230_obj_require_set(
    int obj_id,
    const int* stats,
    const int* levels,
    int count);

/** How many objs carry a requirement, and how many came from the cache alone.
 *  For mock230_pack's report. */
void
mock230_obj_require_counts(
    int* total,
    int* from_cache);

/* ------------------------------------------------------------------ */
/* NPC metadata (mock230_npcinfo.c)                                    */
/* ------------------------------------------------------------------ */

struct Mock230NpcInfo
{
    /** Never NULL from mock230_npcinfo(); unknown ids report a placeholder. */
    const char* name;
    int combat_level;
    int size;
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
     * `mock230_npc_category()` states once the -1 that means "no category
     * rung", because 0 is the decoder's "unstated" *and* would be a legal id.
     * `mock230_npcinfo_record()` is the ungated row, for the callers where a
     * nameless multinpc instance still has to answer — `mock230_npcinfo()`
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
 * Deliberately NOT read off `mock230_npcinfo()`. That accessor gates on the
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
 * **Six npc dispatch sites in mock230_world.c still pass a literal -1**, and
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
 * existing and never firing. Measured both ways in `mock230_world_npc_died`.
 *
 * `[opnpc*]`/`[apnpc*]` from a real interaction already reach the rung, through
 * `interaction_category()`. Nothing needs to *guard* the remaining six: a
 * category of -1 and a category nothing binds behave identically, so adopting
 * them is additive and cannot change an existing dispatch.
 */
int
mock230_npc_category(int npc_id);

/** How many npc records carry this category id — see
 *  `mock230_obj_category_members`, same question one namespace over. */
int
mock230_npc_category_members(int category);

/**
 * One param off an npc record, as the cache stored it.
 *
 * Same shape and same rule as `struct Mock230ObjParam`: `sval` is non-NULL
 * exactly when the cache marked the entry a string, and that is a different
 * question from what `configs/all.param` *declares* the param to be. Go by the
 * declaration when choosing a stack, by this when reading the value.
 */
struct Mock230NpcParam
{
    int32_t npc_id;
    int32_t key;
    int32_t ival;
    char* sval;
};

/** The param, or NULL when this npc does not carry it. */
const struct Mock230NpcParam*
mock230_npc_param(int npc_id, int param_id);

/** Decode the npc config table once. Returns 0 when the cache is absent, in
 *  which case every lookup reports a placeholder name and the mock still runs. */
int
mock230_npcinfo_load(const char* cache_dir);

void
mock230_npcinfo_free(void);

const struct Mock230NpcInfo*
mock230_npcinfo(int npc_id);

/**
 * 1 when `mock230_npcinfo(npc_id)` answers from the decoded table, 0 when it
 * would hand back the "Someone" placeholder.
 *
 * The accessor gates on the record having a *name*, so a nameless record — the
 * multinpc instances are all of them — hides its whole decoded row, params
 * included, from everything seeded through it. The server-band verifier needs
 * that fact out loud: a band value over such a record has nothing on this side
 * to be compared against, which is different from being wrong.
 */
int
mock230_npcinfo_known(int npc_id);

/**
 * The decoded row, or NULL — **without** the name gate `mock230_npcinfo` puts
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
const struct Mock230NpcInfo*
mock230_npcinfo_record(int npc_id);

/* ------------------------------------------------------------------ */
/* loc and struct configs (mock230_locinfo.c, mock230_structinfo.c)    */
/* ------------------------------------------------------------------ */

/*
 * Two more config tables, for the `lc_*` / `loc_*` reads and `struct_param`.
 *
 * A `struct` record is a param map and nothing else, so struct exposes only
 * params. Loc exposes params plus the three fields the script host asks for by
 * id — name and footprint — because `lc_name`/`lc_width`/`lc_length` name a loc
 * that need not be anywhere near the scene. Everything else about a loc really
 * is the scene's business. The row type is the shared `struct Mock230ParamRow`
 * (mock230_paramtable.h) rather than a per-table clone of the same four fields.
 */
struct Mock230ParamRow;

/** The param, or NULL when this loc does not carry it. */
const struct Mock230ParamRow*
mock230_loc_param(int loc_id, int param_id);

/**
 * The loc's display name, or NULL when the record carries none.
 *
 * Borrowed, and valid until `mock230_locinfo_free`. `loc_name` / `lc_name` push
 * the reference's `'null'` in the NULL case; 32,161 of cache.osrs239's 62,194
 * records land there.
 */
const char*
mock230_loc_name(int loc_id);

/**
 * The record's **unrotated** footprint. Writes 1x1 — the decoder's own default
 * — for an id with no override and for an id with no record, so a caller never
 * has to branch on whether the cache loaded.
 *
 * Not the same as `struct Mock230SceneLoc`'s size_x/size_z, which have already
 * been rotated by the placed angle.
 */
void
mock230_loc_footprint(int loc_id, int* out_width, int* out_length);

/**
 * Does the config group hold a record for this id? (The reference's
 * `LocTypeValid`.)
 *
 * Reports 0 for everything when the cache is absent, so a caller that wants to
 * degrade rather than abort tests `mock230_locinfo_count()` first — see
 * `check_loc_id` in mock230_ops_loc.c.
 */
int
mock230_loc_known(int loc_id);

/** The param, or NULL when this struct does not carry it. */
const struct Mock230ParamRow*
mock230_struct_param(int struct_id, int param_id);

/** Decode the loc / struct config groups once. Returns 0 when the cache is
 *  absent, in which case every lookup reports "not carried" and the server
 *  still runs — content then reads the param's declared default. */
int
mock230_locinfo_load(const char* cache_dir);

void
mock230_locinfo_free(void);

int
mock230_structinfo_load(const char* cache_dir);

void
mock230_structinfo_free(void);

/** Records decoded and rows retained, for the tests and the boot line. */
int
mock230_locinfo_count(void);
int
mock230_locinfo_param_count(void);
int
mock230_locinfo_name_count(void);
int
mock230_locinfo_size_count(void);
int
mock230_structinfo_count(void);
int
mock230_structinfo_param_count(void);

/* ------------------------------------------------------------------ */
/* Packet capture (selftest only)                                      */
/* ------------------------------------------------------------------ */

/*
 * Records every packet the tick produces, so the selftest can assert on what
 * actually went out rather than only on the state left behind.
 *
 * The hook sits at the top of mock230_send, above its `fd < 0` early return —
 * every encoder has already built its payload by then, so all of them become
 * observable without a single encoder changing. Under the selftest there is no
 * cipher either, so the recorded opcode is the plain one.
 */

struct Mock230Server;
struct SSVM_Provider;
struct SSVM_Env;
struct SSVM_State;
struct SSVM_Script;
struct Mock230NpcDef;

enum
{
    MOCK230_CAPTURE_MAX = 512,
    MOCK230_CAPTURE_BYTES = 1024,
};

struct Mock230CapturedPacket
{
    int opcode;
    int len;
    uint8_t data[MOCK230_CAPTURE_BYTES];
};

struct Mock230Capture
{
    struct Mock230CapturedPacket packets[MOCK230_CAPTURE_MAX];
    int count;
    /** Set when a packet was dropped, so a test cannot silently assert against
     *  a truncated record. */
    int overflow;
};

void
mock230_capture_begin(
    struct Mock230Server* srv,
    struct Mock230Capture* capture);
void
mock230_capture_end(struct Mock230Server* srv);
void
mock230_capture_reset(struct Mock230Capture* capture);

/** Index of the next packet with this opcode at or after `from`, else -1. */
int
mock230_capture_find(
    const struct Mock230Capture* capture,
    int opcode,
    int from);

/** True when `opcodes` all appear in order. Other packets may interleave. */
int
mock230_capture_has_sequence(
    const struct Mock230Capture* capture,
    const int* opcodes,
    int count);

/* ------------------------------------------------------------------ */
/* Game state                                                          */
/* ------------------------------------------------------------------ */

struct Mock230Item
{
    int obj_id; /* -1 = empty slot */
    int count;
};

/*
 * One registered container. The registry itself is mock230_container.h; the
 * struct is here because `struct Mock230Player` embeds a table of them and
 * mock230_container.h cannot be included ahead of `struct Mock230Item`.
 *
 * A row is what `container_for` used to be a `case` of. Everything that made
 * the three-case version wrong is a *field* here — where the items live, who
 * owns them, how "changed" is recorded, what the container paints into — so a
 * fourth container is a registration rather than a fourth branch.
 */
struct Mock230Container
{
    /** 0 for a free row. A zeroed player struct is therefore an empty table,
     *  which matters because `mock230_world_player_init` memsets one. */
    uint8_t used;
    /** MOCK230_CONTAINER_PLAYER / _WORLD. The whole reason resolve does not
     *  take `active_player`: a shared container has no player to ask. */
    uint8_t owner_kind;
    /** The registry calloc'd `items` and must free it. 0 for an adopted array
     *  belonging to the player struct or to mock230_bank. */
    uint8_t owns_items;
    /** Dirty is a 32-bit per-slot mask rather than a whole-container flag.
     *  Decided at registration from `slots`, because UPDATE_INV_PARTIAL can
     *  only address 32 of them — 304 of the cache's 1026 invs are larger. */
    uint8_t per_slot;
    /** A write to this container changes how its owner looks, so it also sets
     *  MOCK230_PMASK_APPEARANCE. True of the worn container and nothing else. */
    uint8_t appearance;
    /** Set when a component binds and cleared once a full update has gone out —
     *  the reference's `listener.firstSeen`, which is what makes a binding
     *  paint a container that has not changed since. */
    uint8_t first_seen;

    int32_t inv_id;
    int32_t slots;
    struct Mock230Item* items;

    /** The player this row belongs to, for MOCK230_CONTAINER_PLAYER rows; NULL
     *  for a world row. Never a copy: `players[]` is a fixed array that is
     *  neither compacted nor moved (see its comment). */
    struct Mock230Player* owner;

    /*
     * Dirty state, in one of two places.
     *
     * `*_ref` is set when the flag predates the registry and something outside
     * it still reads the original — `player->inv_dirty` feeds the appearance
     * path and two selftests, `bank.dirty` feeds mock230_bank_flush. NULL means
     * the row owns its own, which is the case for every container registered
     * from here on. Access goes through the accessors in mock230_container.h,
     * so no self-referential pointer is ever stored in the row.
     */
    uint32_t* slot_dirty_ref;
    int* dirty_ref;
    uint32_t slot_dirty_own;
    int dirty_own;

    /** The component this container paints into, or -1 when nothing is bound.
     *  `inv_transmit` sets it; `inv_stoptransmit` clears it. */
    int32_t component;
};

/*
 * One obj on the floor.
 *
 * A *spawn* (from the content tree's map squares) respawns after it is taken;
 * a drop does not, and expires instead. That is LostCity's distinction between
 * a static obj and a dynamic one, and it is the whole reason both fields exist.
 */
struct Mock230GroundObj
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
     * One reader: `mock230_world_obj_handle`, which is how a running script
     * holds the obj it is acting on. The reference holds a direct `Obj`
     * reference, so an `obj_takeitem` resumed after a `p_delay` cannot take
     * somebody else's drop; here the handle is an index into a 256-slot array
     * that is reused, and an index alone would resolve to whatever landed in
     * the slot meanwhile. See mock230_ops_obj.c.
     */
    int generation;
    /* `sent` was here, for the same reason `Mock230Npc.tracked` was: whether a
     * client has been told is a fact about the client. The per-client answer is
     * `Mock230Player.loaded_zones` now — see mock230_zone.h on why "does this
     * client hold that zone" replaced "has this client seen that obj". */

    /** Packed zone index **plus one** — 0 means "filed nowhere". Maintained by
     *  `mock230_zone_sync_objs`; see `refile` for why it is offset. */
    int zone_index;
};

/**
 * Which queue a `Mock230Queued` belongs to.
 *
 * `PlayerQueueType` in the reference, minus SOFT, which the reference declares
 * and never uses — a kind nothing can put in the queue is a branch no test can
 * reach.
 *
 * NORMAL, LONG, WEAK and STRONG live in one array where the reference keeps
 * `queue` and `weakQueue` as two lists; the kind is what the drain splits on,
 * and the only observable difference between them is *when they are cleared* —
 * see `mock230_world_close_modal` (WEAK) and `mock230_scripts_process_queues`
 * (STRONG).
 *
 * ENGINE is the exception and lives in `Mock230Player.engine_queue`, because
 * the reference keeps it apart too and the separation is load-bearing rather
 * than stylistic: `clearqueue` must not be able to cancel a zone trigger, and
 * an engine entry's delay is forced to 0 rather than taken from content.
 */
enum Mock230QueueKind
{
    MOCK230_QUEUE_NORMAL = 0,
    /** `longqueue` — like NORMAL, plus a logout action. */
    MOCK230_QUEUE_LONG,
    /** `weakqueue` — discarded whenever a modal closes. */
    MOCK230_QUEUE_WEAK,
    /** `strongqueue` — closes whatever modal is up before the drain, so its own
     *  entry passes the access check on the tick it is due. */
    MOCK230_QUEUE_STRONG,
    /** The zone family. Engine-produced, delay always 0, drained after the
     *  timers from its own array. Content cannot put one here. */
    MOCK230_QUEUE_ENGINE
};

/** A script waiting for its delay to run out. */
struct Mock230Queued
{
    int active;
    int script_id;
    /** Ticks remaining. Decremented once per tick — unconditionally, as the
     *  reference does, so an entry that came due while the player was busy
     *  fires the moment access returns rather than restarting its wait. */
    int delay;
    int32_t arg;
    /** enum Mock230QueueKind. */
    int kind;
    /** LONG only: `^accelerate` (0) means "run it early when the player logs
     *  out" rather than discarding it. Stored and not yet read — `phase_logouts`
     *  is empty (osrs230_mockserver.md §3.9). */
    int logout_action;
};

/** Whether a timer runs while the player is busy. */
enum Mock230TimerType
{
    /** `settimer` — needs access, and runs *with* protected access. */
    MOCK230_TIMER_NORMAL = 0,
    /** `softtimer` — runs while busy, and without protected access. */
    MOCK230_TIMER_SOFT
};

/** A script that re-runs on an interval. */
struct Mock230Timer
{
    int active;
    int script_id;
    int interval;
    /** The world tick the timer was last armed or fired at — **absolute**, not
     *  a countdown. That is what `gettimer` returns, so a relative counter here
     *  would make the opcode unimplementable rather than merely different. */
    int clock;
    /** enum Mock230TimerType. */
    int type;
};

/** One queued tile of movement, in absolute tile coordinates. */
struct Mock230Step
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
enum Mock230InteractionKind
{
    MOCK230_INTERACT_NONE = 0,
    MOCK230_INTERACT_NPC,
    MOCK230_INTERACT_LOC,
    MOCK230_INTERACT_OBJ,
};

struct Mock230Interaction
{
    enum Mock230InteractionKind kind;
    /** 1-based op index, as the OP<thing><n> packet numbered it. */
    int op;

    /** NPC slot for MOCK230_INTERACT_NPC. Revalidated every tick: an npc can
     *  die or have its slot reused while the player is still walking over. */
    int npc_slot;
    /** The npc type / loc id / obj id this interaction was started against, so
     *  a slot that changed underneath is detected rather than acted on. */
    int target_id;

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
     * `mock230_scripts_run_opheldu`.
     */
    int use_on;
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
    MOCK230_AP_RANGE_DEFAULT = 10,
    /*
     * LostCity's `npcmode` numbering, which the compiler already seeds as
     * builtin symbols (`SSC_SymbolsSeedBuiltins`). Restated here rather than
     * shared, because the compiler's table is the *language's* and this is the
     * engine's reading of it — the day a mode is added, one of the two moving
     * without the other should be a mismatch somebody notices, not a silent
     * renumber.
     */
    MOCK230_NPCMODE_NULL = -1,
    MOCK230_NPCMODE_NONE = 0,
    MOCK230_NPCMODE_WANDER = 1,
    MOCK230_NPCMODE_PATROL = 2,
    MOCK230_NPCMODE_PLAYERESCAPE = 3,
    MOCK230_NPCMODE_PLAYERFOLLOW = 4,
    MOCK230_NPCMODE_PLAYERFACE = 5,
    MOCK230_NPCMODE_PLAYERFACECLOSE = 6,
    MOCK230_NPCMODE_OPPLAYER1 = 7,
    MOCK230_NPCMODE_OPPLAYER5 = 11,
    MOCK230_NPCMODE_APPLAYER1 = 12,
    MOCK230_NPCMODE_APPLAYER5 = 16,
};

struct Mock230Npc
{
    int active;
    int type;
    int x, z, level;
    int spawn_x, spawn_z, spawn_level;
    int wander_radius;
    /** The content block this npc was spawned from, or the engine defaults.
     *  Never NULL on an active npc; owned by mock230_content.c. */
    const struct Mock230NpcDef* def;
    /** RSMod/xrsps parity: idle NPCs try to roam every 15-30 ticks. */
    int next_roam_tick;

    /** Packed zone index **plus one** — 0 means "filed nowhere". Maintained by
     *  `mock230_zone_sync_npcs`, which reconciles rather than hooks because an
     *  npc's tile is written from five places. See `refile` in mock230_zone.c. */
    int zone_index;

    /** Filled in by the tick, consumed by the encoder, then cleared. */
    int step_dir; /* -1 when the npc did not move this tick */

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
    int damage;
    int damage_type;
    int hitpoints;
    int max_hitpoints;

    /* `tracked` was here — one flag saying "the client knows about this npc",
     * which with two clients is two different answers. It is
     * `Mock230Player.npc_tracked[slot]` now, and the sites that used to clear it
     * on despawn/respawn/death no longer need to: the encoder derives the whole
     * set from `active` and range every tick, per player, so an npc that goes
     * away is removed from each client's list on the tick it goes away and
     * re-added as new when it comes back. */

    /** A script parked on this npc by npc_delay, resumed by phase 4. */
    struct SSVM_State* active_script;
    /** Tick at which the npc stops being delayed. */
    int delayed_until;

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
    } queue[MOCK230_NPC_QUEUE_MAX];

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
     *  and a different meaning — read `mock230_combat_npc_tick` for how a
     *  logged-out target is answered. */
    int combat_target;
    int attack_clock;
    /** Tick the death animation started; -1 while alive. The npc stays visible
     *  until it expires, which is what makes a death read as a death rather
     *  than as the npc vanishing. */
    int death_tick;
    /** Tick to respawn at the spawn tile; -1 when not waiting. */
    int respawn_tick;
    /** Resolved from the cache's sequence names at spawn; -1 = play nothing. */
    int attack_seq;
    int block_seq;
    int death_seq;
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
mock230_npc_face_player(
    struct Mock230Npc* npc,
    int pid)
{
    int id = MOCK230_FACE_PLAYER_BASE + pid;

    if( npc->face_entity == id )
        return;
    npc->face_entity = id;
    npc->masks |= MOCK230_NMASK_FACE_ENTITY;
}

struct Mock230Player
{
    /*
     * The world this player is in, and where its bytes go.
     *
     * Both used to live on `struct Mock230Server` — the session as a field, the
     * world implicitly by being the same struct. They are here because a packet
     * is addressed to *a player*: with more than one, "send the inventory" has
     * to know whose. `session` is NULL for a player with no client attached,
     * which is what the selftest runs and what makes every encoder exercisable
     * without a socket.
     */
    struct Mock230Server* world;
    struct Mock230Session* session;

    /**
     * 1 while this slot holds a player.
     *
     * The pool is never compacted, so this — not `player_count` — is what says
     * whether `players[i]` is anybody. A logout clears it and leaves the hole.
     */
    int active;

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
     * `MOCK230_PLAYER_MAX` keeps unreachable rather than the pid allocator
     * having to know.
     */
    int pid;

    int x, z, level;
    /** Whether this tick's steps are being run rather than walked. Derived
     *  each tick from `run_toggle` and whether any energy is left. */
    int running;
    /** The player's standing preference: the run orb, and the ctrl-held flag
     *  the client puts on a move request. Mirrored into varp 173 so the orb
     *  draws itself lit. */
    int run_toggle;
    /** 0..MOCK230_RUN_ENERGY_MAX. */
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
    /** Percent / grams last put on the wire, so UPDATE_RUNENERGY and
     *  UPDATE_RUNWEIGHT go out only when the orb would actually change. */
    int run_energy_sent;
    int run_weight_sent;

    struct Mock230Step steps[MOCK230_STEP_MAX];
    int step_count;
    int step_head;

    /** Absolute destination of the walk in progress, for the arrival check
     *  that clears the client's map flag. -1 when idle. */
    int dest_x, dest_z;

    /** What this walk is *for*, resolved once per tick by phase 5. */
    struct Mock230Interaction interaction;

    struct Mock230Item inv[MOCK230_INV_SLOTS];
    struct Mock230Item worn[MOCK230_WORN_SLOTS];

    /** Per-slot "changed since the last flush" bits, so a tick sends one
     *  UPDATE_INV_PARTIAL with only what moved. Owned by the two registry rows
     *  that adopt them (mock230_container.h), not written directly. */
    uint32_t inv_dirty;
    uint32_t worn_dirty;

    /** Every container this player holds, including the backpack, the worn set
     *  and the bank. mock230_container.h; freed by
     *  mock230_container_shutdown_player before the struct is cleared. */
    struct Mock230Container containers[MOCK230_CONTAINER_MAX];

    /** Which extended-info fields to send. Cleared in phase 11. */
    uint32_t masks;
    int anim_id;
    int anim_delay;
    int face_entity;
    int face_x;
    int face_z;
    char say[80];
    int spotanim_id;
    int spotanim_height_delay;
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
    int tracked[MOCK230_TRACKED_NPC_MAX];
    int tracked_count;
    uint8_t npc_tracked[MOCK230_NPC_MAX];

    /** The same pair for other players. `tracked_players` holds pool indices
     *  (pids), in the order PLAYER_INFO's tracked section writes them. */
    int tracked_players[MOCK230_PLAYER_MAX];
    int tracked_player_count;
    uint8_t player_tracked[MOCK230_PLAYER_MAX];

    /*
     * ── Zones ────────────────────────────────────────────────────────
     *
     * `ground_sent[MOCK230_GROUND_MAX]` was here: one flag per ground slot per
     * client, rescanned flat every tick. It is gone, and what replaced it is
     * not a smaller flag array — it is `loaded_zones`. "Has this client been
     * told about that obj" turns out to be the wrong question; the right one is
     * "does this client hold that zone", because the answer covers the locs and
     * the replay too, and because it is the question the wire asks.
     *
     * `active_zones` is the 7x7 window around the player clipped to the build
     * area, recomputed when `zone_index` changes. `loaded_zones` is the subset
     * the client has been sent state for. See mock230_zone.h.
     */
    int active_zones[MOCK230_ZONE_ACTIVE_MAX];
    int active_zone_count;
    int loaded_zones[MOCK230_ZONE_ACTIVE_MAX];
    int loaded_zone_count;
    /** Packed zone this client was last in, or -1. */
    int zone_index;

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
     * right field and is not: `mock230_zone_player_reset` sets it to -1 on every
     * rebuild and on every climb (four call sites), so a trigger latch hung off
     * it would re-fire `[zone,…]` whenever the world's origin moved under a
     * standing player, and swallow the matching `[zoneexit]`. Nothing else may
     * write these two; only `mock230_world_update_map` does.
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

    /** Player variables. No dirty list beside them: `mock230_world_mark_varp`
     *  encodes the packet at the point of the write, the way the reference's
     *  `Player.setVar` does, so that a script's own ordering against
     *  `if_opensub` survives. */
    int32_t varps[MOCK230_VARP_COUNT];

    /** The bank: container 95 plus the settings its interface reads out of
     *  varbits. Heap-allocated, so mock230_bank_shutdown has to run before the
     *  player struct is cleared. See mock230_bank.h. */
    struct Mock230Bank bank;

    /** The script parked on this player, resumed by phase 5. At most one: a
     *  player is doing one thing at a time, which is also why a new trigger
     *  arriving while one is parked has to be refused rather than queued. */
    struct SSVM_State* active_script;
    /** Tick at which the player stops being delayed. */
    int delayed_until;

    struct Mock230Queued queue[MOCK230_QUEUE_MAX];
    /** `Player.engineQueue`. Zone triggers only; see `enum Mock230QueueKind`. */
    struct Mock230Queued engine_queue[MOCK230_ENGINE_QUEUE_MAX];
    struct Mock230Timer timers[MOCK230_TIMER_MAX];

    /** Component uids that will release a p_pausebutton wait. Cleared whenever
     *  a script finishes, so a stale button cannot resume the next one. */
    int resume_buttons[MOCK230_RESUME_BUTTON_MAX];
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
     * and `last_int` the number a p_countdialog collected. All -1 / 0 when the
     * last trigger did not carry one, which is the same thing the reference
     * does — a script reading one it was not given gets a sentinel, not a
     * stale value from an unrelated click.
     */
    int last_slot;
    int last_targetslot;
    int last_item;
    int last_verb;
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
     * See `mock230_scripts_run_opheldu`.
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
     * drift: mock230_world_set_display_name is the one place that writes both.
     * 0 for a player whose name never arrived (the selftest's, before it is
     * given one), which the service rejects as an invalid name.
     */
    int64_t name37;

    /**
     * The reference's `socialProtect`: one social packet per tick, spent by the
     * first of the six that arrives and cleared in phase 11 (the reference
     * clears it in `resetEntity`). Read and written only through
     * mock230_friends_social_gate.
     */
    int social_protect;

    /* Combat. `hitpoints` / `max_hitpoints` live with the DAMAGE mask fields
     * above, since the mask is what carries them to the client. */
    /** Npc slot being fought, or -1. */
    int combat_target;
    int attack_clock;

    /*
     * Skills. `level` is the base level, `boosted` what a potion or a drain
     * left it at, and `xp` is in tenths of a point — OldSchool's hitpoints
     * award is 4/3 of the damage dealt, which is not an integer, and rounding
     * it every hit loses a third of a point per swing.
     *
     * The hitpoints *stat* and the player's hitpoints are one thing:
     * `boosted[STAT_HITPOINTS]` IS `hitpoints`, kept in step by
     * mock230_combat_sync_hitpoints, because the client's health orb reads the
     * stat and the hitsplat's health bar reads the DAMAGE mask.
     */
    int stat_level[MOCK230_STAT_COUNT];
    int stat_boosted[MOCK230_STAT_COUNT];
    int stat_xp_tenths[MOCK230_STAT_COUNT];
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
 * Every script the *engine* starts, resolved once when the pack loads.
 *
 * The engine does not spell a script's name at a call site. A name is content's
 * identifier, and a literal in C is the same category of mistake as an anim id
 * in C — docs/CONTENT_ARCHITECTURE.md §8.6 is the whole argument. This is the
 * shape `mock230_ids` already uses for interface and varbit names, for the same
 * reason and with the same property: one place, checked at boot, loud when it is
 * wrong.
 *
 * Loud is the point. Every helper that ran a proc by name treated an unknown
 * name as "do nothing, quietly" — so renaming a script broke no build, failed no
 * test and logged nothing outside `--verbose`; it deleted a feature. The worst
 * case was `[queue,player_death]`, where a typo means a player who dies is a
 * corpse forever.
 *
 * A *trigger* is still the better answer where one exists: `[login,_]`,
 * `[opnpc1..5,<npc>]` and `[ai_queue3,<npc>]` reach content with no name in C at
 * all, because the engine names an event and content names itself. What is here
 * is the residue — the places this server does work LostCity's content does, and
 * so has to call a proc the reference's engine never calls.
 */
struct Mock230Hooks
{
    /* Combat. */
    const struct SSVM_Script* player_death;
    const struct SSVM_Script* combat_defend_anim;
    const struct SSVM_Script* combat_levelup_message;
    const struct SSVM_Script* player_melee_swing;
    const struct SSVM_Script* npc_meleeattack;
    const struct SSVM_Script* combat_weapon_type;

    /* Equipment. */
    const struct SSVM_Script* equipment_refresh;
    const struct SSVM_Script* equipment_open;

    /*
     * Friend presence.
     *
     * The reference has no equivalent and never could: its 2004 client derived
     * "X has logged in." for itself from the world-id transitions in
     * UPDATE_FRIENDLIST, so no server ever worded it. At rev 230 the client
     * dropped that derivation and the notification arrives as a server
     * MESSAGE_GAME — which makes the sentence the server's to say, and a
     * sentence the server says is content's to word
     * (CONTENT_ARCHITECTURE.md §8.2(a)).
     *
     * Both take one string, the display name. The engine supplies the name
     * because only the engine knows it; it supplies nothing else, and in
     * particular it does not decide whether anything is said at all — a tree
     * that does not define these procs is a tree whose players are not
     * notified, which is a policy a content author can now choose.
     *
     * Addressed per *follower*: see social_notify_followers in
     * mock230_world.c, which is what makes `mes` reach the right chatbox.
     */
    const struct SSVM_Script* friend_login_notification;
    const struct SSVM_Script* friend_logout_notification;
};

struct Mock230Server
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
     * A pool rather than a single embedded `struct Mock230Player`. That field
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
    struct Mock230Player players[MOCK230_PLAYER_MAX];
    int player_count;

    /*
     * World-owned containers — the `scope=shared` half of the registry.
     *
     * Structurally empty today and that is the honest state: `scope` is decoded
     * from LostCity's *server-side* inv.dat, and this tree has no `fields/inv.ini`
     * and no `[namespace:inv]` for it to live in, so mock230_container_scope
     * classifies everything as per-player. The table and the branch exist so
     * that adding the classifier is a one-function change rather than a rewrite
     * of every resolve site — which is what the three-case `container_for` would
     * have forced. See mock230_container.h.
     */
    struct Mock230Container world_containers[MOCK230_WORLD_CONTAINER_MAX];
    /** Whose turn it is — see the header comment. Never "the player". */
    struct Mock230Player* active_player;

    int tick;

    /** 1 once mock230_world_init has built the scene and the entities. Both
     *  hosts call that on every login, so this is what stops the second one
     *  respawning the roster under the first player. */
    int world_built;

    /** Origin zone of the scene every client currently holds, and the window
     *  `mock230_scene_build` keeps collision for. One per world rather than one
     *  per player: the scene builder is a singleton, so two players far enough
     *  apart would rebuild it under each other. §6.1 step 3 is the fix. */
    int zone_x, zone_z;

    struct Mock230Npc npcs[MOCK230_NPC_MAX];
    /** One past the highest slot ever spawned into. The per-tick phases walk
     *  this rather than the pool: the cap is a memory ceiling now, not the
     *  roster, and iterating 2048 slots to find 63 npcs would make it read like
     *  one. */
    int npc_slot_max;

    /**
     * The world cut into 8x8 zones — entity lists, loc records and this tick's
     * event buffers. Opaque; owned by mock230_zone.c, which is where the whole
     * design is written down. This is the durable record of every loc mutation
     * in the world, because the scene is not: it is re-read from the cache
     * whenever the origin moves.
     */
    struct Mock230ZoneMap* zone_map;

    /* The npc tracking set is per *player* — `Mock230Player.tracked` — because
     * NPC_INFO's deltas are relative to whoever is being written to. It lived
     * here while the pool held one, and encoded the same npcs for everybody. */

    /* `rebuild_pending`, `login_pending` and `clear_map_flag` were here. All
     * three describe one client's session rather than the world's state, and
     * with a pool they have to: a second player logging in must not re-run the
     * first one's [login], and one player's walk ending must not clear the
     * other's map flag. They are on `Mock230Player` now. */

    /** Deterministic per-connection RNG so a session replays identically. */
    uint32_t rng;

    int verbose;

    /** Non-NULL only under the selftest; see mock230_capture_begin. */
    struct Mock230Capture* capture;

    /** Objs on the floor. Flat rather than zone-bucketed: 256 entries scanned
     *  once a tick is nothing, and a zone index would be the only structure in
     *  the mock that has to be kept consistent under a rebuild. */
    struct Mock230GroundObj ground[MOCK230_GROUND_MAX];

    /**
     * The ground obj the *next* trigger dispatch should make active, as
     * `mock230_world_obj_handle` encodes it. 0 means none.
     *
     * One-shot, and set by the dispatch site immediately before it calls
     * `mock230_scripts_run_trigger`. `[opobj<n>]` is the only trigger with an
     * obj subject, so widening the shared `run_trigger` signature — which all
     * nineteen of its call sites would then pass 0 to — buys nothing.
     * `run_trigger_script` consumes and clears it, and the call site clears it
     * again, so a lookup that finds no script cannot leak it into the next
     * trigger. The npc slot rides as a parameter instead only because it
     * predates this.
     */
    intptr_t pending_active_obj;

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
        int slots[MOCK230_NPC_MAX];
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
     * rebuild: `mock230_scene_build` frees the loc array and re-reads it from
     * the cache, so a revert armed before a rebuild used to fire against
     * whatever loc had inherited its index.
     */
    struct Mock230LocRevert
    {
        int active;
        /** Ticks remaining. */
        int delay;
        /** What to put back: -1 means "remove this loc again" (undo a loc_add). */
        int loc_id;
        int shape;
        int angle;
        int x, z, level;
    } loc_reverts[MOCK230_LOC_REVERT_MAX];

    /** Scripts parked by world_delay, drained by phase 1. */
    struct
    {
        struct SSVM_State* state;
        int delay;
        int active;
    } world_queue[MOCK230_WORLD_QUEUE_MAX];

    /* Scripts. Opaque so mock230.h does not pull the whole VM into every
     * translation unit; owned by mock230_scripts.c. */
    struct SSVM_Provider* scripts;
    struct SSVM_Env* script_env;
    /** 0 when no script pack loaded. Every trigger site falls back to its
     *  hardcoded C behaviour, so the mock stays usable without content. */
    int scripts_ok;
    /** Resolved once when that pack loads. See struct Mock230Hooks. */
    struct Mock230Hooks hooks;
};

/* ------------------------------------------------------------------ */
/* World (mock230_world.c)                                             */
/* ------------------------------------------------------------------ */

/** Cache the scene reads its map squares from. Set from main() beside the
 *  other cache loaders. */
void
mock230_world_set_cache_dir(const char* dir);
const char*
mock230_world_cache_dir(void);

/* ------------------------------------------------------------------ */
/* Varbits (mock230_varbit.c)                                          */
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
mock230_varbit_load(const char* cache_dir);
void
mock230_varbit_free(void);

/** Read a varbit out of the player's varps. 0 when the id is unknown. */
int
mock230_varbit_get(
    const struct Mock230Player* player,
    int varbit_id);

/**
 * Write a varbit, patching the bits inside its base varp and marking that varp
 * for transmission. Returns the base varp id, or -1 when the varbit is unknown
 * — which every caller must tolerate: a cache without the record is a cache
 * this content does not fit, not a crash.
 */
int
mock230_varbit_set(
    struct Mock230Server* srv,
    int varbit_id,
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
mock230_varbit_carrier_bits(int varp);

/** Non-zero while a varbit write is patching its base varp. That write is the
 *  correct way to touch a carrier, so the backstop must not count it. */
int
mock230_varbit_patching(void);

/**
 * Whole-varp writes to a carrier varp seen so far, and the last one's varp id.
 *
 * A counter rather than an abort: the engine's job at that point is to keep
 * running and be audited, and the selftest asserts the count is zero. Reset by
 * `mock230_world_carrier_writes_reset`.
 */
int
mock230_world_carrier_writes(int* out_last_varp);
void
mock230_world_carrier_writes_reset(void);

/** Recompute the two varbits interface 593 builds itself from — the equipped
 *  weapon's category and the player's combat level. Call after anything that
 *  changes either. */
void
mock230_world_sync_combat_varbits(struct Mock230Server* srv);

/** A varp id by symbol, from content/pack/varp.pack. -1 when unknown, which
 *  every caller must treat as "do not write" — an undeclared varp is not an
 *  error, it is a content tree that does not use that variable. */
int
mock230_world_varp(const char* symbol);

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
mock230_world_attack_style(const struct Mock230Server* srv);

/** Set it, and mark it for transmission. */
void
mock230_world_set_attack_style(
    struct Mock230Server* srv,
    int style);

/** Write a player variable and queue it for phase 10, skipping the write when
 *  the value is unchanged (a varp that did not change must not be sent — the
 *  client re-runs every script listening on it). */
/*
 * A script wrote a varp directly (SS_OP_POP_VARP), bypassing the setter.
 *
 * `%varp = value` must still trigger whatever engine state hangs off that varp,
 * and it cannot simply call `mock230_world_set_varp`: assignment marks the varp
 * for transmission even when the value is unchanged (the reference's semantics,
 * and what makes `%option_nodef = %option_nodef;` "resync varp" mean anything),
 * while the setter early-returns on an equal write. So the transmission half
 * stays in the opcode and the side-effect half comes through here.
 */
void
mock230_world_varp_written(
    struct Mock230Server* srv,
    int varp,
    int value);

void
mock230_world_set_varp(
    struct Mock230Server* srv,
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
mock230_world_mark_varp(
    struct Mock230Player* player,
    int varp);

/** The tile a session logs in on, and respawns at. Set from main() before
 *  mock230_world_init; defaults to the Lumbridge castle courtyard. */
void
mock230_world_set_home(
    int tile_x,
    int tile_z);

/**
 * Take a pool slot and hand it its session.
 *
 * **Call before mock230_world_login**, and before anything encodes: the session
 * exists as soon as the handshake does, the world does not, and an encoder with
 * no player has nowhere to write. `session` may be NULL — a world with no
 * client, which is what the selftest runs.
 *
 * Returns NULL when the pool is full, which a host must treat as "refuse this
 * connection" rather than as a reason to overwrite somebody. The returned player
 * is also left as `srv->active_player`, because everything the caller does next
 * (the login burst, the display name) is that player's.
 */
struct Mock230Player*
mock230_world_add_player(
    struct Mock230Server* srv,
    struct Mock230Session* session);

/**
 * Release a slot, and take the player out of everyone else's view.
 *
 * The removal is not "stop encoding them": every other client is holding a pid
 * that has to be retired explicitly, or a later player taking the same slot
 * inherits the corpse. `mock230_send_player_info` does the retiring; this is
 * what tells it to, by clearing `active`.
 */
void
mock230_world_remove_player(
    struct Mock230Server* srv,
    struct Mock230Player* player);

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
mock230_world_set_active(
    struct Mock230Server* srv,
    struct Mock230Player* player);

/**
 * Reset one player to a newly-created character, on the home tile.
 *
 * Split out of `mock230_world_init` (which now does the *world*: scene, npcs,
 * ground objs, and runs once) because a second login must not respawn the npc
 * roster or move everyone's scene. Preserves `world`, `session`, `pid` and
 * `active` across the clear, all of which the caller set.
 */
void
mock230_world_player_init(struct Mock230Player* player);

/**
 * Copy the login name onto the player. **Call after mock230_world_player_init**,
 * which memsets the player struct — writing the name before it is what used to
 * make `displayname` report nothing.
 */
void
mock230_world_set_display_name(
    struct Mock230Player* player,
    const char* name);

/**
 * Build the world: the scene at this origin zone, the npc roster and the map
 * squares' ground objs.
 *
 * **Idempotent by design, and it has to be**: both hosts call it on every login
 * because a second player arriving must not respawn the roster, move the scene
 * or return every taken spawn. The second and later calls do nothing and say so
 * under `MOCK230_VERBOSE`. `mock230_world_reset` is what a test uses to get a
 * fresh world deliberately.
 */
void
mock230_world_init(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z);

/** Drop the world so the next mock230_world_init rebuilds it. For the selftest,
 *  which runs many worlds in one process. */
void
mock230_world_reset(struct Mock230Server* srv);

/** Advance one 600 ms tick: movement, npc roaming, then every packet the tick
 *  produces (rebuild, player info, npc info, container deltas, tick end). */
void
mock230_world_tick(struct Mock230Server* srv);

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
mock230_world_handle(
    struct Mock230Player* player,
    int name,
    const uint8_t* payload,
    int len);

/** The on-login burst for one player: scene, gameframe, containers, stats,
 *  first info tick. */
void
mock230_world_login(struct Mock230Player* player);

/**
 * Put `loc_id` on (x, z, level) with this shape, or remove what is there when
 * `loc_id < 0`. Returns 0 when the id is not in the cache or the tile is outside
 * the built scene, in which case nothing changed.
 *
 * This is the one door every runtime loc mutation goes through, and it does
 * three things that have to happen together: move the scene's collision, record
 * the change in the ZoneMap, and queue the zone event. It replaced
 * `mock230_world_broadcast_loc`, which did only the last of the three and did it
 * by walking the player pool — correct for everyone standing there at the time
 * and invisible to everyone else, forever, because a broadcast has no memory.
 *
 * `(x, z, level, shape)` is the key rather than a scene slot because that is
 * what LOC_ADD_CHANGE and LOC_DEL carry, and because a scene slot does not
 * survive a rebuild.
 */
int
mock230_world_loc_set(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle);

/** Re-apply every recorded loc change to a scene that has just been rebuilt
 *  from the cache. Without this the server forgets its own doors whenever the
 *  origin moves — which it did, despite a comment claiming otherwise. */
void
mock230_world_locs_reapply(struct Mock230Server* srv);

/** Put the player on an absolute tile, clearing the walk and rebuilding the
 *  scene if the destination left the current one. */
void
mock230_world_teleport(
    struct Mock230Server* srv,
    int level,
    int abs_x,
    int abs_z);

/* ------------------------------------------------------------------ */
/* World map (mock230_worldmap.c)                                      */
/* ------------------------------------------------------------------ */

/** Arm the orb's and the close button's ops with IF_SETEVENTS. Without this the
 *  client never sends the click — rev 230 has no clickable-by-default. */
void
mock230_worldmap_login(struct Mock230Server* srv);

/** Mount / unmount interface 595 in the toplevel's floater slot. */
void
mock230_worldmap_open(struct Mock230Server* srv);
void
mock230_worldmap_close(struct Mock230Server* srv);

/** Claim an IF_BUTTON<op> aimed at the orb or the map's close button. Returns
 *  1 when it was one of those, 0 to let the normal button routing have it. */
int
mock230_worldmap_handle_button(
    struct Mock230Server* srv,
    int uid,
    int op);

/** CLICK_WORLD_MAP: the player clicked a tile on the open map. */
void
mock230_worldmap_click(
    struct Mock230Server* srv,
    int level,
    int abs_x,
    int abs_z);

/** Once per tick: refresh the "you are here" marker while the map is open. */
void
mock230_worldmap_tick(struct Mock230Server* srv);

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
mock230_world_selftest(void);

/** Absolute tile of scene-local (0,0) for a scene whose origin zone is `zone`.
 *  Entity coordinates in the info streams are relative to this. */
static inline int
mock230_scene_origin(int zone)
{
    return (zone - 6) * 8;
}

/** Direction index for a one-tile step, in the client's World_CoordStep
 *  numbering: 0 NW, 1 N, 2 NE, 3 W, 4 E, 5 SW, 6 S, 7 SE. -1 when (dx, dz) is
 *  not a single tile. */
int
mock230_step_direction(
    int dx,
    int dz);

/** The tile delta a direction index moves by — `mock230_step_direction`
 *  inverted. (0, 0) for anything outside 0..7. */
void
mock230_step_delta(
    int dir,
    int* dx,
    int* dz);

/* ------------------------------------------------------------------ */
/* Sequence names (mock230_seqinfo.c)                                  */
/* ------------------------------------------------------------------ */

/** Index every sequence's debug name. Returns the count, 0 when absent. */
int
mock230_seqinfo_load(const char* cache_dir);

void
mock230_seqinfo_free(void);

/** Sequence id for an exact debug name, or -1. */
int
mock230_seq_by_name(const char* name);

/**
 * A sequence's animation priority — cache opcode 5, `forcedpriority` in the
 * unpacked configs, default 5 for a record that omits it.
 *
 * This is the number `mock230_anim_play_*` compares. It is NOT the record's
 * `priority` field (opcode 10) or its `precedence` (opcode 9), both of which
 * are client-side rendering concerns; the reference's `SeqType.priority`, the
 * one its `playAnimation` gate reads, decodes opcode 5.
 */
int
mock230_seq_priority(int seq_id);

/* ------------------------------------------------------------------ */
/* Animation (mock230_combat.c)                                        */
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
mock230_anim_play_npc(
    struct Mock230Npc* npc,
    int seq_id,
    int delay);

int
mock230_anim_play_player(
    struct Mock230Player* player,
    int seq_id,
    int delay);

/* ------------------------------------------------------------------ */
/* Combat (mock230_combat.c)                                           */
/* ------------------------------------------------------------------ */

/** Is this npc type a valid combat target? Decided by the cache's own menu ops,
 *  the same test the client's minimenu makes. */
int
mock230_combat_attackable(int npc_type);

/**
 * Stop fighting, and tell the client to stop facing.
 *
 * The two halves are one action: dropping `combat_target` without sending a
 * FACE_ENTITY of -1 leaves the client turned toward a corpse forever, because
 * the mask is a latch — nothing un-faces an entity except being told to. That
 * is the whole of "facing never clears after clicking away".
 */
void
mock230_combat_stop_player(struct Mock230Server* srv);

/** The same for a named player, which the npc-death path needs: it has to end
 *  the fight for everyone attacking the corpse, not only for whoever's turn it
 *  is. */
void
mock230_combat_stop_player_at(struct Mock230Player* player);
void
mock230_combat_stop_npc(
    struct Mock230Server* srv,
    int slot);

/** The player's combat level, by OldSchool's melee formula. Shared by the
 *  aggression check and the combat tab's `combat_level` varbit. */
int
mock230_combat_level(const struct Mock230Player* player);

/** Mark a stat as changed so phase 10 flushes it. */
void
mock230_combat_stat_mark(
    struct Mock230Player* player,
    int stat);

/** Keep the hitpoints stat and the player's hitpoints in step. Call after any
 *  change to either; they are one number in two places. */
void
mock230_combat_sync_hitpoints(struct Mock230Player* player);

/** Award experience, in tenths of a point. Levels up and marks the stat. */
void
mock230_combat_add_xp(
    struct Mock230Server* srv,
    int stat,
    int tenths);

/** Engage an npc in melee: face it, walk beside it, and start swinging. */
void
mock230_combat_engage(
    struct Mock230Server* srv,
    int slot);

/** Apply damage and the hitsplat that carries it. A zero amount is a block
 *  splat, not nothing — otherwise a miss looks like a dropped swing. */
void
mock230_combat_hit_npc(
    struct Mock230Server* srv,
    int slot,
    int type,
    int amount);
void
mock230_combat_hit_player(
    struct Mock230Server* srv,
    int type,
    int amount);

/**
 * Re-path the player to its combat target, before phase 5 moves it.
 *
 * The reference's `pathToTarget()`, in the reference's position: a step aimed
 * after the move is a step aimed at where the target used to be, and against a
 * target that moves that is enough to stop a fight ever starting.
 */
void
mock230_combat_player_approach(struct Mock230Server* srv);

/** Called from tick phases 5, 4 and 1 respectively. */
void
mock230_combat_player_tick(struct Mock230Server* srv);
void
mock230_combat_npc_tick(
    struct Mock230Server* srv,
    int slot);
void
mock230_combat_respawn_tick(struct Mock230Server* srv);

/* ------------------------------------------------------------------ */
/* Shared world helpers (mock230_world.c)                              */
/* ------------------------------------------------------------------ */

/** Deterministic roll in [lo, hi]. */
int
mock230_random(
    struct Mock230Server* srv,
    int lo,
    int hi);

/** An npc reached zero hitpoints: run its drop table and leave the loot. */
/** Spawn an npc and return its slot, or -1. `npc_add`'s entry point. */
int
mock230_world_npc_spawn(
    struct Mock230Server* srv,
    int type,
    int x,
    int z,
    int level);

void
mock230_world_npc_died(
    struct Mock230Server* srv,
    int slot);

/** When a just-appeared npc may first consider roaming, staggered so a room
 *  spawned on one tick does not step in unison. Spawn and respawn both use it. */
void
mock230_world_npc_roam_stagger(
    struct Mock230Server* srv,
    struct Mock230Npc* npc);


/** Drop an obj on the floor. `duration` is ticks, or -1 for a permanent spawn.
 *  Returns the ground slot, or -1 when the floor is full. */
int
mock230_world_obj_add(
    struct Mock230Server* srv,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration);

/** The first active ground obj of `obj_id` on that tile, or -1. */
int
mock230_world_ground_find(
    struct Mock230Server* srv,
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
mock230_world_ground_take(
    struct Mock230Server* srv,
    int slot);

/**
 * How a script holds a ground obj across a suspension.
 *
 * `slot + 1` in the low bits, the slot's `generation` above them, so that a
 * resumed script either finds the obj it was acting on or finds none —
 * `mock230_world_ground_slot` is the other half. Never 0 for a valid slot,
 * because the VM's active-entity pointer uses NULL to mean "no obj".
 *
 * The `+ 1` matches the npc and loc conventions; the generation does not exist
 * for those two and is the difference between a scene slot (whose contents a
 * rebuild replaces wholesale, which `active` already catches) and a 256-entry
 * free list that hands the same index to the next drop.
 */
intptr_t
mock230_world_obj_handle(
    struct Mock230Server* srv,
    int slot);

/** The ground slot a handle names, or -1 when that obj is gone. */
int
mock230_world_ground_slot(
    struct Mock230Server* srv,
    intptr_t handle);

/** Drop the player's queued route. */
void
mock230_world_steps_clear(struct Mock230Player* player);

/** Queue a walk to a tile adjacent to (x, z) rather than onto it. */
void
mock230_world_walk_beside(
    struct Mock230Server* srv,
    int x,
    int z);

/**
 * The tile to stand on to reach (x, z), approaching from (from_x, from_z).
 *
 * An orthogonal neighbour of the target, nearest the approacher and one it can
 * stand on — melee cannot reach a diagonal, so this is where "squaring up"
 * comes from. Shared by the player's walk and the npc chase so both approach
 * the same way.
 */
void
mock230_world_beside_tile(
    int level,
    int from_x,
    int from_z,
    int x,
    int z,
    int* out_x,
    int* out_z);

/**
 * One tile of an npc's walk toward (target_x, target_z), routing around
 * anything in the way. Returns 1 when it moved.
 *
 * This is the npc half of `mock230_scene_route` — the reference's
 * `pathToTarget()` + `updateMovement()` at one tile a tick — and the only mover
 * an npc has: the chase, the follow modes and the walk home all go through it.
 */
int
mock230_world_npc_walk_to(
    struct Mock230Npc* npc,
    int target_x,
    int target_z);

/* ------------------------------------------------------------------ */
/* Interactions (mock230_world.c)                                      */
/* ------------------------------------------------------------------ */

/** Latch what the player is trying to do. The walk is the caller's; resolving
 *  it is mock230_world_process_interaction's. */
void
mock230_world_interaction_set(
    struct Mock230Server* srv,
    enum Mock230InteractionKind kind,
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
mock230_world_interaction_clear(struct Mock230Server* srv);

/**
 * Shut whatever modal is up: the chatbox dialogue, the main slot and the side
 * slot, plus the script parked on the dialogue's `p_pausebutton`.
 *
 * The reference's `Player.closeModal()`. `[if_close,<iface>:0]` still gets first
 * refusal on the main slot, which is how the bank closes itself.
 */
void
mock230_world_close_modal(struct Mock230Server* srv);

/** `Player.closeModal(clearWeakQueue)`. The `false` form has exactly one caller
 *  in the reference and exactly one here: the automatic chat close when a parked
 *  script finishes (`Player.executeScript`), which must not discard a weak queue
 *  the finished script may have just filled. */
void
mock230_world_close_modal_ex(
    struct Mock230Server* srv,
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
mock230_world_clear_pending_action(struct Mock230Server* srv);

/**
 * Resolve the pending interaction if it is in range, else leave it walking.
 *
 * Called twice per interaction at least: once by the packet handler that set it
 * (so clicking something you are standing next to acts immediately) and once per
 * tick from phase 5 after movement.
 */
void
mock230_world_process_interaction(struct Mock230Server* srv);

/* ------------------------------------------------------------------ */
/* Scripts (mock230_scripts.c)                                         */
/* ------------------------------------------------------------------ */

/**
 * Load a compiled script pack.
 *
 * Returns the number of scripts, or 0 when there is no pack — which *is* an
 * error now, and says so in a banner. It used to be a supported mode on the
 * grounds that "every trigger site falls back to the C behaviour it had before
 * scripts existed"; that promise is what `enum Mock230Fallback` withdraws. A
 * server with no pack does not answer triggers out of C, because a whole
 * parallel implementation of the game running silently is worse than a server
 * that visibly does nothing.
 *
 * Build the pack with `make -C src mock230-scripts`. A fresh checkout has none:
 * the compiler's output is gitignored.
 */
int
mock230_scripts_load(
    struct Mock230Server* srv,
    const char* dir);

void
mock230_scripts_free(struct Mock230Server* srv);

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
mock230_scripts_report_gaps(struct Mock230Server* srv);

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
enum Mock230TriggerResult
{
    /** Nothing bound at any rung — type, category, or the `_` wildcard. */
    MOCK230_TRIGGER_NONE = 0,
    /** A script ran, and either finished or parked. */
    MOCK230_TRIGGER_RAN = 1,
    /** A script was bound and did not complete: it aborted, declared arguments
     *  a trigger cannot supply, or had nowhere to park. */
    MOCK230_TRIGGER_FAILED = 2
};

/**
 * The engine behaviours that still answer a trigger nothing is bound to.
 *
 * Every one is C standing in for something LostCity states in content, and every
 * one is here only because the ServerScript surface cannot yet say it — the
 * ~3,200 lines `osrs230_mockserver.md` §6.1 step 5 calls blocked. They are
 * *enumerated* rather than written inline at their call sites so that the set is
 * countable: `mock230_scripts_report_fallbacks` names them at boot, and the
 * selftest pins how many there are. One added quietly is the failure this enum
 * exists to prevent, in the same spirit as the ten named hooks in
 * `mock230_scripts.c` (PORTING_GUIDE §2.4 item 5).
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
 *   `mock230_scripts_stale_blockers` fails the moment a cited opcode is
 *   implemented. Before touching a row, re-check its blocker against the tree;
 *   assume nothing in it is still true.
 */
enum Mock230Fallback
{
    /** `[opnpc<n>]` → "Attack" engages combat, anything else greets
     *  (`interaction_engine_npc`; the greeting itself is content already). */
    MOCK230_FALLBACK_OPNPC = 0,
    /** `[oploc<n>]` → doors, bank booths, stairs and ladders
     *  (`interaction_engine_loc`). */
    MOCK230_FALLBACK_OPLOC,
    /** `[inv_button<n>]` on a bank component → the bank's own router, reached
     *  through the quantity ladder `mock230_bank_quantity_for_op`. */
    MOCK230_FALLBACK_INV_BUTTON,
    /** `[if_button]` → the bank's settings/deposit router,
     *  `mock230_bank_handle_button`. */
    MOCK230_FALLBACK_IF_BUTTON,
    /*
     * `[if_close]` is deliberately not here. It was, in the sense that the
     * engine's unmount ran only when no script was bound — and that is not what
     * the trigger means. `Player.closeModal` runs the close script *and then*
     * unmounts unconditionally; an interface the player closed closes. See
     * `mock230_world_close_modal`.
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
    MOCK230_FALLBACK_COUNT
};

/**
 * Run the script bound to a trigger, resolving it the way the reference does.
 *
 * `ScriptProvider.getByTrigger`: the exact `type`, then the `category`, then the
 * bare `_` wildcard, and nothing after that. Pass -1 for a subject that does not
 * apply. Returns an `enum Mock230TriggerResult`.
 *
 * `npc_slot` is the npc the trigger is about, or -1. It becomes the script's
 * active npc, which is what `npc_say` and friends operate on.
 *
 * A miss reports itself under `MOCK230_VERBOSE`, in the reference's own words
 * (`no trigger for [opnpc2,goblin]`), because a trigger that does nothing is now
 * the *designed* outcome and is otherwise indistinguishable from a dropped
 * packet.
 */
int
mock230_scripts_run_trigger(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot);

/**
 * One rung, no chain — `ScriptProvider.getByTriggerSpecific`.
 *
 * `type` if it is not -1, else `category` if it is not -1, else the global form.
 * The reference uses this where a wildcard would be actively wrong: `[login,_]`
 * is global by construction, and an `[if_button,_]` that swallowed every click
 * on every interface is not a fallback anyone wants.
 */
int
mock230_scripts_run_trigger_specific(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot);

/**
 * May this call site run its engine fallback for `result`?
 *
 * 1 only when `result` is `MOCK230_TRIGGER_NONE` *and* a script pack is loaded.
 * Both halves are the inversion:
 *
 * - A `MOCK230_TRIGGER_FAILED` is not a gap in content, it is a bug in content.
 *   Running the C body would hide it behind a plausible-looking game.
 * - With no pack at all, nothing is a gap because everything is. A server that
 *   answered every trigger out of C would be a second implementation of the
 *   game, silently different from the one the content tree describes, and the
 *   only way to notice would be to find a behaviour that disagreed. It says so
 *   at boot and then does nothing.
 *
 * Reports which fallback ran, and why one did not, under `MOCK230_VERBOSE`.
 */
int
mock230_scripts_fallback(
    struct Mock230Server* srv,
    enum Mock230Fallback which,
    int result);

/** Name every live engine fallback and what it is blocked on. Returns the
 *  count, which is `MOCK230_FALLBACK_COUNT` and shrinks as content grows.
 *  Also runs `mock230_scripts_stale_blockers` and prints any hit. */
int
mock230_scripts_report_fallbacks(struct Mock230Server* srv);

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
mock230_scripts_stale_blockers(void);

/**
 * The cache menu verb this engine answers itself for `[<trigger>,<subject>]`,
 * or NULL if it answers none.
 *
 * The inverse question to `mock230_scripts_fallback`'s. That one asks "did
 * content claim this at runtime"; this asks, of a binding, "was there an engine
 * behaviour here to claim" — which is answerable at load, before any player has
 * clicked anything. `subject` is an exact type id; a category or wildcard
 * binding names no record and gets NULL.
 */
const char*
mock230_world_engine_claimed_verb(
    int trigger,
    int32_t subject);

/** Name every trigger content binds over a verb the engine answers itself and
 *  does not re-issue. Returns the count; 0 is the state to keep. Triage §7.7. */
int
mock230_scripts_report_shadowed_ops(struct Mock230Server* srv);

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
mock230_scripts_run_if_button(
    struct Mock230Server* srv,
    int uid,
    int op_num);

/**
 * Dispatch a coordinate-subject trigger — the `zone`/`mapzone` family — by name.
 *
 * These four are the only triggers whose subject is a *place*, and a place is
 * not a type id: the reference formats `[zone,<level>_<mx>_<mz>_<lx>_<lz>]` and
 * `[mapzone,0_<mx>_<mz>]` and asks `ScriptProvider.getByName`
 * (`NetworkPlayer.updateMap` → `Player.ts`). `mock230_scripts_run_trigger`
 * takes an integer subject and cannot express that, which is the whole reason
 * this exists — the same reason `mock230_scripts_run_if_button` has a
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
mock230_scripts_run_trigger_at(
    struct Mock230Server* srv,
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
 * Returns MOCK230_TRIGGER_RAN when a script was found and queued (it has not
 * run yet), MOCK230_TRIGGER_NONE when nothing is bound — the same silent miss.
 */
int
mock230_scripts_queue_trigger_at(
    struct Mock230Server* srv,
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
 * `mock230_scripts_run_trigger` (that resolves one `(type, category)` pair).
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
 * Pass -1 for an obj with no category. Returns `enum Mock230TriggerResult`; a
 * miss is the caller's to answer, and the answer is content's
 * `[proc,nothing_interesting_message]` — never an engine fallback, because
 * there is no engine use-on behaviour for one to fall back to.
 */
int
mock230_scripts_run_opheldu(
    struct Mock230Server* srv,
    int obj_type,
    int obj_category,
    int use_obj_type,
    int use_obj_category);

/**
 * Run a `::command` as `[debugproc,<name>]`, with the words after it as its
 * declared arguments. Returns 1 when content claimed the line.
 *
 * The reference's own arrangement — see ClientCheatHandler — and the reason a
 * cheat need not be an engine change. `::pray 18` reaches Protect from Melee
 * through the same `~prayer_toggle` the button does.
 */
int
mock230_scripts_run_debugproc(
    struct Mock230Server* srv,
    const char* line);

/** Resume anything parked whose wait is over. Called by tick phases 1, 4 and 5. */
void
mock230_scripts_resume_world(struct Mock230Server* srv);
void
mock230_scripts_resume_npc(
    struct Mock230Server* srv,
    int slot);
void
mock230_scripts_resume_player(struct Mock230Server* srv);

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
mock230_scripts_process_queues(struct Mock230Server* srv);
void
mock230_scripts_process_timers(struct Mock230Server* srv);
/**
 * Drain the engine queue — the zone family — on `srv->active_player`.
 *
 * Runs *after* the timers, which is where `World.processPlayers` calls
 * `processEngineQueue()`. Entries carry delay 0, so an entry queued by phase 10
 * of tick N runs in phase 5 of tick N+1 unless the player is busy, in which case
 * it waits rather than being dropped.
 */
void
mock230_scripts_process_engine_queue(struct Mock230Server* srv);

/** Discard every WEAK queue entry. Called by `mock230_world_close_modal`, which
 *  is the only thing that clears them (`Player.closeModal`). */
void
mock230_scripts_clear_weak_queue(struct Mock230Player* player);

/**
 * Release a p_pausebutton wait.
 *
 * Returns 1 when the click matched a registered resume button and the script
 * continued, 0 otherwise — an unmatched click must leave the script parked.
 */
int
mock230_scripts_resume_button(
    struct Mock230Server* srv,
    int component_uid);

/**
 * Release a p_countdialog wait with the number the client sent.
 *
 * Separate from mock230_scripts_resume_button: the two waits are released by
 * different packets and neither may release the other — a click arriving while
 * a count dialog is up must leave the script parked.
 */
int
mock230_scripts_resume_countdialog(
    struct Mock230Server* srv,
    int32_t value);

/**
 * End a script parked on a dialogue, because the interface it is blocked on is
 * being taken away. Returns 1 when one was actually discarded.
 *
 * Only a `p_pausebutton` / `p_countdialog` wait: those two are the ones whose
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
mock230_scripts_close_dialogue(struct Mock230Server* srv);

/** Start a script by id on behalf of the player. For the selftest and for
 *  anything the engine reaches by id rather than by trigger. Returns 1 when a
 *  script ran or parked. */
int
mock230_scripts_run_script(
    struct Mock230Server* srv,
    int script_id);

/** Run a named content proc immediately with int arguments (`name` includes the
 *  brackets, e.g. "[proc,give_combat_experience]"). The seam for keeping policy
 *  the reference expresses as a proc in content instead of as a C switch.
 *  Returns 1 when the proc ran; 0 for a missing script or an arity mismatch. */
int
mock230_scripts_run_proc(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc);

/** Run a named proc with an npc made active, so `npc_stat`/`npc_param`/
 *  `npc_damage` inside it resolve to that npc. The combat swing needs it. */
int
mock230_scripts_run_proc_on_npc(
    struct Mock230Server* srv,
    const char* name,
    int npc_slot);

/** Say a content-owned message: runs `[proc,<name>]`, optionally with one
 *  string argument (an obj or skill name the engine had to look up). Silent
 *  when content does not define it — never a C fallback, which is how two
 *  copies of a sentence come to disagree. */
void
mock230_say(
    struct Mock230Server* srv,
    const char* name,
    const char* arg);

/** As above, with string arguments too — for a message content should word but
 *  only the engine knows a name for. `strv` entries are copied by the VM's
 *  string pool, so a caller may pass a stack buffer. */
int
mock230_scripts_run_proc_sv(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc);

/** Value-returning, with string arguments. */
int
mock230_scripts_run_proc_int_sv(
    struct Mock230Server* srv,
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
mock230_scripts_run_proc_int(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc,
    int32_t* out);

/** Queue a named script (`name` includes the brackets, e.g.
 *  "[queue,playerhit_n_retaliate]") on the player, `delay` ticks out, carrying
 *  one int argument. Lets the engine start an exchange content finishes.
 *  Returns 1 when queued; 0 for a missing script or a full queue. */
int
mock230_scripts_queue_named(
    struct Mock230Server* srv,
    const char* name,
    int delay,
    int32_t arg);

/* ------------------------------------------------------------------ */
/* Engine hooks (mock230_scripts.c)                                    */
/* ------------------------------------------------------------------ */

/*
 * The same six calls, addressed by a resolved hook instead of by a name.
 *
 * These are what the *engine* uses; the by-name forms above are for tests,
 * which name the script they are testing on purpose. `struct Mock230Hooks` has
 * the argument for the split, and the practical difference is that a hook that
 * does not resolve is reported once at boot rather than doing nothing forever.
 *
 * A NULL hook is a no-op with the same return as a missing name, so a tree that
 * ships without one still runs.
 */
int
mock230_scripts_run_hook(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc);

int
mock230_scripts_run_hook_sv(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc);

int
mock230_scripts_run_hook_int(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    int32_t* out);

int
mock230_scripts_run_hook_int_sv(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out);

int
mock230_scripts_run_hook_on_npc(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    int npc_slot);

int
mock230_scripts_queue_hook(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    int delay,
    int32_t arg);

/** Fill `srv->hooks` from the loaded pack, reporting every name that is not
 *  there. Called by mock230_scripts_load; returns the number missing. */
int
mock230_scripts_resolve_hooks(struct Mock230Server* srv);

/** The host command seam: every opcode the VM does not implement itself. */
int
mock230_script_command(
    struct SSVM_State* state,
    int opcode,
    int dot);

/*
 * Per-domain opcode handlers (`mock230_ops_*.c`).
 *
 * `mock230_script_command` offers each the opcode in turn; each returns 1 when it
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
mock230_ops_db(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `*_param` family. See mock230_ops_param.c. */
int
mock230_ops_param(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `loc_*` / `lc_*` config reads. See mock230_ops_loc.c. The loc family's
 *  *mutating* half stays in mock230_scripts.c's switch, with the scene and the
 *  revert queue it needs. */
int
mock230_ops_loc(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `npc_*` / `nc_*` config reads and the hunt iterators. See
 *  mock230_ops_npc.c. Addressing, lifecycle and the mode machine stay in
 *  mock230_scripts.c's switch, with the world state they mutate. */
int
mock230_ops_npc(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The `obj_*` family — the reads and the removal of the *active ground obj* —
 *  plus the `oc_wearpos*` config reads. See mock230_ops_obj.c. `obj_add` stays
 *  in mock230_scripts.c's switch: it takes a coord rather than an active obj
 *  and is already implemented there. */
int
mock230_ops_obj(
    struct SSVM_State* state,
    int opcode,
    int dot);

/** The inv *moves* — `inv_movefromslot`, `inv_dropslot` and the `inv_moveitem`
 *  family. See mock230_ops_inv.c. The rest of the inv family stays in
 *  mock230_scripts.c's switch; new inv opcodes land in the domain file. */
int
mock230_ops_inv(
    struct SSVM_State* state,
    int opcode,
    int dot);

/*
 * Push a param onto the stack its *declaration* calls for — the shared seam of
 * the whole `*_param` family, defined in mock230_ops_param.c.
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
mock230_push_typed_param(
    struct SSVM_State* state,
    int param_id,
    const char* sval,
    int32_t ival,
    int present,
    const char* record_kind,
    int record_id);

/* ------------------------------------------------------------------ */
/* Encoders (mock230_encode.c)                                         */
/* ------------------------------------------------------------------ */

/** Frame + ISAAC-scramble one packet and write it to the socket. `var` is 0
 *  (fixed), 1 (var-u8) or 2 (var-u16). */
void
mock230_send(
    struct Mock230Player* player,
    int opcode,
    const uint8_t* payload,
    int len,
    int var);

void
mock230_send_rebuild_normal(struct Mock230Player* player);
void
mock230_send_if_opentop(
    struct Mock230Player* player,
    int group);
void
mock230_send_if_opensub(
    struct Mock230Player* player,
    int parent,
    int child,
    int group,
    int type);
void
mock230_send_if_setevents(
    struct Mock230Player* player,
    int uid,
    int from,
    int to,
    int events);
/** RUNCLIENTSCRIPT: run a CS2 clientscript on the client with int arguments.
 *  The world map's `worldmap_transmitdata` is the one the mock needs — it is
 *  how the server tells the map where the player is standing. */
void
mock230_send_run_clientscript(
    struct Mock230Player* player,
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
mock230_send_run_clientscript_mixed(
    struct Mock230Player* player,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc);
/* Interface setters. `uid` is the packed (interface << 16) | child. */
void
mock230_send_if_settext(
    struct Mock230Player* player,
    int uid,
    const char* text);
void
mock230_send_if_setnpchead(
    struct Mock230Player* player,
    int uid,
    int npc_id);
void
mock230_send_if_setplayerhead(
    struct Mock230Player* player,
    int uid);
void
mock230_send_if_setanim(
    struct Mock230Player* player,
    int uid,
    int anim_id);
void
mock230_send_if_sethide(
    struct Mock230Player* player,
    int uid,
    int hide);
void
mock230_send_if_closesub(
    struct Mock230Player* player,
    int uid);
/** Record a mount into (or out of) the gameframe's modal slots. Called by the
 *  IF_OPENSUB / IF_CLOSESUB encoders, so no opener has to remember to; `group`
 *  is 0 for a close. CLOSE_MODAL is what reads it back. */
void
mock230_note_modal_mount(
    struct Mock230Server* srv,
    int uid,
    int group);
/** Open the "Enter amount" prompt. Zero payload; the answer arrives as
 *  RESUME_P_COUNTDIALOG. */
void
mock230_send_if_opencountdialog(struct Mock230Player* player);

void
mock230_send_varp_small(
    struct Mock230Player* player,
    int id,
    int value);
/** p2 id, p4 value — for anything VARP_SMALL's signed byte cannot hold. */
void
mock230_send_varp_large(
    struct Mock230Player* player,
    int id,
    int value);

void
mock230_send_stat(
    struct Mock230Player* player,
    int stat,
    int level,
    int xp,
    int boosted);
/** Tell the client which player index it is. Sent once, in the login burst. */
void
mock230_send_update_pid(
    struct Mock230Player* player,
    int local_pid);

void
mock230_send_run_energy(
    struct Mock230Player* player,
    int percent);
void
mock230_send_run_weight(
    struct Mock230Player* player,
    int kilograms);
void
mock230_send_message(
    struct Mock230Player* player,
    const char* text);
void
mock230_send_unset_map_flag(struct Mock230Player* player);
void
mock230_send_tick_end(struct Mock230Player* player);

/*
 * Social. The five server->client packets of the friend / ignore / private-chat
 * feature; docs/FRIENDS_PRIVATE_CHAT.md §3.2 is the wire table and
 * src/net/mock/mock230_friends.h is the service that decides what goes in them.
 *
 * These encoders decide nothing. In particular `world` below is already the
 * answer `isVisibleTo` gave — 0 means "offline OR not visible to this viewer",
 * and the encoder cannot tell which, deliberately, because the client cannot
 * either.
 */

/** One friend-list entry: p8 name37, p1 world. Both the login dump (one packet
 *  per friend) and every delta afterwards use this. */
void
mock230_send_update_friendlist(
    struct Mock230Player* player,
    int64_t name37,
    int world);

/** The whole ignore list: p8 name37 * count. The client replaces its store
 *  wholesale, so there is no single-entry form. */
void
mock230_send_update_ignorelist(
    struct Mock230Player* player,
    const int64_t* names37,
    int count);

/** 0 loading, 1 connecting, 2 online. Sent once, after the login dump. */
void
mock230_send_friendlist_loaded(
    struct Mock230Player* player,
    int status);

/** An incoming private message. `message_id` must be non-zero — the client
 *  dedupes against a zero-filled ring and drops a 0. */
void
mock230_send_message_private(
    struct Mock230Player* player,
    int64_t from37,
    int32_t message_id,
    int staff_mod,
    const char* text);

/** The three chat filter modes, echoed back so the client's UI agrees with the
 *  server's copy. */
void
mock230_send_chat_filter_settings(
    struct Mock230Player* player,
    int public_mode,
    int private_mode,
    int trade_mode);

/** Whole container. `component` is the packed (interface << 16 | child) uid the
 *  container binds to. */
void
mock230_send_inv_full(
    struct Mock230Player* player,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count);

/** Only the slots whose bit is set in `dirty`. No-op when `dirty` is 0. */
void
mock230_send_inv_partial(
    struct Mock230Player* player,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count,
    uint32_t dirty);

/*
 * Zone updates.
 *
 * A zone sub-packet carries no coordinate — only `pos`, the tile's offset inside
 * an 8x8 zone. Which zone that is comes from the UPDATE_ZONE_* packet before it,
 * and the client keeps it as state, so the header and the sub-packets are only
 * ever correct together. Callers are mock230_zone.c and nothing else.
 */
/** Name a zone as the target of the sub-packets that follow. `full` picks
 *  UPDATE_ZONE_FULL_FOLLOWS, which also resets the client's memory of the zone;
 *  otherwise UPDATE_ZONE_PARTIAL_FOLLOWS, which does not. Coordinates are in
 *  zone units. */
void
mock230_send_zone_header(
    struct Mock230Player* player,
    int zone_x,
    int zone_z,
    int full);

/** One sub-packet, applied to whichever zone was last named. */
void
mock230_send_zone_sub(
    struct Mock230Player* player,
    const struct Mock230ZoneEvent* event);

/** The same sub-packet, encoded into a caller-owned buffer instead of sent —
 *  this is what makes a zone's shared blob shared. Returns the bytes written. */
int
mock230_encode_zone_sub(
    uint8_t* dst,
    int max,
    const struct Mock230ZoneEvent* event);

/** A whole shared blob as one UPDATE_ZONE_PARTIAL_ENCLOSED. */
void
mock230_send_zone_enclosed(
    struct Mock230Player* player,
    int zone_x,
    int zone_z,
    const uint8_t* blob,
    int len);

/** Phase 8: put expired loc mutations back. */
void
mock230_world_loc_reverts(struct Mock230Server* srv);

/** Schedule a revert `duration` ticks out. `duration <= 0` means never, and
 *  `loc_id < 0` means "remove it again", which is how a `loc_add` expires. */
int
mock230_world_loc_revert_queue(
    struct Mock230Server* srv,
    int duration,
    int loc_id,
    int shape,
    int angle,
    int x,
    int z,
    int level);

void
mock230_send_player_info(struct Mock230Player* player);
void
mock230_send_npc_info(struct Mock230Player* player);

#endif
