#ifndef SRC_NET_MOCK_MOCK230_H
#define SRC_NET_MOCK_MOCK230_H

/*
 * Mock OSRS rev-230 game server: shared state and the seam between its four
 * translation units.
 *
 *   mock230_main.c     socket, RSA/ISAAC login handshake, the 600 ms tick loop
 *   mock230_world.c    game state — movement, NPCs, containers, equipment
 *   mock230_encode.c   every server->client packet
 *   mock230_objinfo.c  obj metadata (name / wearpos / stackable) from the cache
 *
 * The server is authoritative in the way a real one is: the client asks to
 * walk, to wear, to swap two inventory slots, and nothing moves until a packet
 * comes back saying it did. That is the whole point of the mock — it exercises
 * the client's server-driven paths rather than its local-prediction ones.
 *
 * Wire encoding lives entirely in mock230_encode.c and goes through
 * 3rd/rsareabuf. See docs/osrs230_mockserver.md for the protocol notes and for
 * which parts deviate from a real rev-230 server.
 */

#include "net/isaac.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

enum
{
    MOCK230_NPC_MAX = 96,
    /* The client sends at most 25 waypoints per move request; a walk can then
     * be interpolated over many tiles, so the step queue is larger. */
    MOCK230_STEP_MAX = 256,
    MOCK230_INV_SLOTS = 28,
    MOCK230_WORN_SLOTS = 14,

    /* Container ids the client's InvManager knows (INV_MANAGER_CONTAINER_*). */
    MOCK230_INV_BACKPACK = 93,
    MOCK230_INV_WORN = 94,

    /* Gameframe root (manifest_osrs230.ini ui:boot interface_id) and the two
     * component slots the inventory and equipment containers bind to. */
    MOCK230_ROOT_IFACE = 161,
    MOCK230_INV_IFACE = 149,
    MOCK230_WORN_IFACE = 387,

    /* Scene is 104x104 tiles based at (zone - 6) * 8. Rebuild once the player
     * comes within 16 tiles of an edge, the same margin the reference uses. */
    MOCK230_SCENE_TILES = 104,
    MOCK230_REBUILD_MARGIN = 16,

    /* Player variables the scripts can read and write. rev 230 has far more
     * than this; the mock only needs the ones its own content uses. */
    MOCK230_VARP_COUNT = 256,
    MOCK230_VARP_DIRTY_MAX = 32,

    /* Parked script bookkeeping. */
    MOCK230_QUEUE_MAX = 16,
    MOCK230_TIMER_MAX = 8,
    MOCK230_WORLD_QUEUE_MAX = 16,
    MOCK230_RESUME_BUTTON_MAX = 8,

    /* Combat. Four ticks is the standard melee interval; the rest are the
     * mock's own tuning, not values read from anywhere. */
    MOCK230_ATTACK_SPEED = 4,
    MOCK230_ATTACK_RANGE = 1,
    /* Ticks the corpse stays up after the death animation starts. */
    MOCK230_DEATH_TICKS = 3,
    /* Ticks from despawn to respawn at the spawn tile. */
    MOCK230_RESPAWN_TICKS = 25,
    MOCK230_PLAYER_MAX_HP = 30,

    /* Hitsplat types the client's hitmark renderer knows. */
    MOCK230_HIT_DAMAGE = 0,
    MOCK230_HIT_BLOCK = 1,
    /* IF_SETEVENTS bit 0: the component accepts a plain click, answered with
     * IF_BUTTON. Mirrors RS_MINIMENU_EVENT_CLICK on the client. */
    MOCK230_EVENT_CLICK = 0x1,

    /* rev-230 dialogue interfaces, verified with tools/dump_interface.
     * 162:559 is the chat container and ships hidden=1, so opening a dialogue
     * has to unhide it as well as mount into 162:561 — a 506x129 layer, which
     * is exactly interface 231's root size. */
    MOCK230_CHAT_CONTAINER_UID = (162 << 16) | 559,
    MOCK230_CHAT_SLOT_UID = (162 << 16) | 561,

