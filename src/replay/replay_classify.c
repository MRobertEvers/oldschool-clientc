/*
 * The chase's packet classes (replay_classify.h). One row per name.
 *
 * Where the design's table ("Why three sources") names a packet, its class is
 * the design's. The rest were decided from what the executor does with the
 * packet (rs_gameproto_exec.c, task_gameproto_exec.c), and each says why:
 *
 *   SESSION   UNSET_MAP_FLAG, HINT_ARROW, UPDATE_PID, SET_MULTIWAY,
 *             MINIMAP_TOGGLE, HIDE*OPS, ZBUF, SET/RESET_INTERACTION_MODE,
 *             CAM_MODE, OCULUS_SYNC, CAM_SKYBOX: client state the server set
 *             once and the client keeps (a flag, an arrow, a mode), not a
 *             description of the world a keyframe restates. UPDATE_PID's
 *             index is the keyframe's too, so executing it is harmless.
 *             MIDI_SONG_WITHSECONDARY, MIDI_SWAP, MIDI_SONG_STOP,
 *             AMBIENTSOUND_START/STOP: the music / ambience in force, the
 *             design's MIDI_SONG row (muted while seeking).
 *             SEND_PING, REFLECTION_CHECKER, UNKNOWN_STRING, SITE_SETTINGS,
 *             SET_WEBSITE_TOKEN, UPDATE_UID192, ACCOUNT_FLAGS, HISCORE_REPLY,
 *             OBJUNLOCK_*, GROUP_*, URL_OPEN: session bookkeeping with no
 *             world in it. NONE: a frame no name decodes; staged as today
 *             (net drops it).
 *   WORLD     SET_NPC_UPDATE_ORIGIN, SET_ACTIVE_WORLD, CLEAR_ENTITIES,
 *             REBUILD_WORLDENTITY: entity / view state the keyframe carries
 *             (the origin) or that marks a world-entity span (which falls
 *             back to a full simulation). NPC_HEADICON_SPECIFIC: a per-npc
 *             override the ledger keeps as a sticky slot per head-icon slot
 *             and the keyframe re-sends after the npcs stand.
 *             OBJ_*_SPECIFIC, OBJ_(UN)CUSTOMISE: zone ops (the ledger files
 *             the per-player forms under the zone arm they run through;
 *             OBJ_ENABLED_OPS_SPECIFIC under OBJ_REVEAL's enabled-ops form,
 *             as exec_obj_specific converts it).
 *   TRANSIENT RESET_ANIMS (stops entity animations: the pre-roll's entities
 *             are new), MAP_ANIM_SPECIFIC and the other *_SPECIFIC visuals,
 *             SCRIPTEDPROJ_ADD / _CHANGE: one-shot effects.
 *   STREAM    LOGOUT, LOGOUT_WITHREASON, LOGOUT_TRANSFER (the session's end,
 *             as linear play has it), PACKET_GROUP_START (a no-op that only
 *             sizes a group). The replay's own messages (the chase's tile and
 *             chat, the keyframe) are control frames, not packets
 *             (net/torirs_control.h), and have no class.
 */
#include "replay/replay_classify.h"

#include <assert.h>
#include <stdio.h>

#include "log/torirs_log.h"

#define W REPLAY_PKT_WORLD
#define S REPLAY_PKT_SESSION
#define T REPLAY_PKT_TRANSIENT
#define X REPLAY_PKT_STREAM

