#ifndef OSRS_CORE_SERVERPROT_PACKETS_H
#define OSRS_CORE_SERVERPROT_PACKETS_H

#include "osrs/core/serverprot.h"
#include "osrs/core/serverprot_pkt_npcinfo.h"
#include "osrs/core/serverprot_pkt_playerinfo.h"

struct PktServerProt_MapRebuild_v1
{
    int zonex;
    int zonez;
};

struct PktServerProt_UpdateInvFull_v1
{
    int component_id;
    int size;
    // These are 1-indexed, e.g. 841 is shortbow. Over the
    // network, it's sent as 842.
    int* obj_ids;
    int* obj_counts;
};

struct PktServerProt_IfSetTab_v1
{
    int component_id;
    int tab_id;
};

struct PktServerProt_IfOpenChat_v1
{
    int component_id; /* g2: chat interface component to show */
};

struct PktServerProt_IfClose_v1
{
    /* No payload - closes sidebar, chat, viewport */
    uint8_t _torirs_empty; /* C requires a member; keep 1-byte layout */
};

struct PktServerProt_IfSetTabActive_v1
{
    int tab_id;
};

struct PktServerProt_VarpSmall_v1
{
    int variable;
    int value; /* g1b signed byte */
};

struct PktServerProt_VarpLarge_v1
{
    int variable;
    int value; /* g4 */
};

struct PktServerProt_UpdateStat_v1
{
    int stat;  /* g1: 0-22 */
    int xp;    /* g4 */
    int level; /* g1: effective level */
};

struct PktServerProt_UpdateRunEnergy_v1
{
    int run_energy; /* g1: 0-100 */
};

struct PktServerProt_IfSetColour_v1
{
    int component_id; /* g2 */
    int colour;       /* g2: 15-bit (r<<10|g<<5|b) */
};

struct PktServerProt_IfSetHide_v1
{
    int component_id; /* g2 */
    int hide;         /* g1: 1=hide */
};

struct PktServerProt_IfSetObject_v1
{
    int component_id; /* g2 */
    int obj_id;       /* g2 */
    int zoom;         /* g2 */
};

struct PktServerProt_IfSetModel_v1
{
    int component_id; /* g2 */
    int model_id;     /* g2 */
};

struct PktServerProt_IfSetAnim_v1
{
    int component_id; /* g2 */
    int anim_id;      /* g2 */
};

struct PktServerProt_IfSetPlayerHead_v1
{
    int component_id; /* g2 */
};

struct PktServerProt_IfSetText_v1
{
    int component_id; /* g2 */
    char* text;       /* gjstr / newline-terminated */
};

struct PktServerProt_IfSetNpcHead_v1
{
    int component_id; /* g2 */
    int npc_id;       /* g2 */
};

struct PktServerProt_IfSetPosition_v1
{
    int component_id; /* g2 */
    int x;            /* g2b */
    int z;            /* g2b */
};

struct PktServerProt_IfSetScrollPos_v1
{
    int component_id; /* g2 */
    int pos;          /* g2 */
};

struct PktServerProt_MessageGame_v1
{
    char* text; /* gjstr / newline-terminated */
};

struct PktServerProt_MessagePrivate_v1
{
    int64_t from;   /* g8: base37 username */
    int message_id; /* g4 */
    int staff_mod;  /* g1 */
    char* text;     /* WordPack.unpack(psize-13) */
};

struct PktServerProt_ChatFilterSettings_v1
{
    int chat_public_mode;  /* g1 */
    int chat_private_mode; /* g1 */
    int chat_trade_mode;   /* g1 */
};

/* Zone packets: position = base_x + (pos>>4)&7, base_z + pos&7. Base set by UPDATE_ZONE_* */
struct PktServerProt_UpdateZonePartialFollows_v1
{
    int base_x; /* g1: zone base for subsequent zone packets */
    int base_z; /* g1 */
};

struct PktServerProt_UpdateZoneFullFollows_v1
{
    int base_x; /* g1 */
    int base_z; /* g1 */
};

struct PktServerProt_LocAddChange_v1
{
    int pos;    /* g1: x = base_x + (pos>>4)&7, z = base_z + pos&7 */
    int info;   /* g1: shape = info>>2, angle = info&3 */
    int loc_id; /* g2 */
};

struct PktServerProt_LocDel_v1
{
    int pos;  /* g1 */
    int info; /* g1: shape = info>>2, angle = info&3 */
};

struct PktServerProt_LocAnim_v1
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int seq_id; /* g2 */
};

struct PktServerProt_ObjAdd_v1
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
    int count;  /* g2 */
};

