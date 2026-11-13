#ifndef SRC_REPLAY_REPLAY_LEDGER_H
#define SRC_REPLAY_REPLAY_LEDGER_H

/*
 * The world ledger of a replay scrubber: what world a server would send a
 * client that arrived at tick P of an RSProx recording.
 *
 * A pure function of (indexed RsproxRecording, GameProtoRevTable): no App, no
 * client state. Built in one pass over the recording's frames; every query
 * after that reads the ledger and, for entity state between two keys, decodes
 * at most REPLAY_LEDGER_KEY_TICKS - 1 ticks of info packets again, offline.
 *
 * "At tick P" means at the boundary in front of tick P: every frame of ticks
 * 0..P-1 applied (their SERVER_TICK_ENDs included), none of tick P's.
 * ReplayLedger_FirstFrame(P) is the frame the client would execute next.
 *
 * Three parts (design: "Structure 3: the world ledger"):
 *
 *   scenes     one row per REBUILD_NORMAL / REBUILD_REGION, with its base.
 *   zone ops   every loc and ground-item change as an interval of frames,
 *              [born, died): alive at frame f when born < f <= died. What ends
 *              an interval is what the client does (replay_ledger.c, the rule
 *              table above ledger_zone_op). A stabbing query returns the ops
 *              alive at a frame in birth order, from a checkpoint list every
 *              REPLAY_LEDGER_CHECKPOINT_OPS ops.
 *   entities   a key every REPLAY_LEDGER_KEY_TICKS ticks: the player decoder's
 *              table, the high-resolution players and the npcs in the client's
 *              list order, with references to each one's sticky blocks.
 *
 * The player decoder's table here is a PRIVATE RsprotPlayerInfo, read through
 * the revision's rsprot entry point (player_info_osrs<N>); the session's table
 * in net/proto/proto_info.c (g_pi) is never read into. Parsing a login REBUILD
 * through rev->parse_wire seeds g_pi as a side effect (proto_adapt.c
 * adapt_rebuild_normal*), so the ledger saves g_pi before each REBUILD parse and
 * loads it back after; that is the only time it is touched. See the note on
 * ledger_parse_rebuild.
 *
 * World entities (sailing: WORLDENTITY_INFO, REBUILD_WORLDENTITY,
 * SET_ACTIVE_WORLD to a world entity) are not folded. A tick carrying them, or
 * any tick while a world entity is believed in view, is flagged; a caller asks
 * ReplayLedger_InWorldEntitySpan and falls back to simulating. NPC_INFO and
 * zone packets addressed to a world entity's view are skipped, so the ROOT's
 * npc list and zones stay right across a span.
 *
 * OSRS revisions only (a table with player_info_state_* and an rsprot
 * player_info_osrs<N> / osrsm<N> reader).
 */

#include "net/rev/revpacket.h"
#include "streams/rsprot_info.h"

#include <stddef.h>
#include <stdint.h>

struct RsproxRecording;
struct GameProtoRevTable;

/** `died` of a zone op nothing superseded. */
#define REPLAY_LEDGER_NEVER UINT32_MAX
/** Ticks between entity keys. */
#define REPLAY_LEDGER_KEY_TICKS 256
/** Zone ops between stabbing-query checkpoints. */
#define REPLAY_LEDGER_CHECKPOINT_OPS 1024
/** The build area's side in tiles: the scene a REBUILD loads (task_gameproto_exec.c
 *  CreateTask_WorldLoad(..., 104, ...)). */
#define REPLAY_LEDGER_SCENE_SIZE 104
/** A sticky reference to nothing. */
#define REPLAY_STICKY_NONE UINT64_MAX
/** ReplayWorldNpc.type before an NPCTYPE has named one. */
#define REPLAY_NPC_TYPE_NONE INT32_MIN

/** Pack a tile as RsprotPlayerInfo.coord does: level << 28 | x << 14 | z. */
#define REPLAY_TILE(x, z, level)                                                                   \
    ((uint32_t)(((uint32_t)(level) & 3u) << 28) | (((uint32_t)(x) & 0x3fffu) << 14) |             \
     ((uint32_t)(z) & 0x3fffu))
