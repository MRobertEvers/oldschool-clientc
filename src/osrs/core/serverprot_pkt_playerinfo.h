#ifndef OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H
#define OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktPlayerInfoOpKindV1
{
    PKT_PLAYER_INFO_OP_NONE = 0,
    PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER,
    // This is the offset in the active player map.
    // The active player maps [...2047] to [0...8192]
    //                           ^idx         ^entity_id
    // Where the index in the second array is the entity id.
    // Specify the index in the active player map
    // the active player map, maps to the entry in the entity array.
    // The server keeps track of the list of players that they sent to
    // the client. In future packets, it doesn't send the entity id,
    // only the entry in the list.
    PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID,
    PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX,
    PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX,
    PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX,
    PKT_PLAYER_INFO_OPBITS_COUNT_RESET,
    PKT_PLAYER_INFO_OPBITS_INFO,
    PKT_PLAYER_INFO_OPBITS_WALKDIR,
    PKT_PLAYER_INFO_OPBITS_RUNDIR,
    PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL,
    PKT_PLAYER_INFO_OP_DELTA_XZ,
    PKT_PLAYER_INFO_OP_APPEARANCE,
    PKT_PLAYER_INFO_OP_SEQUENCE,
    PKT_PLAYER_INFO_OP_FACE_ENTITY,
    PKT_PLAYER_INFO_OP_SAY,
    PKT_PLAYER_INFO_OP_DAMAGE,
    PKT_PLAYER_INFO_OP_DAMAGE2,
    PKT_PLAYER_INFO_OP_FACE_COORD,
    PKT_PLAYER_INFO_OP_CHAT,
    PKT_PLAYER_INFO_OP_SPOTANIM,
    PKT_PLAYER_INFO_OP_EXACT_MOVE

};

struct PktPlayerInfoV1_LocalXZLevel
{
    int16_t x;
    int16_t z;
    uint8_t level;
    bool jump;
};

struct PktPlayerInfoV1_DeltaXZ
{
    int16_t dx;
    int16_t dz;
    bool jump;
};

struct PktPlayerInfoV1_Appearance
{
    uint8_t* appearance;
    int len;
};

struct PktPlayerInfoV1_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktPlayerInfoV1_FaceEntity
{
    int32_t entity_id;
};

struct PktPlayerInfoV1_Say
{
    char* text;
};

struct PktPlayerInfoV1_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktPlayerInfoV1_FaceCoord
{
    int32_t entity_id;
    int16_t x;
    int16_t z;
};

struct PktPlayerInfoV1_Chat
{
    char username[13];
    uint16_t color;
    uint8_t type;
    uint8_t length;
    uint8_t* text; // Wordpack Unpack
};

struct PktPlayerInfoV1_SpotAnim
{
    int32_t spotanim_id;
    int32_t delay;
};

struct PktPlayerInfoV1_ExactMove
{
    uint8_t forcemove_start_x;
    uint8_t forcemove_start_z;
    uint8_t forcemove_end_x;
    uint8_t forcemove_end_z;
    uint16_t forcemove_startcycle;
    uint16_t forcemove_endcycle;
    uint8_t forcemove_facedirection;
};

struct PktPlayerInfoV1_Damage2
{
    uint8_t damage;
    uint8_t damage_type;
    uint8_t health;
    uint8_t total_health;
};

struct PktPlayerInfoOpV1
{
    enum PktPlayerInfoOpKindV1 kind;

    union
    {
        uint64_t _bitvalue;
        struct PktPlayerInfoV1_Appearance _appearance;
        struct PktPlayerInfoV1_Sequence _sequence;
        struct PktPlayerInfoV1_FaceEntity _face_entity;
        struct PktPlayerInfoV1_Say _say;
        struct PktPlayerInfoV1_Damage _damage;
        struct PktPlayerInfoV1_FaceCoord _face_coord;
        struct PktPlayerInfoV1_Chat _chat;
        struct PktPlayerInfoV1_SpotAnim _spotanim;
        struct PktPlayerInfoV1_ExactMove _exactmove;
        struct PktPlayerInfoV1_Damage2 _damage2;
        struct PktPlayerInfoV1_LocalXZLevel _local_xz_level;
        struct PktPlayerInfoV1_DeltaXZ _delta_xz;
    };
};

struct PktPlayerInfoV1
{
    int length;
    uint8_t* data;
};

struct PktPlayerInfoReaderV1
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
    struct PktPlayerInfoReaderV1* reader,
    struct PktPlayerInfoV1* pkt,
    struct PktPlayerInfoOpV1* ops,
    int ops_capacity);

#endif /* OSRS_CORE_SERVERPROT_PKT_PLAYERINFO_H */
