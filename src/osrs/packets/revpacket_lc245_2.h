#ifndef PKT_LC245_2_H
#define PKT_LC245_2_H

#include "osrs/packetin.h"
#include "pkt_map_rebuild.h"

// Player info packet is more of a command stream
// than a fixed format packet
struct PktNpcInfoLC245_2
{
    int length;
    uint8_t* data;
};

struct PktPlayerInfoLC245_2
{
    int length;
    uint8_t* data;
};

struct PktUpdateInvFull
{
    int component_id;
    int size;
    int* obj_ids;
    int* obj_counts;
};

struct RevPacket_LC245_2
{
    enum PacketInType_LC245_2 packet_type;

    union
    {
        struct PktMapRebuild _map_rebuild;
        struct PktNpcInfoLC245_2 _npc_info;
        struct PktPlayerInfoLC245_2 _player_info;
        struct PktUpdateInvFull _update_inv_full;
    };
};

#endif