#define REPLAY_TILE_X(t) ((int)(((t) >> 14) & 0x3fffu))
#define REPLAY_TILE_Z(t) ((int)((t) & 0x3fffu))
#define REPLAY_TILE_LEVEL(t) ((int)(((t) >> 28) & 3u))

/** A sticky reference: the info packet's frame and the op's index in the list
 *  that packet decodes to. */
#define REPLAY_STICKY_REF(frame, op) (((uint64_t)(frame) << 16) | (uint64_t)(op))
#define REPLAY_STICKY_FRAME(ref) ((uint32_t)((ref) >> 16))
#define REPLAY_STICKY_OP(ref) ((int)((ref) & 0xffffu))

/* ------------------------------------------------------------------------ */
/* Scenes                                                                     */
/* ------------------------------------------------------------------------ */

struct ReplayLedgerScene
{
    /** The REBUILD's frame: the keyframe plays this packet verbatim. */
    uint32_t frame;
    int tick;
    /** PKT_NAME_REBUILD_NORMAL or PKT_NAME_REBUILD_REGION. */
    int name;
    int zone_x;
    int zone_z;
    /** The scene's south-west tile, absolute (rev->scene_base, or the
     *  (zone - 6) * 8 rule where the table names none). A tile is inside the
     *  build area when base <= t < base + size on both axes. */
    int base_x;
    int base_z;
    int size;
    /** REBUILD_REGION: a template grid (instance). */
    int instanced;
    /** The login REBUILD: it carried the player-info init block. */
    int login;
};

/* ------------------------------------------------------------------------ */
/* Zone ops                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * One loc or ground-item change, as the interval it is alive for. Sorted by
 * (born, sub), which is recorded order.
 */
struct ReplayZoneOp
{
    /** The frame that carried it. */
    uint32_t born;
    /** The frame that superseded it, or REPLAY_LEDGER_NEVER. born == died: it
     *  left nothing behind (an OBJ_DEL, an op that found no stack). */
    uint32_t died;
    /** Its tile, absolute, on the zone header's plane (REPLAY_TILE). */
    uint32_t tile;
    /** What it is about on that tile: a loc's layer (0..3; 0xff for a shape
     *  with none), or a ground item's stack, named by the index of the op that
     *  created the stack. */
    uint32_t key;
    /** The entry inside an UPDATE_ZONE_PARTIAL_ENCLOSED frame; 0 otherwise. */
    uint16_t sub;
    /** The zone arm it runs through (GameProtoPktName: LOC_ADD_CHANGE, LOC_DEL,
     *  OBJ_ADD, OBJ_DEL, OBJ_COUNT, OBJ_REVEAL, OBJ_CUSTOMISE, OBJ_UNCUSTOMISE);
     *  an *_SPECIFIC frame is filed under the arm the executor turns it into. */
    uint16_t name;
};

/** One zone op, decoded again, in the shape the executor's zone arm takes:
 *  `pkt` with a ZoneAt of (base_x, base_z, level) -- rs_gameproto_exec.c
 *  exec_zone_sub_packet_at, AppPendingZonePkt. */
struct ReplayWorldZoneOp
{
    struct PktZoneSubPacket pkt;
    /** The zone's south-west tile, scene-local to the scene it is decoded for. */
    int base_x;
    int base_z;
    /** The header's plane, resolved (a header with none: the local player's). */
    int level;
    /** Index into the ledger's zone ops. */
    uint32_t op;
};

/* ------------------------------------------------------------------------ */
/* Entities                                                                   */
/* ------------------------------------------------------------------------ */

/** A high-resolution player's sticky blocks (the table holds its tile and move
 *  speed). See replay_ledger_entity.c for which op kinds are sticky. */
enum ReplayPlayerSticky
{
    REPLAY_PLAYER_APPEARANCE = 0,
    REPLAY_PLAYER_FACE_ENTITY,
    /** FACE_COORD or FACE_ANGLE, whichever came last. */
    REPLAY_PLAYER_FACE_DIR,
    REPLAY_PLAYER_STICKY_COUNT
};

