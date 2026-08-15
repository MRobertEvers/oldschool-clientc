#ifndef SRC_NET_REV_PKT_PLAYER_INFO_H
#define SRC_NET_REV_PKT_PLAYER_INFO_H

/*
 * PLAYER_INFO command-stream decoder (port of v0 pkt_player_info with the
 * gaps filled from the official revision-239 gamepack deob: EXACT_MOVE
 * decoded, CHAT payload consumed and carried). The decode emits a flat op
 * array the exec task walks; it is pure
 * CPU — applying the ops (spawns, appearance model builds) is where cache IO
 * happens, inside the packet's task.
 *
 * Extended-info masks are revision-specific; the v5 layout is taken from the
 * official gamepack and checked against the authoritative server writer.
 */

#include <stdbool.h>
#include <stdint.h>

#define PKT_PLAYER_MASK_APPEARANCE 0x01
#define PKT_PLAYER_MASK_SEQUENCE 0x02
#define PKT_PLAYER_MASK_FACE_ENTITY 0x04
#define PKT_PLAYER_MASK_SAY 0x08
#define PKT_PLAYER_MASK_DAMAGE 0x10
#define PKT_PLAYER_MASK_FACE_COORD 0x20
#define PKT_PLAYER_MASK_CHAT 0x40
#define PKT_PLAYER_MASK_BIG_UPDATE 0x80
#define PKT_PLAYER_MASK_SPOTANIM 0x100
#define PKT_PLAYER_MASK_EXACT_MOVE 0x200
#define PKT_PLAYER_MASK_DAMAGE2 0x400

enum PktPlayerInfoOpKind
{
    PKT_PLAYER_INFO_OP_NONE = 0,
    PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER,
    /* The server tracks the list of players it has sent; later packets index
     * that list (0..2047 slots) instead of resending entity ids. */
    PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID,
    PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX,
    /* Selects the target for the extended-info block that follows, BY POSITION
     * in the list rebuilt this packet -- not by server slot. The consumer's
     * list must have one entry per entry the decoder counted, or this resolves
     * to the wrong entity and does so silently. See the invariant note on
     * RS_EntitySync::active_players/active_npcs. */
    PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX,
    PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX,
    PKT_PLAYER_INFO_OPBITS_COUNT_RESET,
    PKT_PLAYER_INFO_OPBITS_INFO,
    PKT_PLAYER_INFO_OPBITS_WALKDIR,
    PKT_PLAYER_INFO_OPBITS_RUNDIR,
    PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL,
    /*
     * An ABSOLUTE coordinate, where LOCAL_XZLEVEL carries a scene-local one.
     *
     * The v5 stream states every high-resolution position in world coordinates
     * and the classic stream states the local player's in 7-bit scene tiles.
     * They share `_local_xz_level`; what differs is the frame, and the decoder
     * cannot convert because it does not know the scene origin. The executor
     * subtracts it.
     */
    PKT_PLAYER_INFO_OP_ABS_XZLEVEL,
    /*
     * Drop a player by INDEX, not by a position in the previous packet's list.
     *
     * v5 addresses players by their slot in the client's own 1..2047 table, so
     * a de-resolution names the slot directly. CLEAR_PLAYER_OPBITS_IDX cannot
     * express that -- its payload indexes the old tracked list, which v5 does
     * not have.
     */
    PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID,
    PKT_PLAYER_INFO_OP_DELTA_XZ,
    PKT_PLAYER_INFO_OP_APPEARANCE,
    PKT_PLAYER_INFO_OP_SEQUENCE,
    PKT_PLAYER_INFO_OP_FACE_ENTITY,
    PKT_PLAYER_INFO_OP_SAY,
    PKT_PLAYER_INFO_OP_DAMAGE,
    PKT_PLAYER_INFO_OP_DAMAGE2,
    PKT_PLAYER_INFO_OP_HEADBAR,
    PKT_PLAYER_INFO_OP_FACE_COORD,
    PKT_PLAYER_INFO_OP_CHAT,
    PKT_PLAYER_INFO_OP_SPOTANIM,
    PKT_PLAYER_INFO_OP_EXACT_MOVE,
    PKT_PLAYER_INFO_OP_FACE_ANGLE
};

struct PktPlayerInfo_LocalXZLevel
{
    int16_t x;
    int16_t z;
    uint8_t level;
    bool jump;
    /* Revision 239 separates the coordinate opcode from its traversal speed.
     * Classic readers leave this false; the v5 reader fills `move_speed` from
     * the player's persistent speed and any one-cycle override. */
    bool has_move_speed;
    int8_t move_speed;
};

