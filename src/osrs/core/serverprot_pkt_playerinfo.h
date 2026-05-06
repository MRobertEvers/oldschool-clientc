#ifndef OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H
#define OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktServerProt_PlayerInfo_v1_OpKind
{
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_NONE = 0,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SET_LOCAL_PLAYER,
    // This is the offset in the active player map.
    // The active player maps [...2047] to [0...8192]
    //                           ^idx         ^entity_id
    // Where the index in the second array is the entity id.
    // Specify the index in the active player map
    // the active player map, maps to the entry in the entity array.
    // The server keeps track of the list of players that they sent to
    // the client. In future packets, it doesn't send the entity id,
    // only the entry in the list.
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_ADD_PLAYER_NEW_OPBITS_PID,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_ADD_PLAYER_OLD_OPBITS_IDX,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SET_PLAYER_OPBITS_IDX,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_CLEAR_PLAYER_OPBITS_IDX,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_COUNT_RESET,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_INFO,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_WALKDIR,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_RUNDIR,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_LOCAL_XZLEVEL,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DELTA_XZ,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_APPEARANCE,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SEQUENCE,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_FACE_ENTITY,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SAY,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DAMAGE,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DAMAGE2,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_FACE_COORD,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_CHAT,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SPOTANIM,
    PKT_SERVER_PROT_PLAYER_INFO_V1_OP_EXACT_MOVE

};

struct PktServerProt_PlayerInfo_v1_LocalXZLevel
{
    int16_t x;
    int16_t z;
    uint8_t level;
    bool jump;
};

struct PktServerProt_PlayerInfo_v1_DeltaXZ
{
    int16_t dx;
    int16_t dz;
    bool jump;
};

struct PktServerProt_PlayerInfo_v1_Appearance
{
    uint8_t* appearance;
    int len;
};

struct PktServerProt_PlayerInfo_v1_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktServerProt_PlayerInfo_v1_FaceEntity
{
    int32_t entity_id;
};

struct PktServerProt_PlayerInfo_v1_Say
{
    char* text;
};

struct PktServerProt_PlayerInfo_v1_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktServerProt_PlayerInfo_v1_FaceCoord
{
    int32_t entity_id;
    int16_t x;
    int16_t z;
};

struct PktServerProt_PlayerInfo_v1_Chat
{
    char username[13];
    uint16_t color;
    uint8_t type;
    uint8_t length;
    uint8_t* text; // Wordpack Unpack
};

struct PktServerProt_PlayerInfo_v1_SpotAnim
{
    int32_t spotanim_id;
    int32_t delay;
};

struct PktServerProt_PlayerInfo_v1_ExactMove
{
    uint8_t forcemove_start_x;
    uint8_t forcemove_start_z;
    uint8_t forcemove_end_x;
    uint8_t forcemove_end_z;
    uint16_t forcemove_startcycle;
    uint16_t forcemove_endcycle;
    uint8_t forcemove_facedirection;
};

struct PktServerProt_PlayerInfo_v1_Damage2
{
    uint8_t damage;
    uint8_t damage_type;
    uint8_t health;
    uint8_t total_health;
};

struct PktServerProt_PlayerInfo_v1_Op
{
    enum PktServerProt_PlayerInfo_v1_OpKind kind;

    union
    {
        uint64_t _bitvalue;
        struct PktServerProt_PlayerInfo_v1_Appearance _appearance;
        struct PktServerProt_PlayerInfo_v1_Sequence _sequence;
        struct PktServerProt_PlayerInfo_v1_FaceEntity _face_entity;
        struct PktServerProt_PlayerInfo_v1_Say _say;
        struct PktServerProt_PlayerInfo_v1_Damage _damage;
        struct PktServerProt_PlayerInfo_v1_FaceCoord _face_coord;
        struct PktServerProt_PlayerInfo_v1_Chat _chat;
        struct PktServerProt_PlayerInfo_v1_SpotAnim _spotanim;
        struct PktServerProt_PlayerInfo_v1_ExactMove _exactmove;
        struct PktServerProt_PlayerInfo_v1_Damage2 _damage2;
        struct PktServerProt_PlayerInfo_v1_LocalXZLevel _local_xz_level;
        struct PktServerProt_PlayerInfo_v1_DeltaXZ _delta_xz;
    };
};

struct PktServerProt_PlayerInfo_v1
{
    int length;
    uint8_t* data;
};

struct PktServerProt_PlayerInfoReader_v1
{
    uint16_t extended_queue[2048];
    int extended_count;

    // Maps [...2047] to [0...8192]
    // Where the index in the second array is the entity id.

    int current_op;
    int max_ops;
};

int
pkt_player_info_reader_read(
    struct PktServerProt_PlayerInfoReader_v1* reader,
    struct PktServerProt_PlayerInfo_v1* pkt,
    struct PktServerProt_PlayerInfo_v1_Op* ops,
    int ops_capacity);

#endif /* OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H */