struct PktServerProt_ObjDel_v1
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
};

struct PktServerProt_ObjReveal_v1
{
    int pos;      /* g1 */
    int obj_id;   /* g2 */
    int count;    /* g2 */
    int receiver; /* g2: player index, skip if != local */
};

struct PktServerProt_ObjCount_v1
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int old_count; /* g2 */
    int new_count; /* g2 */
};

struct PktServerProt_LocMerge_v1
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int loc_id; /* g2 */
    int start;  /* g2 */
    int end;    /* g2 */
    int pid;    /* g2 */
    int east;   /* g1b */
    int south;  /* g1b */
    int west;   /* g1b */
    int north;  /* g1b */
};

struct PktServerProt_MapProjAnim_v1
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

struct PktServerProt_MapAnim_v1
{
    int pos;    /* g1 */
    int id;     /* g2 */
    int height; /* g1 */
    int delay;  /* g2 */
};

struct PktServerProt_CamLookAt_v1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktServerProt_CamMoveTo_v1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktServerProt_CamShake_v1
{
    int axis;      /* g1 */
    int amplitude; /* g1 */
    int frequency; /* g1 */
    int speed;     /* g1 */
};

struct PktServerProt_IfOpenMain_v1
{
    int component_id; /* g2 */
};

struct PktServerProt_IfOpenSide_v1
{
    int component_id; /* g2 */
};

struct PktServerProt_IfOpenOverlay_v1
{
    int component_id; /* g2 */
};

struct PktServerProt_IfOpenMainSide_v1
{
    int main_component_id; /* g2 */
    int side_component_id; /* g2 */
};

struct PktServerProt_HintArrow_v1
{
    int type;   /* g1: 1=NPC 2=player 3+=tile */
    int id;     /* g2: entity id or tile x/z depending on type */
    int z;      /* g2 (tile target only) */
    int height; /* g1 (tile target only) */
};

struct PktServerProt_UpdatePid_v1
{
    int local_player_index; /* g2 */
    int unused;             /* g1 */
};

struct PktServerProt_UpdateRunWeight_v1
{
    int run_weight; /* g2s: weight in grams */
};

struct PktServerProt_UpdateInvStopTransmit_v1
{
    int component_id; /* g2 */
};

struct PktServerProt_UpdateInvPartialEntry_v1
{
    int slot; /* g1 */
    /** After parse in serverprot_netrev245_2_parse.c: 0-based object id (wire g2 minus 1); -1 when
     * wire was 0 (empty). buildcachedat invSlotObjId uses 1-based wire (0 = empty). */
    int obj_id;
    int count; /* g1 or g4 when count byte was 255 */
};

struct PktServerProt_UpdateInvPartial_v1
{
    int component_id;
    int count;
    struct PktServerProt_UpdateInvPartialEntry_v1* entries; /* heap-allocated, count entries */
};

struct PktServerProt_SetMultiway_v1
{
    int multiway; /* g1: 1=in multicombat zone */
};

/** Client.ts SET_PLAYER_OP: g1 index (1..5), g1 priority, gjstr (here: newline-terminated). */
struct PktServerProt_SetPlayerOp_v1
{
    int op_index;  /* 1..5 */
    int priority;  /* g1: Client.ts sets deprioritize when priority === 0 */
    char* op_text; /* heap; gstringnewline */
};

struct PktServerProt_TutFlash_v1
{
    int tab_id; /* g1: sidebar tab to flash */
};

struct PktServerProt_TutOpen_v1
{
    int component_id; /* g2: tutorial interface to open */
};

struct PktServerProt_RebootTimer_v1
{
    int ticks; /* g2: server reboot countdown in game ticks */
};

struct PktServerProt_UpdateFriendList_v1
{
    int64_t username; /* g8: base37 */
    int world;        /* g1: 0 = offline, else world number */
};

struct PktServerProt_UpdateIgnoreList_v1
{
    int64_t* usernames; /* heap-alloc; count = data_size / 8 */
    int count;
};

struct PktServerProt_SynthSound_v1
{
    int id;    /* g2 */
    int loops; /* g1 */
    int delay; /* g2 */
};

struct PktServerProt_MidiSong_v1
{
    int id; /* g2 */
};

struct PktServerProt_MidiJingle_v1
{
    int delay; /* g2 */
    int id;    /* g2 */
};

struct RevServerProtPacket
{
    enum ServerProt packet_type;

