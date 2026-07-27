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

    /* Wire sentinels in the classic info streams. */
    MOCK230_PLAYER_TERMINATOR = 2047,
    MOCK230_NPC_TERMINATOR = 16383,
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
/* Game state                                                          */
/* ------------------------------------------------------------------ */

struct Mock230Item
{
    int obj_id; /* -1 = empty slot */
    int count;
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
    int face_entity;
    int face_entity_dirty;
    char say[80];
    int say_dirty;

    /** Set once the client has been told about this npc. Cleared by a rebuild,
     *  which drops the client's whole npc list. */
    int tracked;
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

    /** Appearance is re-sent whenever equipment changes. */
    int appearance_dirty;

    /** Set after a teleport or a rebuild: the next PLAYER_INFO must carry an
     *  absolute placement (move op 3) rather than a step direction. */
    int place_dirty;

    /** This tick's movement, filled in by the tick and consumed by the
     *  PLAYER_INFO encoder: 0 tiles idle, 1 walking, 2 running. */
    int move_dirs[2];
    int move_count;
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

    /** Deterministic per-connection RNG so a session replays identically. */
    uint32_t rng;

    int verbose;
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
