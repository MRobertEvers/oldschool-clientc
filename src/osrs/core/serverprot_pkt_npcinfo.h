#ifndef OSRS_CORE_SERVERPROT_PKT_NPCINFO_H
#define OSRS_CORE_SERVERPROT_PKT_NPCINFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktNpcInfoOpKindV1
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
    PKT_NPC_INFO_OP_DELTA_XZ,
    PKT_NPC_INFO_OP_SEQUENCE,
    PKT_NPC_INFO_OP_FACE_ENTITY,
    PKT_NPC_INFO_OP_SAY,
    PKT_NPC_INFO_OP_DAMAGE,
    PKT_NPC_INFO_OP_DAMAGE2,
    PKT_NPC_INFO_OP_FACE_COORD,
    PKT_NPC_INFO_OP_CHAT,
    PKT_NPC_INFO_OP_SPOTANIM

};

struct PktNpcInfoV1_DeltaXZ
{
    int16_t x;
    int16_t z;
    bool jump;
};

struct PktNpcInfoV1_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktNpcInfoV1_FaceEntity
{
    int32_t entity_id;
};

struct PktNpcInfoV1_FaceCoord
{
    int16_t x;
    int16_t z;
};

struct PktNpcInfoV1_Say
{
    char* text;
};

struct PktNpcInfoV1_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktNpcInfoV1_Damage2
{
    int32_t entity_id;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktNpcInfoV1_Rundir
{
    int32_t rundir_one;
    int32_t rundir_two;
};

struct PktNpcInfoOpV1
{
    enum PktNpcInfoOpKindV1 kind;

    union
    {
        uint64_t _bitvalue;
        struct PktNpcInfoV1_Sequence _sequence;
        struct PktNpcInfoV1_FaceEntity _face_entity;
        struct PktNpcInfoV1_FaceCoord _face_coord;
        struct PktNpcInfoV1_Damage _damage;
        struct PktNpcInfoV1_Damage2 _damage2;
        struct PktNpcInfoV1_Rundir _rundir;
        struct PktNpcInfoV1_DeltaXZ _delta_xz;
    };
};

struct PktNpcInfoV1
{
    int length;
    uint8_t* data;
};

struct PktNpcInfoReaderV1
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
    struct PktNpcInfoReaderV1* reader,
    struct PktNpcInfoV1* pkt,
    struct PktNpcInfoOpV1* ops,
    int ops_capacity);

#endif /* OSRS_CORE_SERVERPROT_PKT_NPCINFO_H */
