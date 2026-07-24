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

enum PktNpcInfoOpKind
{
    PKT_NPC_INFO_OP_NONE = 0,
    PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID, /* 13-bit server npc slot */
    PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX,
    PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OPBITS_COUNT_RESET,
    PKT_NPC_INFO_OPBITS_INFO,
    PKT_NPC_INFO_OPBITS_WALKDIR,
    PKT_NPC_INFO_OPBITS_RUNDIR,
    PKT_NPC_INFO_OPBITS_NPCTYPE, /* 11-bit npc config id */
    PKT_NPC_INFO_OP_DELTA_XZ,
    PKT_NPC_INFO_OP_SEQUENCE,
    PKT_NPC_INFO_OP_FACE_ENTITY,
    PKT_NPC_INFO_OP_SAY,
    PKT_NPC_INFO_OP_DAMAGE,
    PKT_NPC_INFO_OP_FACE_COORD,
    PKT_NPC_INFO_OP_CHANGE_TYPE,
    PKT_NPC_INFO_OP_SPOTANIM
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
};

struct PktNpcInfo_Say
{
    char* text; /* heap */
};

struct PktNpcInfo_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktNpcInfo_FaceCoord
{
    int16_t x;
    int16_t z;
};

struct PktNpcInfo_ChangeType
{
    int npc_type;
};

struct PktNpcInfo_SpotAnim
{
    int32_t spotanim_id; /* 65535 -> -1 */
    int32_t height_delay;
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
    };
};

struct PktNpcInfoReader
{
    uint16_t extended_queue[2048];
    int extended_count;
    int current_op;
};

/** Decode the raw command stream into `ops`. Returns the op count. */
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