/** The head-icon slots NPC_HEADICON_SPECIFIC addresses (WORLD_NPC_HEADICON_SLOTS). */
#define REPLAY_NPC_HEADICON_SLOTS 8

enum ReplayNpcSticky
{
    REPLAY_NPC_FACE_ENTITY = 0,
    /** FACE_COORD or FACE_ANGLE (the spawn facing included). */
    REPLAY_NPC_FACE_DIR,
    REPLAY_NPC_SPAWN_CYCLE,
    REPLAY_NPC_VISIBLE_OPS,
    REPLAY_NPC_NAME,
    REPLAY_NPC_LEVEL,
    REPLAY_NPC_HEADICON,
    REPLAY_NPC_BODY,
    REPLAY_NPC_BAS,
    /** NPC_HEADICON_SPECIFIC, one per head-icon slot: the last since the npc
     *  entered view or since its last HEADICON_CUSTOM (which writes every
     *  slot). Its reference is (frame, 0) of that packet, not an NPC_INFO op;
     *  the keyframe hands these over as `npc_headicons`. */
    REPLAY_NPC_HEADICON_SPECIFIC,
    REPLAY_NPC_STICKY_COUNT = REPLAY_NPC_HEADICON_SPECIFIC + REPLAY_NPC_HEADICON_SLOTS
};

/** The entity half of the fold, at a tick boundary. Opaque; a few hundred KB. */
struct ReplayEntityState;

/* ------------------------------------------------------------------------ */
/* The keyframe                                                               */
/* ------------------------------------------------------------------------ */

struct ReplayWorldPlayer
{
    int pid;
    /** Absolute, from the decoder table. */
    int x;
    int z;
    int level;
    /** Its sticky ops: kf->ops[op_first .. op_first + op_count), in recorded
     *  order, to be applied as player_apply_op applies them. */
    int op_first;
    int op_count;
};

struct ReplayWorldNpc
{
    int slot;
    /** The last NPCTYPE or CHANGE_TYPE value: the client's base_npc_id. */
    int type;
    /** The NPCTYPE it entered view with. */
    int spawn_type;
    /** Absolute. The level is the origin's when it spawned (the npc stream
     *  carries none). */
    int x;
    int z;
    int level;
    int op_first;
    int op_count;
};

/** One NPC_HEADICON_SPECIFIC still in force, as the packet carried it. */
struct ReplayWorldNpcHeadicon
{
    /** The npc's server slot (the packet's npc index). */
    int npc_slot;
    int icon_slot;
    int group;
    int index;
};

/**
 * The world at a tick boundary, as plain data the executor can apply with the
 * types it already has. Arrays are owned by the keyframe
 * (ReplayWorldKeyframe_Free). Op pointers (an appearance blob, a name, a body's
 * model list) point into `bytes`, in the natural order the info readers hand
 * them over in.
 *
 * Apply order: the scene (its frame, verbatim; a login REBUILD parsed through
 * rev->parse_wire seeds the session's player table, so load `player_table`
 * AFTER it), then `zone_ops` in order, then `player_table`, then players and
 * npcs (npcs in this order: NPC_INFO addresses the list by position), then
 * `npc_headicons` through NPC_HEADICON_SPECIFIC's own arm (it finds the npc by
 * slot, so the npcs must stand first).
 */
struct ReplayWorldKeyframe
{
    int tick;
    /** The next frame to play after applying this. */
    uint32_t frame;
    /** 1: inside a world-entity span. The root's state below is right, but a
     *  world entity's is not here: the caller simulates instead. */
    int world_entity;

    /** Index into the scenes, -1 before the first REBUILD. */
    int scene;
    struct ReplayLedgerScene scene_row;

    struct ReplayWorldZoneOp* zone_ops;
    int zone_op_count;

    /** The player decoder's table, exactly as player_info_state_save writes. */
    uint8_t* player_table;
    size_t player_table_size;
    int local_index;
    uint32_t local_tile;

