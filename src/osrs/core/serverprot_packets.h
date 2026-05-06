#ifndef OSRS_CORE_SERVERPROT_PACKETS_H
#define OSRS_CORE_SERVERPROT_PACKETS_H

#include "osrs/core/serverprot.h"
#include "osrs/core/serverprot_pkt_npcinfo.h"
#include "osrs/core/serverprot_pkt_playerinfo.h"

struct PktMapRebuildV1
{
    int zonex;
    int zonez;
};

struct PktUpdateInvFullV1
{
    int component_id;
    int size;
    // These are 1-indexed, e.g. 841 is shortbow. Over the
    // network, it's sent as 842.
    int* obj_ids;
    int* obj_counts;
};

struct PktIfSetTabV1
{
    int component_id;
    int tab_id;
};

struct PktIfOpenChatV1
{
    int component_id; /* g2: chat interface component to show */
};

struct PktIfCloseV1
{
    /* No payload - closes sidebar, chat, viewport */
    uint8_t _torirs_empty; /* C requires a member; keep 1-byte layout */
};

struct PktIfSetTabActiveV1
{
    int tab_id;
};

struct PktVarpSmallV1
{
    int variable;
    int value; /* g1b signed byte */
};

struct PktVarpLargeV1
{
    int variable;
    int value; /* g4 */
};

struct PktUpdateStatV1
{
    int stat;   /* g1: 0-22 */
    int xp;     /* g4 */
    int level;  /* g1: effective level */
};

struct PktUpdateRunEnergyV1
{
    int run_energy; /* g1: 0-100 */
};

struct PktIfSetColourV1
{
    int component_id; /* g2 */
    int colour;       /* g2: 15-bit (r<<10|g<<5|b) */
};

struct PktIfSetHideV1
{
    int component_id; /* g2 */
    int hide;        /* g1: 1=hide */
};

struct PktIfSetObjectV1
{
    int component_id; /* g2 */
    int obj_id;      /* g2 */
    int zoom;        /* g2 */
};

struct PktIfSetModelV1
{
    int component_id; /* g2 */
    int model_id;     /* g2 */
};

struct PktIfSetAnimV1
{
    int component_id; /* g2 */
    int anim_id;      /* g2 */
};

struct PktIfSetPlayerHeadV1
{
    int component_id; /* g2 */
};

struct PktIfSetTextV1
{
    int component_id; /* g2 */
    char* text;       /* gjstr / newline-terminated */
};

struct PktIfSetNpcHeadV1
{
    int component_id; /* g2 */
    int npc_id;       /* g2 */
};

struct PktIfSetPositionV1
{
    int component_id; /* g2 */
    int x;            /* g2b */
    int z;            /* g2b */
};

struct PktIfSetScrollPosV1
{
    int component_id; /* g2 */
    int pos;          /* g2 */
};

struct PktMessageGameV1
{
    char* text; /* gjstr / newline-terminated */
};

struct PktMessagePrivateV1
{
    int64_t from;       /* g8: base37 username */
    int message_id;    /* g4 */
    int staff_mod;     /* g1 */
    char* text;        /* WordPack.unpack(psize-13) */
};

struct PktChatFilterSettingsV1
{
    int chat_public_mode;  /* g1 */
    int chat_private_mode; /* g1 */
    int chat_trade_mode;   /* g1 */
};

/* Zone packets: position = base_x + (pos>>4)&7, base_z + pos&7. Base set by UPDATE_ZONE_* */
struct PktUpdateZonePartialFollowsV1
{
    int base_x; /* g1: zone base for subsequent zone packets */
    int base_z; /* g1 */
};

struct PktUpdateZoneFullFollowsV1
{
    int base_x; /* g1 */
    int base_z; /* g1 */
};

struct PktLocAddChangeV1
{
    int pos;   /* g1: x = base_x + (pos>>4)&7, z = base_z + pos&7 */
    int info;  /* g1: shape = info>>2, angle = info&3 */
    int loc_id; /* g2 */
};