enum PktPlayerTraversal
{
    PKT_PLAYER_TRAVERSAL_CRAWL = 0,
    PKT_PLAYER_TRAVERSAL_WALK = 1,
    PKT_PLAYER_TRAVERSAL_RUN = 2,
    PKT_PLAYER_TRAVERSAL_SNAP = 127,
};

struct PktPlayerInfo_DeltaXZ
{
    int16_t dx;
    int16_t dz;
    bool jump;
};

struct PktPlayerInfo_Appearance
{
    uint8_t* appearance; /* heap; freed by pkt_player_info_ops_free */
    int len;
};

struct PktPlayerInfo_Sequence
{
    int sequence_id; /* -1 = clear */
    uint8_t delay;
};

struct PktPlayerInfo_FaceEntity
{
    int32_t entity_id;
    uint16_t fallback_angle;
    bool has_fallback_angle;
    bool instant;
    uint8_t movement_mode;
    bool modern;
};

struct PktPlayerInfo_Say
{
    char* text; /* heap */
};

struct PktPlayerInfo_Damage
{
    int damage_type;
    int damage;
    uint8_t health;
    uint8_t total_health;
    uint16_t delay;
    uint16_t slots;
};

/* Actor.method3504's six wire values, kept separate from a hitsplat: a bar
 * can update with no damage at all. `duration == 32767` removes this type. */
struct PktPlayerInfo_Headbar
{
    int type;
    int duration;
    int start_delay;
    uint8_t start_fill;
    uint8_t end_fill;
    bool remove;
};

struct PktPlayerInfo_FaceCoord
{
    int16_t x;
    int16_t z;
    bool instant;
    uint8_t movement_mode;
    bool modern;
};

struct PktPlayerInfo_Chat
{
    uint16_t colour_effect;
    uint8_t type; /* staff-mod level */
    uint8_t length;
    uint8_t* data; /* heap: wordpacked payload, `length` bytes */
};

struct PktPlayerInfo_SpotAnim
{
    uint8_t slot;
    int32_t spotanim_id; /* 65535 -> -1 */
    int32_t height_delay; /* height = >>16, cycle delay = &0xffff */
};

struct PktPlayerInfo_ExactMove
{
    int16_t start_x;
    int16_t start_z;
    int16_t end_x;
    int16_t end_z;
    uint16_t end_cycle_delta;   /* + client loop cycle at apply time */
    uint16_t start_cycle_delta; /* + client loop cycle at apply time */
    uint16_t facing;            /* protocol yaw */
    bool facing_is_yaw;
    bool relative;              /* deltas from route[0] in the v5 stream */
};

struct PktPlayerInfo_FaceAngle
{
    uint16_t angle;
    bool instant;
    uint8_t movement_mode;
    bool modern;
};

struct PktPlayerInfoOp
{
    enum PktPlayerInfoOpKind kind;

    union
    {
        uint64_t _bitvalue;
        struct PktPlayerInfo_Appearance _appearance;
        struct PktPlayerInfo_Sequence _sequence;
        struct PktPlayerInfo_FaceEntity _face_entity;
        struct PktPlayerInfo_Say _say;
        struct PktPlayerInfo_Damage _damage;
        struct PktPlayerInfo_Headbar _headbar;
        struct PktPlayerInfo_FaceCoord _face_coord;
        struct PktPlayerInfo_Chat _chat;
        struct PktPlayerInfo_SpotAnim _spotanim;
        struct PktPlayerInfo_ExactMove _exactmove;
        struct PktPlayerInfo_FaceAngle _face_angle;
        struct PktPlayerInfo_LocalXZLevel _local_xz_level;
        struct PktPlayerInfo_DeltaXZ _delta_xz;
    };
};

struct PktPlayerInfoReader
{
    uint16_t extended_queue[2048];
    int extended_count;
    int current_op;
};

/** Decode the raw command stream into `ops`. Returns the op count. */
int
pkt_player_info_reader_read(
    struct PktPlayerInfoReader* reader,
    uint8_t const* data,
    int length,
    struct PktPlayerInfoOp* ops,
    int ops_capacity);

/** Free heap payloads (appearance blobs, say strings, chat data). */
void
pkt_player_info_ops_free(
    struct PktPlayerInfoOp* ops,
    int op_count);

#endif
