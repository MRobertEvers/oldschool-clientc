#ifndef OSRS_CORE_SERVERPROT_PKT_NPCINFO_H
#define OSRS_CORE_SERVERPROT_PKT_NPCINFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktServerProt_NpcInfo_v1_OpKind
{
    PKT_SERVER_PROT_NPC_INFO_V1_OP_NONE = 0,
    // This is the offset in the active player map.
    // The active player maps [...2047] to [0...8192]
    //                           ^idx         ^entity_id
    // Where the index in the second array is the entity id.
    // Specify the index in the active player map
    // the active player map, maps to the entry in the entity array.
    // The server keeps track of the list of players that they sent to
    // the client. In future packets, it doesn't send the entity id,
    // only the entry in the list.
    PKT_SERVER_PROT_NPC_INFO_V1_OP_ADD_NPC_NEW_OPBITS_PID,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_ADD_NPC_OLD_OPBITS_IDX,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_SET_NPC_OPBITS_IDX,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_CLEAR_NPC_OPBITS_IDX,
    PKT_SERVER_PROT_NPC_INFO_V1_OPBITS_COUNT_RESET,
    PKT_SERVER_PROT_NPC_INFO_V1_OPBITS_INFO,
    PKT_SERVER_PROT_NPC_INFO_V1_OPBITS_NPCTYPE,
    PKT_SERVER_PROT_NPC_INFO_V1_OPBITS_WALKDIR,
    PKT_SERVER_PROT_NPC_INFO_V1_OPBITS_RUNDIR,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_DELTA_XZ,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_SEQUENCE,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_FACE_ENTITY,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_SAY,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_DAMAGE,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_DAMAGE2,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_FACE_COORD,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_CHAT,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_CHANGE_NPC_TYPE,
    PKT_SERVER_PROT_NPC_INFO_V1_OP_SPOTANIM

};

struct PktServerProt_NpcInfo_v1_DeltaXZ
{
    int16_t x;
    int16_t z;
    bool jump;
};

struct PktServerProt_NpcInfo_v1_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktServerProt_NpcInfo_v1_FaceEntity
{
    int32_t entity_id;
};

struct PktServerProt_NpcInfo_v1_FaceCoord
{
    int16_t x;
    int16_t z;
};

struct PktServerProt_NpcInfo_v1_Say
{
    char* text;
};

struct PktServerProt_NpcInfo_v1_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktServerProt_NpcInfo_v1_Damage2
{
    int32_t entity_id;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktServerProt_NpcInfo_v1_SpotAnim
{
    int32_t spotanim_id;
    /** Raw g4: height in high 16 bits, cycle delay in low 16 (see Client.ts getNpcPosExtended). */
    int32_t delay;
};

struct PktServerProt_NpcInfo_v1_Rundir
{
    int32_t rundir_one;
    int32_t rundir_two;
};

struct PktServerProt_NpcInfo_v1_Op
{
    enum PktServerProt_NpcInfo_v1_OpKind kind;

    union
    {
        uint64_t _bitvalue;
        struct PktServerProt_NpcInfo_v1_Sequence _sequence;
        struct PktServerProt_NpcInfo_v1_FaceEntity _face_entity;
        struct PktServerProt_NpcInfo_v1_FaceCoord _face_coord;
        struct PktServerProt_NpcInfo_v1_Damage _damage;
        struct PktServerProt_NpcInfo_v1_Damage2 _damage2;
        struct PktServerProt_NpcInfo_v1_Rundir _rundir;
        struct PktServerProt_NpcInfo_v1_DeltaXZ _delta_xz;
        struct PktServerProt_NpcInfo_v1_SpotAnim _spotanim;
    };
};

struct PktServerProt_NpcInfo_v1
{
    int length;
    uint8_t* data;
};

struct PktServerProt_NpcInfoReader_v1
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
    struct PktServerProt_NpcInfoReader_v1* reader,
    struct PktServerProt_NpcInfo_v1* pkt,
    struct PktServerProt_NpcInfo_v1_Op* ops,
    int ops_capacity);

#endif /* OSRS_CORE_SERVERPROT_PKT_NPCINFO_H */
