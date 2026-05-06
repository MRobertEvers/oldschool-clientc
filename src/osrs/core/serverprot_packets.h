#ifndef OSRS_CORE_SERVERPROT_PACKETS_H
#define OSRS_CORE_SERVERPROT_PACKETS_H

#include "osrs/core/serverprot.h"
#include "osrs/core/serverprot_pkt_npcinfo.h"
#include "osrs/core/serverprot_pkt_playerinfo.h"

struct PktServerProtMapRebuildV1
{
    int zonex;
    int zonez;
};

struct PktServerProtUpdateInvFullV1
{
    int component_id;
    int size;
    // These are 1-indexed, e.g. 841 is shortbow. Over the
    // network, it's sent as 842.
    int* obj_ids;
    int* obj_counts;
};

struct PktServerProtIfSetTabV1
{
    int component_id;
    int tab_id;
};

struct PktServerProtIfOpenChatV1
{
    int component_id; /* g2: chat interface component to show */
};

struct PktServerProtIfCloseV1
{
    /* No payload - closes sidebar, chat, viewport */
    uint8_t _torirs_empty; /* C requires a member; keep 1-byte layout */
};

struct PktServerProtIfSetTabActiveV1
{
    int tab_id;
};

struct PktServerProtVarpSmallV1
{
    int variable;
    int value; /* g1b signed byte */
};

struct PktServerProtVarpLargeV1
{
    int variable;
    int value; /* g4 */
};

struct PktServerProtUpdateStatV1
{
    int stat;   /* g1: 0-22 */
    int xp;     /* g4 */
    int level;  /* g1: effective level */
};

struct PktServerProtUpdateRunEnergyV1
{
    int run_energy; /* g1: 0-100 */
};

struct PktServerProtIfSetColourV1
{
    int component_id; /* g2 */
    int colour;       /* g2: 15-bit (r<<10|g<<5|b) */
};

struct PktServerProtIfSetHideV1
{
    int component_id; /* g2 */
    int hide;        /* g1: 1=hide */
};

struct PktServerProtIfSetObjectV1
{
    int component_id; /* g2 */
    int obj_id;      /* g2 */
    int zoom;        /* g2 */
};

struct PktServerProtIfSetModelV1
{
    int component_id; /* g2 */
    int model_id;     /* g2 */
};

struct PktServerProtIfSetAnimV1
{
    int component_id; /* g2 */
    int anim_id;      /* g2 */
};

struct PktServerProtIfSetPlayerHeadV1
{
    int component_id; /* g2 */
};

struct PktServerProtIfSetTextV1
{
    int component_id; /* g2 */
    char* text;       /* gjstr / newline-terminated */
};

struct PktServerProtIfSetNpcHeadV1
{
    int component_id; /* g2 */
    int npc_id;       /* g2 */
};

struct PktServerProtIfSetPositionV1
{
    int component_id; /* g2 */
    int x;            /* g2b */
    int z;            /* g2b */
};

struct PktServerProtIfSetScrollPosV1
{
    int component_id; /* g2 */
    int pos;          /* g2 */
};

struct PktServerProtMessageGameV1
{
    char* text; /* gjstr / newline-terminated */
};

struct PktServerProtMessagePrivateV1
{
    int64_t from;       /* g8: base37 username */
    int message_id;    /* g4 */
    int staff_mod;     /* g1 */
    char* text;        /* WordPack.unpack(psize-13) */
};

struct PktServerProtChatFilterSettingsV1
{
    int chat_public_mode;  /* g1 */
    int chat_private_mode; /* g1 */
    int chat_trade_mode;   /* g1 */
};

/* Zone packets: position = base_x + (pos>>4)&7, base_z + pos&7. Base set by UPDATE_ZONE_* */
struct PktServerProtUpdateZonePartialFollowsV1
{
    int base_x; /* g1: zone base for subsequent zone packets */
    int base_z; /* g1 */
};

struct PktServerProtUpdateZoneFullFollowsV1
{
    int base_x; /* g1 */
    int base_z; /* g1 */
};

struct PktServerProtLocAddChangeV1
{
    int pos;   /* g1: x = base_x + (pos>>4)&7, z = base_z + pos&7 */
    int info;  /* g1: shape = info>>2, angle = info&3 */
    int loc_id; /* g2 */
};

struct PktServerProtLocDelV1
{
    int pos;  /* g1 */
    int info; /* g1: shape = info>>2, angle = info&3 */
};