    struct ReplayWorldPlayer* players;
    int player_count;
    /** The client's npc list, in its order. */
    struct ReplayWorldNpc* npcs;
    int npc_count;
    /** Npc by npc, slot by slot: each writes one slot of one npc, so the order
     *  between them is not state. */
    struct ReplayWorldNpcHeadicon* npc_headicons;
    int npc_headicon_count;
    RsprotInfoOp* ops;
    int op_count;
    uint8_t* bytes;
    int bytes_used;
    int bytes_cap;
    int ops_cap;

    /** SET_NPC_UPDATE_ORIGIN as last sent (scene-local), and whether one was. */
    int npc_origin_valid;
    int npc_origin_x;
    int npc_origin_z;
};

/* ------------------------------------------------------------------------ */
/* Building and sizes                                                         */
/* ------------------------------------------------------------------------ */

struct ReplayLedgerStats
{
    int frames;
    int ticks;
    int scenes;
    int zone_ops;
    int zone_ops_live_max;
    int zone_clears;
    int checkpoints;
    int keys;
    /** One key's bytes, averaged, and the player table's share of it. */
    size_t bytes_per_key;
    size_t table_bytes;
    size_t total_bytes;
    double build_ms;
    int player_info_frames;
    int npc_info_frames;
    /** Reads that did not complete (a STOP op): the fold after them is only
     *  as good as the reader's resynchronisation. */
    int player_info_incomplete;
    int npc_info_incomplete;
    int player_sticky_copies;
    /** Appearance blobs referenced in the recording / copied into the ledger. */
    int appearance_refs;
    int appearance_copies;
    int world_entity_ticks;
    int npc_info_world_entity;
    int zone_world_entity;
    int zone_outside_scene;
    int zone_no_scene;
};

/** Build the ledger. `rec` must be indexed (RsproxRecording_Index) and its
 *  RECONNECT frames resolved (rsprox_recording_resolve_reconnects); `rev` its
 *  revision's table (RsproxRecording_Rev). Both must outlive the ledger. */
struct ReplayLedger*
ReplayLedger_Build(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev);

/** Accepts NULL. */
void
ReplayLedger_Free(struct ReplayLedger* ledger);

void
ReplayLedger_Stats(
    struct ReplayLedger const* ledger,
    struct ReplayLedgerStats* out);

int
ReplayLedger_TickCount(struct ReplayLedger const* ledger);

/** The first frame of tick `tick` (0 <= tick <= tick count; the tick count
 *  gives the frame count). */
uint32_t
ReplayLedger_FirstFrame(
    struct ReplayLedger const* ledger,
    int tick);

/* ------------------------------------------------------------------------ */
/* Queries                                                                    */
/* ------------------------------------------------------------------------ */

int
ReplayLedger_SceneCount(struct ReplayLedger const* ledger);

struct ReplayLedgerScene const*
ReplayLedger_Scene(
    struct ReplayLedger const* ledger,
    int index);

/** The scene in force in front of `frame`: the last REBUILD before it, or -1. */
int
ReplayLedger_SceneAt(
    struct ReplayLedger const* ledger,
    uint32_t frame);

int
ReplayLedger_ZoneOpCount(struct ReplayLedger const* ledger);

struct ReplayZoneOp const*
ReplayLedger_ZoneOp(
    struct ReplayLedger const* ledger,
    int index);

/** The most zone ops ever alive at once: a buffer this long holds any answer
 *  of ReplayLedger_ZoneOpsAlive. */
int
ReplayLedger_ZoneOpsAliveMax(struct ReplayLedger const* ledger);

/** The zone ops alive in front of `frame`, in birth order, as indices into the
 *  ledger's ops. Returns how many; `out` holds ReplayLedger_ZoneOpsAliveMax. */
int
ReplayLedger_ZoneOpsAlive(
    struct ReplayLedger const* ledger,
    uint32_t frame,
    uint32_t* out,
    int cap);

/** Decode zone op `index` again from the recording, for a client whose scene
 *  base is (scene_base_x, scene_base_z). Returns 1, or 0 when the frame no
 *  longer parses (it did when the ledger was built, so 0 is a broken
 *  recording or a parse that is not deterministic). */
