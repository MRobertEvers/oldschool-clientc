#ifndef PKT_NPC_INFO_H
#define PKT_NPC_INFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktNpcInfoOpKind
{
    PKT_NPC_INFO_OP_NONE = 0,
    // This is the offset in the active player map.
    // The active player maps [...2047] to [0...8192]
    //                           ^idx         ^entity_id
    // Where the index in the second array is the entity id.
    // Specify the index in the active player map
    // the active player map, maps to the entry in the entity array.
    // The server keeps track of the list of players that they sent to
    // the client. In future packets, it doesn't send the entity id,
    // only the entry in the list.
    PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID,
    PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX,
    PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX,
    PKT_NPC_INFO_OPBITS_COUNT_RESET,
    PKT_NPC_INFO_OPBITS_INFO,
    PKT_NPC_INFO_OPBITS_NPCTYPE,
    PKT_NPC_INFO_OPBITS_WALKDIR,
    PKT_NPC_INFO_OPBITS_RUNDIR,
    PKT_NPC_INFO_OPBITS_JUMP,
    PKT_NPC_INFO_OPBITS_DX,
    PKT_NPC_INFO_OPBITS_DZ,
    PKT_NPC_INFO_OP_SEQUENCE,
    PKT_NPC_INFO_OP_FACE_ENTITY,
    PKT_NPC_INFO_OP_SAY,
    PKT_NPC_INFO_OP_DAMAGE,
    PKT_NPC_INFO_OP_DAMAGE2,
    PKT_NPC_INFO_OP_FACE_COORD,
    PKT_NPC_INFO_OP_CHAT,
    PKT_NPC_INFO_OP_SPOTANIM

};

struct PktNpcInfo_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktNpcInfo_FaceEntity
{
    int32_t entity_id;
};

struct PktNpcInfo_Say
{
    char* text;
};

struct PktNpcInfo_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktNpcInfo_Damage2
{
    int32_t entity_id;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktNpcInfoOp
{
    enum PktNpcInfoOpKind kind;

    union
    {
        uint64_t _bitvalue;
        struct PktNpcInfo_Sequence _sequence;
        struct PktNpcInfo_Damage2 _damage2;
    };
};

struct PktNpcInfo
{
    int length;
    uint8_t* data;
};

struct PktNpcInfoReader
{
    uint16_t extended_queue[2048];
    int extended_count;

    // Maps [...2047] to [0...8192]
    // Where the index in the second array is the entity id.

    int current_op;
    int max_ops;
};

int
pkt_npc_info_reader_read(
    struct PktNpcInfoReader* reader,
    struct PktNpcInfo* pkt,
    struct PktNpcInfoOp* ops,
    int ops_capacity);

#endif