    /* Wire sentinels in the classic info streams. */
    MOCK230_PLAYER_TERMINATOR = 2047,
    MOCK230_NPC_TERMINATOR = 16383,

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
    /** Primary equipment slot, or -1 when the item cannot be worn. */
    int wearpos;
    /** Extra slots the item occupies: a 2h weapon's shield slot, the
     *  hair/jaw a full helm covers. -1 when unused. */
    int wearpos_2;
    int wearpos_3;
    int stackable;
    /** Inventory menu ops (config opcodes 35-39) — "Wear", "Wield", "Drop",
     *  "Eat". OPHELD<n> carries the 1-based index into this array, so the
     *  server can act on the verb the player actually clicked. NULL = absent. */
    const char* if_ops[5];
};

/** Decode the whole obj config table once. Returns 0 when the cache is absent,
 *  in which case every lookup reports "not wearable" and the mock still runs. */
int
mock230_objinfo_load(const char* cache_dir);

void
mock230_objinfo_free(void);

/** Never NULL: unknown ids report a placeholder name and wearpos -1. */
const struct Mock230ObjInfo*
mock230_objinfo(int obj_id);

/* ------------------------------------------------------------------ */
/* NPC metadata (mock230_npcinfo.c)                                    */
/* ------------------------------------------------------------------ */

struct Mock230NpcInfo
{
    /** Never NULL from mock230_npcinfo(); unknown ids report a placeholder. */
    const char* name;
    int combat_level;
    int size;
};

/** Decode the npc config table once. Returns 0 when the cache is absent, in
 *  which case every lookup reports a placeholder name and the mock still runs. */
int
mock230_npcinfo_load(const char* cache_dir);

void
mock230_npcinfo_free(void);

const struct Mock230NpcInfo*
mock230_npcinfo(int npc_id);

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

/** A script waiting for its delay to run out. */
struct Mock230Queued
{
    int active;
    int script_id;
    /** Ticks remaining. Decremented once per tick; runs at 0. */
    int delay;
    int32_t arg;
};

/** A script that re-runs on an interval. */
struct Mock230Timer
{
    int active;
    int script_id;
    int interval;
    int clock;
};

/** One queued tile of movement, in absolute tile coordinates. */
struct Mock230Step
{
    int16_t x;
    int16_t z;
};

struct Mock230Npc
{
    int active;
    int type;
    int x, z, level;
    int spawn_x, spawn_z;
    int wander_radius;
    /** RSMod/xrsps parity: idle NPCs try to roam every 15-30 ticks. */
    int next_roam_tick;

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

    /** Set once the client has been told about this npc. Cleared by a rebuild,
     *  which drops the client's whole npc list. */
    int tracked;

    /** A script parked on this npc by npc_delay, resumed by phase 4. */
    struct SSVM_State* active_script;
    /** Tick at which the npc stops being delayed. */
    int delayed_until;

    /** [ai_timer]: re-runs every `timer_interval` ticks. -1 = none. */
    int timer_script;
    int timer_interval;
    int timer_clock;

    /* Combat. `hitpoints` / `max_hitpoints` are shared with the DAMAGE mask,
     * which carries the health bar the client draws above the hitsplat. */
    int base_hitpoints;
    /** 0 = fighting the player, -1 = not in combat. Single-player mock, so
     *  there is nothing else to target. */
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

struct Mock230Player
{
    int x, z, level;
    /** Run toggles per move request (the client sends ctrl-held with it). */
    int running;

    struct Mock230Step steps[MOCK230_STEP_MAX];
    int step_count;
    int step_head;

    /** Absolute destination of the walk in progress, for the arrival check
     *  that clears the client's map flag. -1 when idle. */
    int dest_x, dest_z;

    struct Mock230Item inv[MOCK230_INV_SLOTS];
    struct Mock230Item worn[MOCK230_WORN_SLOTS];

    /** Per-slot "changed since the last flush" bits, so a tick sends one
     *  UPDATE_INV_PARTIAL with only what moved. */
    uint32_t inv_dirty;
    uint32_t worn_dirty;

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

    /** This tick's movement, filled in by the tick and consumed by the
     *  PLAYER_INFO encoder: 0 tiles idle, 1 walking, 2 running. */
    int move_dirs[2];
    int move_count;