struct PktServerProtLocAnimV1
{
    int pos;    /* g1 */
    int info;   /* g1 */
    int seq_id; /* g2 */
};

struct PktServerProtObjAddV1
{
    int pos;   /* g1 */
    int obj_id; /* g2 */
    int count;  /* g2 */
};

struct PktServerProtObjDelV1
{
    int pos;    /* g1 */
    int obj_id; /* g2 */
};

struct PktServerProtObjRevealV1
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int count;     /* g2 */
    int receiver;  /* g2: player index, skip if != local */
};

struct PktServerProtObjCountV1
{
    int pos;       /* g1 */
    int obj_id;    /* g2 */
    int old_count; /* g2 */
    int new_count; /* g2 */
};

struct PktServerProtLocMergeV1
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

struct PktServerProtMapProjAnimV1
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

struct PktServerProtMapAnimV1
{
    int pos;    /* g1 */
    int id;     /* g2 */
    int height; /* g1 */
    int delay;  /* g2 */
};

struct PktServerProtCamLookAtV1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktServerProtCamMoveToV1
{
    int local_x; /* g2 */
    int local_z; /* g2 */
    int height;  /* g2 */
};

struct PktServerProtCamShakeV1
{
    int axis;      /* g1 */
    int amplitude; /* g1 */
    int frequency; /* g1 */
    int speed;     /* g1 */
};

struct PktServerProtIfOpenMainV1
{
    int component_id; /* g2 */
};

struct PktServerProtIfOpenSideV1
{
    int component_id; /* g2 */
};

struct PktServerProtIfOpenOverlayV1
{
    int component_id; /* g2 */
};

struct PktServerProtIfOpenMainSideV1
{
    int main_component_id; /* g2 */
    int side_component_id; /* g2 */
};

struct PktServerProtHintArrowV1
{
    int type; /* g1: 1=NPC 2=player 3+=tile */
    int id;   /* g2: entity id or tile x/z depending on type */
    int z;    /* g2 (tile target only) */
    int height; /* g1 (tile target only) */
};

struct PktServerProtUpdatePidV1
{
    int local_player_index; /* g2 */
    int unused;             /* g1 */
};

struct PktServerProtUpdateRunWeightV1
{
    int run_weight; /* g2s: weight in grams */
};

struct PktServerProtUpdateInvStopTransmitV1
{
    int component_id; /* g2 */
};

struct PktServerProtUpdateInvPartialEntryV1
{
    int slot; /* g1 */
    /** After parse in serverprot_netrev245_2_parse.c: 0-based object id (wire g2 minus 1); -1 when
     * wire was 0 (empty). buildcachedat invSlotObjId uses 1-based wire (0 = empty). */
    int obj_id;
    int count; /* g1 or g4 when count byte was 255 */
};

struct PktServerProtUpdateInvPartialV1
{
    int component_id;
    int count;
    struct PktServerProtUpdateInvPartialEntryV1* entries; /* heap-allocated, count entries */
};

struct PktServerProtSetMultiwayV1
{
    int multiway; /* g1: 1=in multicombat zone */
};

/** Client.ts SET_PLAYER_OP: g1 index (1..5), g1 priority, gjstr (here: newline-terminated). */
struct PktServerProtSetPlayerOpV1
{
    int     op_index; /* 1..5 */
    int     priority; /* g1: Client.ts sets deprioritize when priority === 0 */
    char*   op_text;  /* heap; gstringnewline */
};

struct PktServerProtTutFlashV1
{
    int tab_id; /* g1: sidebar tab to flash */
};

struct PktServerProtTutOpenV1
{
    int component_id; /* g2: tutorial interface to open */
};

struct PktServerProtRebootTimerV1
{
    int ticks; /* g2: server reboot countdown in game ticks */
};

struct PktServerProtUpdateFriendListV1
{
    int64_t username; /* g8: base37 */
    int     world;    /* g1: 0 = offline, else world number */
};

struct PktServerProtUpdateIgnoreListV1
{
    int64_t* usernames; /* heap-alloc; count = data_size / 8 */
    int      count;
};

struct PktServerProtSynthSoundV1
{
    int id;    /* g2 */
    int loops; /* g1 */
    int delay; /* g2 */
};

struct PktServerProtMidiSongV1
{
    int id; /* g2 */
};

struct PktServerProtMidiJingleV1
{
    int delay; /* g2 */
    int id;    /* g2 */
};

struct RevServerProtPacket
{
    enum ServerProt packet_type;