struct PktLocDelV1
{
    int pos;  /* g1 */
    int info; /* g1: shape = info>>2, angle = info&3 */
};

struct PktLocAnimV1
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int seq_id; /* g2 */
};

struct PktObjAddV1
{
    int pos;   /* g1 */
    int obj_id; /* g2 */
    int count;  /* g2 */
};

struct PktObjDelV1
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
};

struct PktObjRevealV1
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int count;     /* g2 */
    int receiver;  /* g2: player index, skip if != local */
};

struct PktObjCountV1
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int old_count; /* g2 */
    int new_count; /* g2 */
};

struct PktLocMergeV1
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

struct PktMapProjAnimV1
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

struct PktMapAnimV1
{
    int pos;    /* g1 */
    int id;     /* g2 */
    int height; /* g1 */
    int delay;  /* g2 */
};

struct PktCamLookAtV1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktCamMoveToV1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktCamShakeV1
{
    int axis;      /* g1 */
    int amplitude; /* g1 */
    int frequency; /* g1 */
    int speed;     /* g1 */
};

struct PktIfOpenMainV1
{
    int component_id; /* g2 */
};

struct PktIfOpenSideV1
{
    int component_id; /* g2 */
};

struct PktIfOpenOverlayV1
{
    int component_id; /* g2 */
};

struct PktIfOpenMainSideV1
{
    int main_component_id; /* g2 */
    int side_component_id; /* g2 */
};

struct PktHintArrowV1
{
    int type; /* g1: 1=NPC 2=player 3+=tile */
    int id;   /* g2: entity id or tile x/z depending on type */
    int z;    /* g2 (tile target only) */
    int height; /* g1 (tile target only) */
};

struct PktUpdatePidV1
{
    int local_player_index; /* g2 */
    int unused;             /* g1 */
};

struct PktUpdateRunWeightV1
{
    int run_weight; /* g2s: weight in grams */
};

struct PktUpdateInvStopTransmitV1
{
    int component_id; /* g2 */
};

struct PktUpdateInvPartialEntryV1
{
    int slot; /* g1 */
    /** After parse in serverprot_netrev245_2_parse.c: 0-based object id (wire g2 minus 1); -1 when
     * wire was 0 (empty). buildcachedat invSlotObjId uses 1-based wire (0 = empty). */
    int obj_id;
    int count; /* g1 or g4 when count byte was 255 */
};

struct PktUpdateInvPartialV1
{
    int component_id;
    int count;
    struct PktUpdateInvPartialEntryV1* entries; /* heap-allocated, count entries */
};

struct PktSetMultiwayV1
{
    int multiway; /* g1: 1=in multicombat zone */
};

/** Client.ts SET_PLAYER_OP: g1 index (1..5), g1 priority, gjstr (here: newline-terminated). */
struct PktSetPlayerOpV1
{
    int     op_index; /* 1..5 */
    int     priority; /* g1: Client.ts sets deprioritize when priority === 0 */
    char*   op_text;  /* heap; gstringnewline */
};

struct PktTutFlashV1
{
    int tab_id; /* g1: sidebar tab to flash */
};

struct PktTutOpenV1
{
    int component_id; /* g2: tutorial interface to open */
};

struct PktRebootTimerV1
{
    int ticks; /* g2: server reboot countdown in game ticks */
};

struct PktUpdateFriendListV1
{
    int64_t username; /* g8: base37 */
    int     world;    /* g1: 0 = offline, else world number */
};

struct PktUpdateIgnoreListV1
{
    int64_t* usernames; /* heap-alloc; count = data_size / 8 */
    int      count;
};

struct PktSynthSoundV1
{
    int id;    /* g2 */
    int loops; /* g1 */
    int delay; /* g2 */
};

struct PktMidiSongV1
{
    int id; /* g2 */
};

struct PktMidiJingleV1
{
    int delay; /* g2 */
    int id;    /* g2 */
};