static unsigned char const k_class[PKT_NAME_COUNT] = {
    [PKT_NAME_NONE] = S,

    /* interfaces */
    [PKT_NAME_IF_OPENCHAT] = S,
    [PKT_NAME_IF_OPENMAIN_SIDE] = S,
    [PKT_NAME_IF_CLOSE] = S,
    [PKT_NAME_IF_SETTAB] = S,
    [PKT_NAME_IF_SETTAB_ACTIVE] = S,
    [PKT_NAME_IF_OPENMAIN] = S,
    [PKT_NAME_IF_OPENSIDE] = S,
    [PKT_NAME_IF_OPENOVERLAY] = S,
    [PKT_NAME_IF_OPENTOP] = S,
    [PKT_NAME_IF_OPENSUB] = S,
    [PKT_NAME_IF_CLOSESUB] = S,
    [PKT_NAME_IF_MOVESUB] = S,
    [PKT_NAME_IF_RESYNC_V2] = S,
    [PKT_NAME_IF_CLEARINV] = S,
    [PKT_NAME_IF_SETCOLOUR] = S,
    [PKT_NAME_IF_SETHIDE] = S,
    [PKT_NAME_IF_SETEVENTS] = S,
    [PKT_NAME_IF_SETOBJECT] = S,
    [PKT_NAME_IF_SETMODEL] = S,
    [PKT_NAME_IF_SETANIM] = S,
    [PKT_NAME_IF_SETPLAYERHEAD] = S,
    [PKT_NAME_IF_SETTEXT] = S,
    [PKT_NAME_IF_SETNPCHEAD] = S,
    [PKT_NAME_IF_SETPOSITION] = S,
    [PKT_NAME_IF_SETSCROLLPOS] = S,
    [PKT_NAME_IF_SETROTATESPEED] = S,
    [PKT_NAME_IF_SETANGLE] = S,
    [PKT_NAME_IF_SETNPCHEAD_ACTIVE] = S,
    [PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR] = S,
    [PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE] = S,
    [PKT_NAME_IF_SETPLAYERMODEL_OBJ] = S,
    [PKT_NAME_IF_SETPLAYERMODEL_SELF] = S,
    [PKT_NAME_TUT_FLASH] = S,
    [PKT_NAME_TUT_OPEN] = S,

    /* inventories, camera */
    [PKT_NAME_UPDATE_INV_STOP_TRANSMIT] = S,
    [PKT_NAME_UPDATE_INV_FULL] = S,
    [PKT_NAME_UPDATE_INV_PARTIAL] = S,
    [PKT_NAME_CAM_LOOKAT] = S,
    [PKT_NAME_CAM_SHAKE] = S,
    [PKT_NAME_CAM_MOVETO] = S,
    [PKT_NAME_CAM_RESET] = S,

    /* entity info */
    [PKT_NAME_NPC_INFO] = W,
    [PKT_NAME_PLAYER_INFO] = W,

    /* tracking, social */
    [PKT_NAME_FINISH_TRACKING] = S,
    [PKT_NAME_ENABLE_TRACKING] = S,
    [PKT_NAME_MESSAGE_GAME] = S,
    [PKT_NAME_UPDATE_IGNORELIST] = S,
    [PKT_NAME_CHAT_FILTER_SETTINGS] = S,
    [PKT_NAME_MESSAGE_PRIVATE] = S,
    [PKT_NAME_UPDATE_FRIENDLIST] = S,
    [PKT_NAME_FRIENDLIST_LOADED] = S,

    /* misc */
    [PKT_NAME_UNSET_MAP_FLAG] = S,
    [PKT_NAME_UPDATE_RUNWEIGHT] = S,
    [PKT_NAME_HINT_ARROW] = S,
    [PKT_NAME_UPDATE_REBOOT_TIMER] = S,
    [PKT_NAME_UPDATE_STAT] = S,
    [PKT_NAME_UPDATE_RUNENERGY] = S,
    [PKT_NAME_RESET_ANIMS] = T,
    [PKT_NAME_UPDATE_PID] = S,
    [PKT_NAME_LAST_LOGIN_INFO] = S,
    [PKT_NAME_LOGOUT] = X,
    [PKT_NAME_P_COUNTDIALOG] = S,
    [PKT_NAME_SET_MULTIWAY] = S,
    [PKT_NAME_SET_PLAYER_OP] = S,
    [PKT_NAME_MINIMAP_TOGGLE] = S,
    [PKT_NAME_TRIGGER_ONDIALOGABORT] = S,
    [PKT_NAME_SERVER_TICK_END] = X,
    [PKT_NAME_RUNCLIENTSCRIPT] = S,

    /* maps, vars, audio */
    [PKT_NAME_REBUILD_NORMAL] = W,
    [PKT_NAME_REBUILD_REGION] = W,
    [PKT_NAME_VARP_SMALL] = S,
    [PKT_NAME_VARP_LARGE] = S,
    [PKT_NAME_VARP_SYNC] = S,
    [PKT_NAME_VARP_RESET] = S,
    [PKT_NAME_SYNTH_SOUND] = T,
    [PKT_NAME_MIDI_SONG] = S,
    [PKT_NAME_MIDI_JINGLE] = T,
    [PKT_NAME_MIDI_SONG_WITHSECONDARY] = S,
    [PKT_NAME_MIDI_SWAP] = S,
    [PKT_NAME_MIDI_SONG_STOP] = S,
    [PKT_NAME_AMBIENTSOUND_START] = S,
    [PKT_NAME_AMBIENTSOUND_STOP] = S,

    /* zones */
    [PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS] = W,
    [PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS] = W,
    [PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED] = W,
    [PKT_NAME_LOC_MERGE] = T,
    [PKT_NAME_LOC_ANIM] = T,
    [PKT_NAME_OBJ_DEL] = W,
    [PKT_NAME_OBJ_REVEAL] = W,
    [PKT_NAME_LOC_ADD_CHANGE] = W,
    [PKT_NAME_MAP_PROJANIM] = T,
    [PKT_NAME_LOC_DEL] = W,
    [PKT_NAME_OBJ_COUNT] = W,
    [PKT_NAME_MAP_ANIM] = T,
    [PKT_NAME_OBJ_ADD] = W,
    [PKT_NAME_SOUND_AREA] = T,
    [PKT_NAME_MAP_ANIM_SPECIFIC] = T,
    [PKT_NAME_SET_NPC_UPDATE_ORIGIN] = W,
    [PKT_NAME_SET_ACTIVE_WORLD] = W,
    [PKT_NAME_WORLDENTITY_INFO] = W,
    [PKT_NAME_REBUILD_WORLDENTITY] = W,

    /* friends chat, clans, trading */
    [PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL] = S,
    [PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER] = S,
    [PKT_NAME_VARCLAN] = S,
    [PKT_NAME_VARCLAN_ENABLE] = S,
    [PKT_NAME_VARCLAN_DISABLE] = S,
    [PKT_NAME_CLANCHANNEL_FULL] = S,
    [PKT_NAME_CLANCHANNEL_DELTA] = S,
    [PKT_NAME_CLANSETTINGS_FULL] = S,
    [PKT_NAME_CLANSETTINGS_DELTA] = S,
    [PKT_NAME_MESSAGE_CLANCHANNEL] = S,
    [PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM] = S,
    [PKT_NAME_UPDATE_STOCKMARKET_SLOT] = S,
    [PKT_NAME_UPDATE_TRADINGPOST] = S,
    [PKT_NAME_HIDENPCOPS] = S,
    [PKT_NAME_HIDELOCOPS] = S,
    [PKT_NAME_HIDEOBJOPS] = S,

    /* appended names */
    [PKT_NAME_CAM_MOVETO_CYCLES] = S,
    [PKT_NAME_CAM_ROTATETO_COORDINATE] = S,
    [PKT_NAME_VARP_LONG] = S,
    [PKT_NAME_SCRIPTEDPROJ_ADD] = T,
    [PKT_NAME_CAM_UNLOCK] = S,
    [PKT_NAME_NPC_INFO_LARGE] = W,
    [PKT_NAME_CAM_TARGET] = S,
    [PKT_NAME_CAM_MOVETO_ARC] = S,
    [PKT_NAME_SEND_PING] = S,
    [PKT_NAME_UNKNOWN_STRING] = S,
    [PKT_NAME_REFLECTION_CHECKER] = S,
    [PKT_NAME_LOGOUT_WITHREASON] = X,
    [PKT_NAME_PACKET_GROUP_START] = X,
    [PKT_NAME_SET_INTERACTION_MODE] = S,
    [PKT_NAME_RESET_INTERACTION_MODE] = S,
    [PKT_NAME_NPC_HEADICON_SPECIFIC] = W,
    [PKT_NAME_OBJ_ADD_SPECIFIC] = W,
    [PKT_NAME_OBJ_DEL_SPECIFIC] = W,
    [PKT_NAME_OBJ_COUNT_SPECIFIC] = W,
    [PKT_NAME_SITE_SETTINGS] = S,
    [PKT_NAME_CLEAR_ENTITIES] = W,
    [PKT_NAME_ZBUF] = S,
    [PKT_NAME_LOGOUT_TRANSFER] = X,
    [PKT_NAME_GROUP_FULL] = S,
    [PKT_NAME_CAM_SMOOTHRESET] = S,
    [PKT_NAME_ANIM_SPECIFIC] = T,
    [PKT_NAME_NPC_ANIM_SPECIFIC] = T,
    [PKT_NAME_NPC_SPOTANIM_SPECIFIC] = T,
    [PKT_NAME_PLAYER_SPOTANIM_SPECIFIC] = T,
    [PKT_NAME_LOC_ANIM_SPECIFIC] = T,
    [PKT_NAME_PROJANIM_SPECIFIC] = T,
    [PKT_NAME_CAM_ROTATEBY] = S,
    [PKT_NAME_CAM_ROTATETO] = S,
    [PKT_NAME_CAM_LOOKAT_CYCLES] = S,
    [PKT_NAME_MESSAGE_PRIVATE_ECHO] = S,
    [PKT_NAME_MESSAGE_FRIENDCHANNEL] = S,
    [PKT_NAME_URL_OPEN] = S,
    [PKT_NAME_SCRIPTEDPROJ_CHANGE] = T,
    [PKT_NAME_OBJ_CUSTOMISE] = W,
    [PKT_NAME_OBJ_UNCUSTOMISE] = W,
    [PKT_NAME_OBJ_CUSTOMISE_SPECIFIC] = W,
    [PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC] = W,
    [PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC] = W,
    [PKT_NAME_OBJUNLOCK_RESET] = S,
    [PKT_NAME_OBJUNLOCK_UPDATE] = S,
    [PKT_NAME_ACCOUNT_FLAGS] = S,
    [PKT_NAME_HISCORE_REPLY] = S,
    [PKT_NAME_SET_WEBSITE_TOKEN] = S,
    [PKT_NAME_UPDATE_UID192] = S,
    [PKT_NAME_CAM_MODE] = S,
    [PKT_NAME_OCULUS_SYNC] = S,
    [PKT_NAME_CAM_SKYBOX] = S,
    [PKT_NAME_SET_HEATMAP_ENABLED] = S,
    [PKT_NAME_GROUP_VAR] = S,
};

#undef W
#undef S
#undef T
#undef X

void
ReplayClassify_Check(void)
{
    for( int name = 0; name < PKT_NAME_COUNT; name++ )
    {
        if( k_class[name] == REPLAY_PKT_UNCLASSIFIED )
            TORIRS_ERR("replay: packet name %d has no chase class (replay_classify.c)\n", name);
        assert(k_class[name] != REPLAY_PKT_UNCLASSIFIED);
    }
}

enum ReplayPktClass
ReplayClassify_Packet(enum GameProtoPktName name)
{
    assert((int)name >= 0);
    assert((int)name < PKT_NAME_COUNT);
    assert(k_class[name] != REPLAY_PKT_UNCLASSIFIED);
    return (enum ReplayPktClass)k_class[name];
}

char const*
ReplayClassify_Name(enum ReplayPktClass cls)
{
    if( cls == REPLAY_PKT_WORLD )
        return "world";
    if( cls == REPLAY_PKT_SESSION )
        return "session";
    if( cls == REPLAY_PKT_TRANSIENT )
        return "transient";
    if( cls == REPLAY_PKT_STREAM )
        return "stream";
    return "unclassified";
}