    union
    {
        struct PktServerProtMapRebuildV1 map_rebuild_v1;
        struct PktNpcInfoV1 npc_info_v1;
        struct PktPlayerInfoV1 player_info_v1;
        struct PktServerProtUpdateInvFullV1 update_inv_full_v1;
        struct PktServerProtIfSetTabV1 if_settab_v1;
        struct PktServerProtIfOpenChatV1 if_openchat_v1;
        struct PktServerProtIfCloseV1 if_close_v1;
        struct PktServerProtIfSetTabActiveV1 if_settab_active_v1;
        struct PktServerProtVarpSmallV1 varp_small_v1;
        struct PktServerProtVarpLargeV1 varp_large_v1;
        struct PktServerProtUpdateStatV1 update_stat_v1;
        struct PktServerProtUpdateRunEnergyV1 update_run_energy_v1;
        struct PktServerProtIfSetColourV1 if_setcolour_v1;
        struct PktServerProtIfSetHideV1 if_sethide_v1;
        struct PktServerProtIfSetObjectV1 if_setobject_v1;
        struct PktServerProtIfSetModelV1 if_setmodel_v1;
        struct PktServerProtIfSetAnimV1 if_setanim_v1;
        struct PktServerProtIfSetPlayerHeadV1 if_setplayerhead_v1;
        struct PktServerProtIfSetTextV1 if_settext_v1;
        struct PktServerProtIfSetNpcHeadV1 if_setnpchead_v1;
        struct PktServerProtIfSetPositionV1 if_setposition_v1;
        struct PktServerProtIfSetScrollPosV1 if_setscrollpos_v1;
        struct PktServerProtMessageGameV1 message_game_v1;
        struct PktServerProtMessagePrivateV1 message_private_v1;
        struct PktServerProtChatFilterSettingsV1 chat_filter_settings_v1;
        struct PktServerProtUpdateZonePartialFollowsV1 update_zone_partial_follows_v1;
        struct PktServerProtUpdateZoneFullFollowsV1 update_zone_full_follows_v1;
        struct PktServerProtLocAddChangeV1 loc_add_change_v1;
        struct PktServerProtLocDelV1 loc_del_v1;
        struct PktServerProtLocAnimV1 loc_anim_v1;
        struct PktServerProtObjAddV1 obj_add_v1;
        struct PktServerProtObjDelV1 obj_del_v1;
        struct PktServerProtObjRevealV1 obj_reveal_v1;
        struct PktServerProtObjCountV1 obj_count_v1;
        struct PktServerProtLocMergeV1 loc_merge_v1;
        struct PktServerProtMapProjAnimV1 map_projanim_v1;
        struct PktServerProtMapAnimV1 map_anim_v1;
        struct PktServerProtCamLookAtV1 cam_lookat_v1;
        struct PktServerProtCamMoveToV1 cam_moveto_v1;
        struct PktServerProtCamShakeV1 cam_shake_v1;
        struct PktServerProtIfOpenMainV1 if_openmain_v1;
        struct PktServerProtIfOpenSideV1 if_openside_v1;
        struct PktServerProtIfOpenOverlayV1 if_openoverlay_v1;
        struct PktServerProtIfOpenMainSideV1 if_openmain_side_v1;
        struct PktServerProtHintArrowV1 hint_arrow_v1;
        struct PktServerProtUpdatePidV1 update_pid_v1;
        struct PktServerProtUpdateRunWeightV1 update_runweight_v1;
        struct PktServerProtUpdateInvStopTransmitV1 update_inv_stop_transmit_v1;
        struct PktServerProtUpdateInvPartialV1 update_inv_partial_v1;
        struct PktServerProtSetMultiwayV1 set_multiway_v1;
        struct PktServerProtSetPlayerOpV1 set_player_op_v1;
        struct PktServerProtTutFlashV1 tut_flash_v1;
        struct PktServerProtTutOpenV1 tut_open_v1;
        struct PktServerProtRebootTimerV1 reboot_timer_v1;
        struct PktServerProtUpdateFriendListV1 update_friendlist_v1;
        struct PktServerProtUpdateIgnoreListV1 update_ignorelist_v1;
        struct PktServerProtSynthSoundV1 synth_sound_v1;
        struct PktServerProtMidiSongV1 midi_song_v1;
        struct PktServerProtMidiJingleV1 midi_jingle_v1;
    } u;
};

#endif /* OSRS_CORE_SERVERPROT_PACKETS_H */
