#ifndef SRC_REPLAY_REPLAY_LEDGER_INTERNAL_H
#define SRC_REPLAY_REPLAY_LEDGER_INTERNAL_H

/* Shared by replay_ledger.c (the build, zones, keyframes), replay_ledger_entity.c
 * (the entity fold) and replay_ledger_verify.c. Not for anyone else. */

#include "replay/replay_ledger.h"

#include "net/rev/gameproto_revisions.h"
#include "net/rsprox_recording.h"
#include "streams/rsprot_info.h"

#include <stddef.h>
#include <stdint.h>

/** The client's npc list cap (rs_entity_sync.h RS_ENTITY_SYNC_MAX_NPCS). */
#define REPLAY_LEDGER_MAX_NPCS 2048
#define REPLAY_LEDGER_PLAYER_SLOTS RSPROT_INFO_PLAYER_SLOTS

/** One npc in the client's list. */
struct ReplayLedgerNpc
{
    int32_t slot;
    int32_t type;
    int32_t spawn_type;
    int32_t x;
    int32_t z;
    int32_t level;
    uint64_t sticky[REPLAY_NPC_STICKY_COUNT];
};

struct ReplayEntityState
{
    /** Frames [0, next_frame) are applied; the state is in front of `tick`. */
    uint32_t next_frame;
    int tick;
    RsprotPlayerInfo pi;
    /** Sticky references per player slot; REPLAY_STICKY_NONE for every slot
     *  not in high resolution (cleared as the player leaves). */
    uint64_t player_sticky[REPLAY_LEDGER_PLAYER_SLOTS][REPLAY_PLAYER_STICKY_COUNT];
    int npc_count;
    struct ReplayLedgerNpc npcs[REPLAY_LEDGER_MAX_NPCS];
    int npc_origin_valid;
    int npc_origin_x;
    int npc_origin_z;
    /** 0 between a SET_ACTIVE_WORLD naming a world entity and the tick end. */
    int active_root;
};

/** A serialised key: everything a state holds, players and npcs packed. */
struct ReplayLedgerKeyPlayer
{
    uint16_t pid;
    uint64_t sticky[REPLAY_PLAYER_STICKY_COUNT];
};

struct ReplayLedgerKey
{
    int tick;
    uint32_t next_frame;
    RsprotPlayerInfo* pi;
    int player_count;
    struct ReplayLedgerKeyPlayer* players;
    int npc_count;
    struct ReplayLedgerNpc* npcs;
    int npc_origin_valid;
    int npc_origin_x;
    int npc_origin_z;
};

/** A PLAYER_INFO sticky op, copied: its frame cannot be decoded again without
 *  the table as it stood then. An appearance blob is a reference into the
 *  recording (`blob_order` the RsprotInfoPrim DATA order it was sent in) when
 *  its bytes stand there, else a copy in `blobs` (`blob_order` -1). */
struct ReplayLedgerPlayerOp
{
    uint64_t ref;
    RsprotInfoOp op;
    uint32_t blob_offset;
    int32_t blob_len;
    int8_t blob_order;
};

struct ReplayLedgerZoneClear
{
    uint32_t frame;
    /** The zone's south-west tile, absolute, on the header's plane. */
    uint32_t tile;
};

struct ReplayLedger
{
    struct RsproxRecording const* rec;
    struct GameProtoRevTable const* rev;
    RsprotPlayerInfoReadFn player_read;

    int tick_count;
    /** tick_count + 1 entries: first_frame[t]; first_frame[tick_count] is the
     *  frame count. */
    uint32_t* first_frame;
    /** tick_count + 1 entries. */
    uint32_t* local_tile;
    /** tick_count + 1 entries. */
    uint8_t* world_entity;

    struct ReplayLedgerScene* scenes;
    int scene_count;
    int scene_cap;
    /** Per scene: the table a login REBUILD seeds (its parse seeds the
     *  session's; ledger_parse_rebuild hands that over), NULL otherwise. */
    RsprotPlayerInfo** login_tables;

    struct ReplayZoneOp* ops;
    int op_count;
    int op_cap;
    int live_max;
    /** checkpoint c covers ops [0, c * REPLAY_LEDGER_CHECKPOINT_OPS): the ones
     *  among them still alive after the last of them was born, as op indices
     *  in order: cp_ops[cp_first[c] .. cp_first[c + 1]). */
    int checkpoint_count;
    uint32_t* cp_first;
    uint32_t* cp_ops;

    struct ReplayLedgerZoneClear* clears;
    int clear_count;

    struct ReplayLedgerKey* keys;
    int key_count;

    struct ReplayLedgerPlayerOp* player_ops;
    int player_op_count;
    int player_op_cap;
    uint8_t* blobs;
    size_t blob_used;
    size_t blob_cap;

    struct ReplayLedgerStats stats;
};

/* --- replay_ledger_entity.c ------------------------------------------------ */

/** Apply frame `f` (== state->next_frame) to the entity state. `building` is
 *  the ledger under construction (it records PLAYER_INFO sticky copies and
 *  counts), NULL when advancing a finished ledger. */
void
ledger_entity_step(
    struct ReplayLedger const* ledger,
    struct ReplayLedger* building,
    struct ReplayEntityState* state,
    uint32_t f);

void
ledger_entity_reset(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state);

void
ledger_key_take(
    struct ReplayEntityState const* state,
    struct ReplayLedgerKey* key);

void
ledger_key_release(struct ReplayLedgerKey* key);

size_t
ledger_key_bytes(struct ReplayLedgerKey const* key);

/** Seed the state's table from a login REBUILD's init block: the frame's
 *  parse put it in the session table, which the caller hands over here. */
void
ledger_entity_seed_table(
    struct ReplayEntityState* state,
    RsprotPlayerInfo const* seeded);

/** Fill `kf`'s table, players, npcs and their sticky ops from `state`
 *  (pointers copied into its arena or pointing at the recording / the
 *  ledger). Returns 1, 0 when a sticky reference no longer resolves. */
int
ledger_entity_keyframe(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState const* state,
    struct ReplayWorldKeyframe* kf);

/* --- replay_ledger.c ------------------------------------------------------- */

/** The scene in force in front of frame `f`, -1 when none. */
int
ledger_scene_at(
    struct ReplayLedger const* ledger,
    uint32_t f);

/** The GameProtoPktName of frame `f`; PKT_NAME_NONE for RSProx's own two. */
int
ledger_frame_name(
    struct ReplayLedger const* ledger,
    uint32_t f);

/** Parse frame `f` through rev->parse_wire. Returns 1 with `out` filled (free
 *  with gameproto_free), 0 when it does not parse. Never a REBUILD_NORMAL /
 *  REGION (see ledger_parse_rebuild). */
int
ledger_parse(
    struct ReplayLedger const* ledger,
    uint32_t f,
    struct RevPacket* out);

/** Copy `len` bytes into `kf`'s arena. Returns where, as offset + 1 (0 for
 *  len 0): the arena grows, so op pointers into it are written as these and
 *  fixed up once every op is in (ledger_kf_fixup). */
uintptr_t
ledger_kf_put(
    struct ReplayWorldKeyframe* kf,
    void const* src,
    size_t len);

/** Turn the arena offsets ledger_kf_put returned back into pointers. */
void
ledger_kf_fixup(struct ReplayWorldKeyframe* kf);

/** Append an op to `kf`'s list; returns it. */
RsprotInfoOp*
ledger_kf_op(struct ReplayWorldKeyframe* kf);

#endif
