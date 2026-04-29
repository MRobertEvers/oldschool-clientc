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

struct PktIfOpenChat
{
    int component_id; /* g2: chat interface component to show */
};

struct PktIfClose
{
    /* No payload - closes sidebar, chat, viewport */
    uint8_t _torirs_empty; /* C requires a member; keep 1-byte layout */
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

struct PktIfSetColour
{
    int component_id; /* g2 */
    int colour;       /* g2: 15-bit (r<<10|g<<5|b) */
};

struct PktIfSetHide
{
    int component_id; /* g2 */
    int hide;        /* g1: 1=hide */
};

struct PktIfSetObject
{
    int component_id; /* g2 */
    int obj_id;      /* g2 */
    int zoom;        /* g2 */
};

struct PktIfSetModel
{
    int component_id; /* g2 */
    int model_id;     /* g2 */
};

struct PktIfSetAnim
{
    int component_id; /* g2 */
    int anim_id;      /* g2 */
};

struct PktIfSetPlayerHead
{
    int component_id; /* g2 */
};

struct PktIfSetText
{
    int component_id; /* g2 */
    char* text;       /* gjstr / newline-terminated */
};

struct PktIfSetNpcHead
{
    int component_id; /* g2 */
    int npc_id;       /* g2 */
};

struct PktIfSetPosition
{
    int component_id; /* g2 */
    int x;            /* g2b */
    int z;            /* g2b */
};

struct PktIfSetScrollPos
{
    int component_id; /* g2 */
    int pos;          /* g2 */
};

struct PktMessageGame
{
    char* text; /* gjstr / newline-terminated */
};

struct PktMessagePrivate
{
    int64_t from;       /* g8: base37 username */
    int message_id;    /* g4 */
    int staff_mod;     /* g1 */
    char* text;        /* WordPack.unpack(psize-13) */
};

struct PktChatFilterSettings
{
    int chat_public_mode;  /* g1 */
    int chat_private_mode; /* g1 */
    int chat_trade_mode;   /* g1 */
};

/* Zone packets: position = base_x + (pos>>4)&7, base_z + pos&7. Base set by UPDATE_ZONE_* */
struct PktUpdateZonePartialFollows
{
    int base_x; /* g1: zone base for subsequent zone packets */
    int base_z; /* g1 */
};

struct PktUpdateZoneFullFollows
{
    int base_x; /* g1 */
    int base_z; /* g1 */
};

struct PktLocAddChange
{
    int pos;   /* g1: x = base_x + (pos>>4)&7, z = base_z + pos&7 */
    int info;  /* g1: shape = info>>2, angle = info&3 */
    int loc_id; /* g2 */
};

struct PktLocDel
{
    int pos;  /* g1 */
    int info; /* g1: shape = info>>2, angle = info&3 */
};

struct PktLocAnim
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int seq_id; /* g2 */
};

struct PktObjAdd
{
    int pos;   /* g1 */
    int obj_id; /* g2 */
    int count;  /* g2 */
};

struct PktObjDel
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
};

struct PktObjReveal
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int count;     /* g2 */
    int receiver;  /* g2: player index, skip if != local */
};

struct PktObjCount
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int old_count; /* g2 */
    int new_count; /* g2 */
};

struct PktLocMerge
{
    int pos;   /* g1 */
    int info;  /* g1 */
    int loc_id; /* g2 */
    int start;  /* g2 */
    int end;    /* g2 */
    int pid;    /* g2 */
    int east;   /* g1b */
    int south;  /* g1b */
    int west;   /* g1b */
    int north;  /* g1b */
};

struct PktMapProjAnim
{
    int pos;         /* g1 */
    int dx_offset;   /* g1b: dx = x + dx_offset */
    int dz_offset;   /* g1b: dz = z + dz_offset */
    int target;      /* g2b */
    int spotanim;    /* g2 */
    int src_height;  /* g1 */
    int dst_height;  /* g1 */
    int start_delay; /* g2 */
    int end_delay;   /* g2 */
    int peak;        /* g1 */
    int arc;         /* g1 */
};

struct PktMapAnim
{
    int pos;    /* g1 */
    int id;     /* g2 */
    int height; /* g1 */
    int delay;  /* g2 */
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
        struct PktIfOpenChat _if_openchat;
        struct PktIfClose _if_close;
        struct PktIfSetTabActive _if_settab_active;
        struct PktVarpSmall _varp_small;
        struct PktVarpLarge _varp_large;
        struct PktUpdateStat _update_stat;
        struct PktUpdateRunEnergy _update_run_energy;
        struct PktIfSetColour _if_setcolour;
        struct PktIfSetHide _if_sethide;
        struct PktIfSetObject _if_setobject;
        struct PktIfSetModel _if_setmodel;
        struct PktIfSetAnim _if_setanim;
        struct PktIfSetPlayerHead _if_setplayerhead;
        struct PktIfSetText _if_settext;
        struct PktIfSetNpcHead _if_setnpchead;
        struct PktIfSetPosition _if_setposition;
        struct PktIfSetScrollPos _if_setscrollpos;
        struct PktMessageGame _message_game;
        struct PktMessagePrivate _message_private;
        struct PktChatFilterSettings _chat_filter_settings;
        struct PktUpdateZonePartialFollows _update_zone_partial_follows;
        struct PktUpdateZoneFullFollows _update_zone_full_follows;
        struct PktLocAddChange _loc_add_change;
        struct PktLocDel _loc_del;
        struct PktLocAnim _loc_anim;
        struct PktObjAdd _obj_add;
        struct PktObjDel _obj_del;
        struct PktObjReveal _obj_reveal;
        struct PktObjCount _obj_count;
        struct PktLocMerge _loc_merge;
        struct PktMapProjAnim _map_projanim;
        struct PktMapAnim _map_anim;
    };
};

#endif