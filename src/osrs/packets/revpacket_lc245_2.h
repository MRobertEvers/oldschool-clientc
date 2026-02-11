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
    // These are 1-indexed, e.g. 841 is shortbow. Over the
    // network, it's sent as 842.
    int* obj_ids;
    int* obj_counts;
};

struct PktIfSetTab
{
    int component_id;
    int tab_id;
};

struct PktIfSetTabActive
{
    int tab_id;
};

struct PktVarpSmall
{
    int variable;
    int value; /* g1b signed byte */
};

struct PktVarpLarge
{
    int variable;
    int value; /* g4 */
};

struct PktUpdateStat
{
    int stat;   /* g1: 0-22 */
    int xp;     /* g4 */
    int level;  /* g1: effective level */
};

struct PktUpdateRunEnergy
{
    int run_energy; /* g1: 0-100 */
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
        struct PktIfSetTab _if_settab;
        struct PktIfSetTabActive _if_settab_active;
        struct PktVarpSmall _varp_small;
        struct PktVarpLarge _varp_large;
        struct PktUpdateStat _update_stat;
        struct PktUpdateRunEnergy _update_run_energy;
    };
};

#endif