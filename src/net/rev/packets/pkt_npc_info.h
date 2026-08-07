#ifndef SRC_NET_REV_PKT_NPC_INFO_H
#define SRC_NET_REV_PKT_NPC_INFO_H

/*
 * NPC_INFO command-stream decoder (port of v0 pkt_npc_info with the gaps
 * filled from Client-TS: CHANGE_TYPE and SPOTANIM decoded, SAY carried in
 * the op instead of printf'd). Pure CPU; applying happens in the packet task.
 *
 * Extended-info masks (Client-TS NpcUpdate / server rsbuf NpcInfoProt).
 */

#include <stdbool.h>
#include <stdint.h>

#define PKT_NPC_MASK_DAMAGE2 0x1
#define PKT_NPC_MASK_ANIM 0x2
#define PKT_NPC_MASK_FACE_ENTITY 0x4
#define PKT_NPC_MASK_SAY 0x8
#define PKT_NPC_MASK_DAMAGE 0x10
#define PKT_NPC_MASK_CHANGE_TYPE 0x20
#define PKT_NPC_MASK_SPOTANIM 0x40
#define PKT_NPC_MASK_FACE_COORD 0x80

/*
 * Wire widths of the new-npc record's slot and type fields.
 *
 * These are revision-dependent — the stream keeps its shape while individual
 * fields widen with the game's id space — so a reader is initialised with the
 * widths its revision states rather than reading a constant. A too-narrow
 * width truncates silently into a different, usually valid, id.
 */
#define PKT_NPC_INFO_SLOT_BITS_CLASSIC 14
#define PKT_NPC_INFO_TYPE_BITS_CLASSIC 11
#define PKT_NPC_INFO_BITS_MAX 16

enum PktNpcInfoOpKind
{
    PKT_NPC_INFO_OP_NONE = 0,
    /* Server npc slot; width is the reader's `slot_bits`. */
    PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID,
    PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX,
    PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OPBITS_COUNT_RESET,
    PKT_NPC_INFO_OPBITS_INFO,
    PKT_NPC_INFO_OPBITS_WALKDIR,
    PKT_NPC_INFO_OPBITS_RUNDIR,
    /* Npc config id; width is the reader's `type_bits`. */
    PKT_NPC_INFO_OPBITS_NPCTYPE,
    PKT_NPC_INFO_OP_DELTA_XZ,
    PKT_NPC_INFO_OP_SEQUENCE,
    PKT_NPC_INFO_OP_FACE_ENTITY,
    PKT_NPC_INFO_OP_SAY,
    PKT_NPC_INFO_OP_DAMAGE,
    PKT_NPC_INFO_OP_FACE_COORD,
    PKT_NPC_INFO_OP_CHANGE_TYPE,
    PKT_NPC_INFO_OP_SPOTANIM,
    PKT_NPC_INFO_OP_EXACT_MOVE,
    PKT_NPC_INFO_OP_FACE_ANGLE,
    PKT_NPC_INFO_OP_SPAWN_CYCLE,
    PKT_NPC_INFO_OP_VISIBLE_OPS,
    PKT_NPC_INFO_OP_NAME_CHANGE,
    PKT_NPC_INFO_OP_LEVEL_CHANGE,
    PKT_NPC_INFO_OP_BAS_CHANGE
};

struct PktNpcInfo_DeltaXZ
{
    int16_t dx;
    int16_t dz;
    bool jump;
};

struct PktNpcInfo_Sequence
{
    int sequence_id; /* -1 = clear */
    uint8_t delay;
};

struct PktNpcInfo_FaceEntity
{
    int32_t entity_id;
    uint16_t fallback_angle;
    bool has_fallback_angle;
    bool instant;
};

struct PktNpcInfo_Say
{
    char* text; /* heap */
};

struct PktNpcInfo_Damage
{
    int damage_type;
    int damage;
    uint8_t health;
    uint8_t total_health;
    uint16_t delay;
    uint16_t duration;
};

struct PktNpcInfo_FaceCoord
{
    int16_t x;
    int16_t z;
    bool instant;
};

struct PktNpcInfo_ChangeType
{
    int npc_type;
};

struct PktNpcInfo_SpotAnim
{
    uint8_t slot;
    int32_t spotanim_id; /* 65535 -> -1 */
    int32_t height_delay;
};

struct PktNpcInfo_ExactMove
{
    int16_t start_x;
    int16_t start_z;
    int16_t end_x;
    int16_t end_z;
    uint16_t start_cycle_delta;
    uint16_t end_cycle_delta;
    uint16_t facing;
    bool relative;
};

struct PktNpcInfo_FaceAngle
{
    uint16_t angle;
    bool instant;
};

struct PktNpcInfo_NameChange
{
    char* name; /* heap */
};

struct PktNpcInfo_BasChange
{
    uint32_t mask;
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
};

struct PktNpcInfoOp
{
    enum PktNpcInfoOpKind kind;

    union
    {
        uint64_t _bitvalue;
        struct PktNpcInfo_DeltaXZ _delta_xz;
        struct PktNpcInfo_Sequence _sequence;
        struct PktNpcInfo_FaceEntity _face_entity;
        struct PktNpcInfo_Say _say;
        struct PktNpcInfo_Damage _damage;
        struct PktNpcInfo_FaceCoord _face_coord;
        struct PktNpcInfo_ChangeType _change_type;
        struct PktNpcInfo_SpotAnim _spotanim;
        struct PktNpcInfo_ExactMove _exactmove;
        struct PktNpcInfo_FaceAngle _face_angle;
        struct PktNpcInfo_NameChange _name_change;
        struct PktNpcInfo_BasChange _bas_change;
    };
};

struct PktNpcInfoReader
{
    uint16_t extended_queue[2048];
    int extended_count;
    int current_op;
    /** Set by pkt_npc_info_reader_init from the revision table. Never read
     *  these from a zeroed reader — pkt_npc_info_reader_read asserts they are
     *  in range precisely so a forgotten init is loud rather than a bit width
     *  of 0. */
    int slot_bits;
    int type_bits;
};

/** Arm a reader for one revision's wire widths. Zeroes the reader; either
 *  width may be 0 to take the classic default. */
void
pkt_npc_info_reader_init(
    struct PktNpcInfoReader* reader,
    int slot_bits,
    int type_bits);

/** Decode the raw command stream into `ops`. Returns the op count.
 *  The reader must have been through pkt_npc_info_reader_init. */
int
pkt_npc_info_reader_read(
    struct PktNpcInfoReader* reader,
    uint8_t const* data,
    int length,
    struct PktNpcInfoOp* ops,
    int ops_capacity);

/** Free heap payloads (say strings). */
void
pkt_npc_info_ops_free(
    struct PktNpcInfoOp* ops,
    int op_count);

#endif
