#ifndef PKT_PLAYER_INFO_H
#define PKT_PLAYER_INFO_H

#include <stdbool.h>
#include <stdint.h>

enum PktPlayerInfoOpKind
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
    PKT_PLAYER_INFO_OPBITS_LEVEL,
    PKT_PLAYER_INFO_OPBITS_LOCAL_X,
    PKT_PLAYER_INFO_OPBITS_LOCAL_Z,
    PKT_PLAYER_INFO_OPBITS_JUMP,
    PKT_PLAYER_INFO_OPBITS_DX,
    PKT_PLAYER_INFO_OPBITS_DZ,
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

struct PktPlayerInfo_Appearance
{
    uint8_t* appearance;
    int len;
};

struct PktPlayerInfo_Sequence
{
    uint16_t sequence_id;
    uint8_t delay;
};

struct PktPlayerInfo_FaceEntity
{
    int32_t entity_id;
};

struct PktPlayerInfo_Say
{
    char* text;
};

struct PktPlayerInfo_Damage
{
    uint8_t damage_type;
    uint8_t damage;
    uint8_t health;
    uint8_t total_health;
};

struct PktPlayerInfo_FaceCoord
{
    int32_t entity_id;
    int16_t x;
    int16_t z;
};

struct PktPlayerInfo_Chat
{
    char username[13];
    uint16_t color;
    uint8_t type;
    uint8_t length;
    uint8_t* text; // Wordpack Unpack
};

struct PktPlayerInfo_SpotAnim
{
    int32_t spotanim_id;
    int32_t delay;
};

struct PktPlayerInfo_ExactMove
{
    uint8_t forcemove_start_x;
    uint8_t forcemove_start_z;
    uint8_t forcemove_end_x;
    uint8_t forcemove_end_z;
    uint16_t forcemove_startcycle;
    uint16_t forcemove_endcycle;
    uint8_t forcemove_facedirection;
};

struct PktPlayerInfo_Damage2
{
    uint8_t damage;
    uint8_t damage_type;
    uint8_t health;
    uint8_t total_health;
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
        struct PktPlayerInfo_FaceCoord _face_coord;
        struct PktPlayerInfo_Chat _chat;
        struct PktPlayerInfo_SpotAnim _spotanim;
        struct PktPlayerInfo_ExactMove _exactmove;
        struct PktPlayerInfo_Damage2 _damage2;
    };
};

struct PktPlayerInfo
{
    int length;
    uint8_t* data;
};

struct PktPlayerInfoReader
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
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfo* pkt,
    struct PktPlayerInfoOp* ops,
    int ops_capacity);

#endif