int
ReplayLedger_ZoneOpDecode(
    struct ReplayLedger const* ledger,
    int index,
    int scene_base_x,
    int scene_base_z,
    struct ReplayWorldZoneOp* out);

/** The local player's tile in front of `tick` (REPLAY_TILE; 0 before the login
 *  seeds the table). 4 bytes a tick. */
uint32_t
ReplayLedger_LocalTile(
    struct ReplayLedger const* ledger,
    int tick);

/** 1 when `tick` carries a world-entity packet or a world entity is believed
 *  in view during it: a seek into it simulates instead of folding. */
int
ReplayLedger_InWorldEntitySpan(
    struct ReplayLedger const* ledger,
    int tick);

/** The world in front of `tick`. `out` is filled whole (free it with
 *  ReplayWorldKeyframe_Free). Returns 1, or 0 when a sticky block no longer
 *  decodes. */
int
ReplayLedger_WorldAt(
    struct ReplayLedger const* ledger,
    int tick,
    struct ReplayWorldKeyframe* out);

/** Accepts NULL; leaves `kf` zeroed. */
void
ReplayWorldKeyframe_Free(struct ReplayWorldKeyframe* kf);

/* ------------------------------------------------------------------------ */
/* Entity state, for the tests and the scrubber                               */
/* ------------------------------------------------------------------------ */

int
ReplayLedger_KeyCount(struct ReplayLedger const* ledger);

/** A state at tick 0: nothing applied, the table reset to the recording's
 *  local index. */
struct ReplayEntityState*
ReplayLedger_EntityNew(struct ReplayLedger const* ledger);

/** Accepts NULL. */
void
ReplayEntityState_Free(struct ReplayEntityState* state);

/** Load key `key` (its tick is key * REPLAY_LEDGER_KEY_TICKS) into `state`. */
void
ReplayLedger_EntityLoadKey(
    struct ReplayLedger const* ledger,
    int key,
    struct ReplayEntityState* state);

/** The advance function the keys were built with: apply every frame from the
 *  state's tick up to the boundary in front of `tick` (>= the state's). */
void
ReplayLedger_EntityAdvance(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state,
    int tick);

int
ReplayEntityState_Tick(struct ReplayEntityState const* state);

/** 1 when the two hold the same fold: table bytes, players and their sticky
 *  references, npcs in order. Otherwise 0 and a sentence in `why`. */
int
ReplayEntityState_Equal(
    struct ReplayEntityState const* a,
    struct ReplayEntityState const* b,
    char* why,
    int why_cap);

/** A public chat line a PLAYER_INFO carries (a SAY or a CHAT op): what the
 *  executor puts in the chatbox for it (task_exec_entity_info.c,
 *  player_apply_op), which a seek's chase must not lose with the frame. */
struct ReplayChatLine
{
    int pid;
    /** 1: SAY (`text`); 0: CHAT (`data`/`len`, the revision's packed text). */
    int say;
    int crown;
    int colour_effect;
    char const* text;
    uint8_t const* data;
    int len;
    /** The speaker's appearance block as the reader hands it over (natural
     *  order; NULL when the ledger has none): its name is the chat's. The one
     *  in force AT the op, as the executor applies ops in order: a player
     *  whose appearance comes later in the same packet speaks unnamed. */
    uint8_t const* appearance;
    int appearance_len;
    uint64_t appearance_ref;
};

/** Step `state` over its next frame, as ReplayLedger_EntityAdvance does, and
 *  when that frame is a PLAYER_INFO hand back its chat lines (up to `cap`).
 *  Returns how many; their pointers hold until the next call. */
int
ReplayLedger_EntityStepChat(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state,
    struct ReplayChatLine* out,
    int cap);

/** The frame `state` steps over next. */
uint32_t
ReplayEntityState_NextFrame(struct ReplayEntityState const* state);

/** The npc list, in order, for a test that knows what it should be. Returns
 *  the count; fills up to `cap` slots. */
int
ReplayEntityState_NpcSlots(
    struct ReplayEntityState const* state,
    int* slots,
    int cap);

#endif