    union
    {
        struct PktServerProt_MapRebuild_v1 map_rebuild_v1;
        struct PktServerProt_NpcInfo_v1 npc_info_v1;
        struct PktServerProt_PlayerInfo_v1 player_info_v1;
        struct PktServerProt_UpdateInvFull_v1 update_inv_full_v1;
        struct PktServerProt_IfSetTab_v1 if_settab_v1;
        struct PktServerProt_IfOpenChat_v1 if_openchat_v1;
        struct PktServerProt_IfClose_v1 if_close_v1;
        struct PktServerProt_IfSetTabActive_v1 if_settab_active_v1;
        struct PktServerProt_VarpSmall_v1 varp_small_v1;
        struct PktServerProt_VarpLarge_v1 varp_large_v1;
        struct PktServerProt_UpdateStat_v1 update_stat_v1;
        struct PktServerProt_UpdateRunEnergy_v1 update_run_energy_v1;
        struct PktServerProt_IfSetColour_v1 if_setcolour_v1;
        struct PktServerProt_IfSetHide_v1 if_sethide_v1;
        struct PktServerProt_IfSetObject_v1 if_setobject_v1;
        struct PktServerProt_IfSetModel_v1 if_setmodel_v1;
        struct PktServerProt_IfSetAnim_v1 if_setanim_v1;
        struct PktServerProt_IfSetPlayerHead_v1 if_setplayerhead_v1;
        struct PktServerProt_IfSetText_v1 if_settext_v1;
        struct PktServerProt_IfSetNpcHead_v1 if_setnpchead_v1;
        struct PktServerProt_IfSetPosition_v1 if_setposition_v1;
        struct PktServerProt_IfSetScrollPos_v1 if_setscrollpos_v1;
        struct PktServerProt_MessageGame_v1 message_game_v1;
        struct PktServerProt_MessagePrivate_v1 message_private_v1;
        struct PktServerProt_ChatFilterSettings_v1 chat_filter_settings_v1;
        struct PktServerProt_UpdateZonePartialFollows_v1 update_zone_partial_follows_v1;
        struct PktServerProt_UpdateZoneFullFollows_v1 update_zone_full_follows_v1;
        struct PktServerProt_LocAddChange_v1 loc_add_change_v1;
        struct PktServerProt_LocDel_v1 loc_del_v1;
        struct PktServerProt_LocAnim_v1 loc_anim_v1;
        struct PktServerProt_ObjAdd_v1 obj_add_v1;
        struct PktServerProt_ObjDel_v1 obj_del_v1;
        struct PktServerProt_ObjReveal_v1 obj_reveal_v1;
        struct PktServerProt_ObjCount_v1 obj_count_v1;
        struct PktServerProt_LocMerge_v1 loc_merge_v1;
        struct PktServerProt_MapProjAnim_v1 map_projanim_v1;
        struct PktServerProt_MapAnim_v1 map_anim_v1;
        struct PktServerProt_CamLookAt_v1 cam_lookat_v1;
        struct PktServerProt_CamMoveTo_v1 cam_moveto_v1;
        struct PktServerProt_CamShake_v1 cam_shake_v1;
        struct PktServerProt_IfOpenMain_v1 if_openmain_v1;
        struct PktServerProt_IfOpenSide_v1 if_openside_v1;
        struct PktServerProt_IfOpenOverlay_v1 if_openoverlay_v1;
        struct PktServerProt_IfOpenMainSide_v1 if_openmain_side_v1;
        struct PktServerProt_HintArrow_v1 hint_arrow_v1;
        struct PktServerProt_UpdatePid_v1 update_pid_v1;
        struct PktServerProt_UpdateRunWeight_v1 update_runweight_v1;
        struct PktServerProt_UpdateInvStopTransmit_v1 update_inv_stop_transmit_v1;
        struct PktServerProt_UpdateInvPartial_v1 update_inv_partial_v1;
        struct PktServerProt_SetMultiway_v1 set_multiway_v1;
        struct PktServerProt_SetPlayerOp_v1 set_player_op_v1;
        struct PktServerProt_TutFlash_v1 tut_flash_v1;
        struct PktServerProt_TutOpen_v1 tut_open_v1;
        struct PktServerProt_RebootTimer_v1 reboot_timer_v1;
        struct PktServerProt_UpdateFriendList_v1 update_friendlist_v1;
        struct PktServerProt_UpdateIgnoreList_v1 update_ignorelist_v1;
        struct PktServerProt_SynthSound_v1 synth_sound_v1;
        struct PktServerProt_MidiSong_v1 midi_song_v1;
        struct PktServerProt_MidiJingle_v1 midi_jingle_v1;
    } u;
};

#endif /* OSRS_CORE_SERVERPROT_PACKETS_H */