    /** Player variables. A changed-list rather than a dirty bitmap: a tick
     *  usually touches one or two, and the list is what the encoder walks. */
    int32_t varps[MOCK230_VARP_COUNT];
    int varp_changed[MOCK230_VARP_DIRTY_MAX];
    int varp_changed_count;

    /** The script parked on this player, resumed by phase 5. At most one: a
     *  player is doing one thing at a time, which is also why a new trigger
     *  arriving while one is parked has to be refused rather than queued. */
    struct SSVM_State* active_script;
    /** Tick at which the player stops being delayed. */
    int delayed_until;

    struct Mock230Queued queue[MOCK230_QUEUE_MAX];
    struct Mock230Timer timers[MOCK230_TIMER_MAX];

    /** Component uids that will release a p_pausebutton wait. Cleared whenever
     *  a script finishes, so a stale button cannot resume the next one. */
    int resume_buttons[MOCK230_RESUME_BUTTON_MAX];
    int resume_button_count;
    /** The component that released the last wait — what `last_com` returns. */
    int last_com;

    /* Combat. `hitpoints` / `max_hitpoints` live with the DAMAGE mask fields
     * above, since the mask is what carries them to the client. */
    /** Npc slot being fought, or -1. */
    int combat_target;
    int attack_clock;
};

struct Mock230Server
{
    int fd;
    struct Isaac* cipher_out; /* server -> client opcode scramble */
    struct Isaac* cipher_in;  /* client -> server opcode descramble */
    int tick;

    /** Origin zone of the scene the client currently holds. Absolute tile of
     *  scene-local (0,0) is (zone - 6) * 8. */
    int zone_x, zone_z;

    struct Mock230Player player;
    struct Mock230Npc npcs[MOCK230_NPC_MAX];

    /** Ordered list of npc slots the client is tracking, which is exactly the
     *  order the tracked section of NPC_INFO must be written in. */
    int tracked[MOCK230_NPC_MAX];
    int tracked_count;

    /** Latched by a handler, acted on by the next tick. */
    int rebuild_pending;
    int clear_map_flag;
    /** Set by the login burst, drained by phase 7 so [login] runs inside the
     *  tick rather than ahead of it. */
    int login_pending;

    /** Deterministic per-connection RNG so a session replays identically. */
    uint32_t rng;

    int verbose;