struct RevServerProtPacket
{
    enum ServerProt packet_type;

    union
    {
        struct PktMapRebuildV1 map_rebuild_v1;
        struct PktNpcInfoV1 npc_info_v1;
        struct PktPlayerInfoV1 player_info_v1;
        struct PktUpdateInvFullV1 update_inv_full_v1;
        struct PktIfSetTabV1 if_settab_v1;
        struct PktIfOpenChatV1 if_openchat_v1;
        struct PktIfCloseV1 if_close_v1;
        struct PktIfSetTabActiveV1 if_settab_active_v1;
        struct PktVarpSmallV1 varp_small_v1;
        struct PktVarpLargeV1 varp_large_v1;
        struct PktUpdateStatV1 update_stat_v1;
        struct PktUpdateRunEnergyV1 update_run_energy_v1;
        struct PktIfSetColourV1 if_setcolour_v1;
        struct PktIfSetHideV1 if_sethide_v1;
        struct PktIfSetObjectV1 if_setobject_v1;
        struct PktIfSetModelV1 if_setmodel_v1;
        struct PktIfSetAnimV1 if_setanim_v1;
        struct PktIfSetPlayerHeadV1 if_setplayerhead_v1;
        struct PktIfSetTextV1 if_settext_v1;
        struct PktIfSetNpcHeadV1 if_setnpchead_v1;
        struct PktIfSetPositionV1 if_setposition_v1;
        struct PktIfSetScrollPosV1 if_setscrollpos_v1;
        struct PktMessageGameV1 message_game_v1;
        struct PktMessagePrivateV1 message_private_v1;
        struct PktChatFilterSettingsV1 chat_filter_settings_v1;
        struct PktUpdateZonePartialFollowsV1 update_zone_partial_follows_v1;
        struct PktUpdateZoneFullFollowsV1 update_zone_full_follows_v1;
        struct PktLocAddChangeV1 loc_add_change_v1;
        struct PktLocDelV1 loc_del_v1;
        struct PktLocAnimV1 loc_anim_v1;
        struct PktObjAddV1 obj_add_v1;
        struct PktObjDelV1 obj_del_v1;
        struct PktObjRevealV1 obj_reveal_v1;
        struct PktObjCountV1 obj_count_v1;
        struct PktLocMergeV1 loc_merge_v1;
        struct PktMapProjAnimV1 map_projanim_v1;
        struct PktMapAnimV1 map_anim_v1;
        struct PktCamLookAtV1 cam_lookat_v1;
        struct PktCamMoveToV1 cam_moveto_v1;
        struct PktCamShakeV1 cam_shake_v1;
        struct PktIfOpenMainV1 if_openmain_v1;
        struct PktIfOpenSideV1 if_openside_v1;
        struct PktIfOpenOverlayV1 if_openoverlay_v1;
        struct PktIfOpenMainSideV1 if_openmain_side_v1;
        struct PktHintArrowV1 hint_arrow_v1;
        struct PktUpdatePidV1 update_pid_v1;
        struct PktUpdateRunWeightV1 update_runweight_v1;
        struct PktUpdateInvStopTransmitV1 update_inv_stop_transmit_v1;
        struct PktUpdateInvPartialV1 update_inv_partial_v1;
        struct PktSetMultiwayV1 set_multiway_v1;
        struct PktSetPlayerOpV1 set_player_op_v1;
        struct PktTutFlashV1 tut_flash_v1;
        struct PktTutOpenV1 tut_open_v1;
        struct PktRebootTimerV1 reboot_timer_v1;
        struct PktUpdateFriendListV1 update_friendlist_v1;
        struct PktUpdateIgnoreListV1 update_ignorelist_v1;
        struct PktSynthSoundV1 synth_sound_v1;
        struct PktMidiSongV1 midi_song_v1;
        struct PktMidiJingleV1 midi_jingle_v1;
    } u;
};

#endif /* OSRS_CORE_SERVERPROT_PACKETS_H */