    /** Non-NULL only under the selftest; see mock230_capture_begin. */
    struct Mock230Capture* capture;

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
};

/* ------------------------------------------------------------------ */
/* World (mock230_world.c)                                             */
/* ------------------------------------------------------------------ */

/** Seed the player, the npc roster and the starting inventory. */
void
mock230_world_init(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z);

/** Advance one 600 ms tick: movement, npc roaming, then every packet the tick
 *  produces (rebuild, player info, npc info, container deltas, tick end). */
void
mock230_world_tick(struct Mock230Server* srv);

/** Route one decoded client packet into the game state. `name` is a canonical
 *  PKTOUT_NAME_*; `payload`/`len` exclude the opcode and any length prefix. */
void
mock230_world_handle(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len);

/** The on-login burst: scene, gameframe, containers, stats, first info tick. */
void
mock230_world_login(struct Mock230Server* srv);

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
 * Sequence for an npc by convention: `<lowercased name><suffix>`, e.g.
 * ("Goblin", "_attack") -> `goblin_attack_unarmed` = 309. -1 when the cache has
 * no such name, which every caller must treat as "play nothing".
 */
int
mock230_seq_for_npc(
    int npc_type,
    const char* suffix);

/* ------------------------------------------------------------------ */
/* Combat (mock230_combat.c)                                           */
/* ------------------------------------------------------------------ */

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

/** Queue a walk to a tile adjacent to (x, z) rather than onto it. */
void
mock230_world_walk_beside(
    struct Mock230Server* srv,
    int x,
    int z);

/* ------------------------------------------------------------------ */
/* Scripts (mock230_scripts.c)                                         */
/* ------------------------------------------------------------------ */

/**
 * Load a compiled script pack.
 *
 * Returns the number of scripts, or 0 when there is no pack — which is not an
 * error. Every trigger site falls back to the C behaviour it had before
 * scripts existed, so a broken or absent toolchain degrades the mock rather
 * than breaking it. That is what keeps `make test-mock230` green on every
 * intermediate commit.
 */
int
mock230_scripts_load(
    struct Mock230Server* srv,
    const char* dir);

void
mock230_scripts_free(struct Mock230Server* srv);

/**
 * Run the script bound to a trigger, if there is one.
 *
 * Returns 1 when a script ran to completion, 0 when none matched or it aborted
 * — in both of those cases the caller should do whatever it did before.
 *
 * `npc_slot` is the npc the trigger is about, or -1. It becomes the script's
 * active npc, which is what `npc_say` and friends operate on.
 */
int
mock230_scripts_run_trigger(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot);

/** Resume anything parked whose wait is over. Called by tick phases 1, 4 and 5. */
void
mock230_scripts_resume_world(struct Mock230Server* srv);
void
mock230_scripts_resume_npc(
    struct Mock230Server* srv,
    int slot);
void
mock230_scripts_resume_player(struct Mock230Server* srv);

/** Run due queue entries and tick the timers. */
void
mock230_scripts_process_queues(struct Mock230Server* srv);
void
mock230_scripts_process_timers(struct Mock230Server* srv);
void
mock230_scripts_process_npc_timer(
    struct Mock230Server* srv,
    int slot);

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

/** Start a script by id on behalf of the player. For the selftest and for
 *  anything the engine reaches by id rather than by trigger. Returns 1 when a
 *  script ran or parked. */
int
mock230_scripts_run_script(
    struct Mock230Server* srv,
    int script_id);

/** The host command seam: every opcode the VM does not implement itself. */
int
mock230_script_command(
    struct SSVM_State* state,
    int opcode,
    int dot);

/* ------------------------------------------------------------------ */
/* Encoders (mock230_encode.c)                                         */
/* ------------------------------------------------------------------ */

/** Frame + ISAAC-scramble one packet and write it to the socket. `var` is 0
 *  (fixed), 1 (var-u8) or 2 (var-u16). */
void
mock230_send(
    struct Mock230Server* srv,
    int opcode,
    const uint8_t* payload,
    int len,
    int var);

void
mock230_send_rebuild_normal(struct Mock230Server* srv);
void
mock230_send_if_opentop(
    struct Mock230Server* srv,
    int group);
void
mock230_send_if_opensub(
    struct Mock230Server* srv,
    int parent,
    int child,
    int group,
    int type);
void
mock230_send_if_setevents(
    struct Mock230Server* srv,
    int uid,
    int from,
    int to,
    int events);
/* Interface setters. `uid` is the packed (interface << 16) | child. */
void
mock230_send_if_settext(
    struct Mock230Server* srv,
    int uid,
    const char* text);
void
mock230_send_if_setnpchead(
    struct Mock230Server* srv,
    int uid,
    int npc_id);
void
mock230_send_if_setplayerhead(
    struct Mock230Server* srv,
    int uid);
void
mock230_send_if_setanim(
    struct Mock230Server* srv,
    int uid,
    int anim_id);
void
mock230_send_if_sethide(
    struct Mock230Server* srv,
    int uid,
    int hide);
void
mock230_send_if_closesub(
    struct Mock230Server* srv,
    int uid);

void
mock230_send_varp_small(
    struct Mock230Server* srv,
    int id,
    int value);
void
mock230_send_stat(
    struct Mock230Server* srv,
    int stat,
    int level,
    int xp);
void
mock230_send_run_energy(
    struct Mock230Server* srv,
    int percent);
void
mock230_send_run_weight(
    struct Mock230Server* srv,
    int kilograms);
void
mock230_send_message(
    struct Mock230Server* srv,
    const char* text);
void
mock230_send_unset_map_flag(struct Mock230Server* srv);
void
mock230_send_tick_end(struct Mock230Server* srv);

/** Whole container. `component` is the packed (interface << 16 | child) uid the
 *  container binds to. */
void
mock230_send_inv_full(
    struct Mock230Server* srv,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count);

/** Only the slots whose bit is set in `dirty`. No-op when `dirty` is 0. */
void
mock230_send_inv_partial(
    struct Mock230Server* srv,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count,
    uint32_t dirty);

void
mock230_send_player_info(struct Mock230Server* srv);
void
mock230_send_npc_info(struct Mock230Server* srv);

#endif
