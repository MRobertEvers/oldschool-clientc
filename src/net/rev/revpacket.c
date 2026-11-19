/*
 * What every revision's decoded packet shares, whoever decoded it: freeing its
 * owned fields, and the one table of packet kinds the packet pump asks its two
 * questions of.
 */
#include "revpacket.h"

#include "mem/torimem_rscache.h"
#include "mem/torirs_mem.h"
#include "net/torirs_control.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * THE PACKET KINDS, one row per canonical name (revpacket.h says what the three
 * columns mean). The packet pump reads LATCH and FLUSH off the row
 * (app_pump_net_packets); test-packet-kinds checks that every name has a row,
 * that every row that flushes also latches, and that a row that waits says for
 * what.
 *
 * Which packets latch is one rule: the packet can change the interface or what
 * a client script reads (a varp, an inventory, a stat, a chat line, a social,
 * clan or exchange store, a component, the chatbox's input). World feedback,
 * the camera, audio and the session do not. Every packet our server sends
 * rides a tick's flush closed by SERVER_TICK_END (torirs_server_session.c, "No
 * answer leaves from here"), and Jagex's transcripts group each of these
 * packets into its server tick, so a latch holds a frame across one burst.
 */
#define KIND(name_, latch_, flush_, wait_, waits_for_) [name_] = { 1, (latch_), (flush_), (wait_), (waits_for_) }
#define L_NO GAMEPROTO_LATCH_NONE
#define L_UI GAMEPROTO_LATCH_UI
#define L_VARP GAMEPROTO_LATCH_VARP
#define F_NO GAMEPROTO_FLUSH_NONE
#define F_MOUNT GAMEPROTO_FLUSH_MOUNT
#define F_COMP GAMEPROTO_FLUSH_COMPONENT
#define F_HOOKS GAMEPROTO_FLUSH_HOOKS
#define W_NO GAMEPROTO_WAIT_NONE
#define W_REQ GAMEPROTO_WAIT_REQUIRED
#define W_PAR GAMEPROTO_WAIT_PARALLEL
#define W_BOTH (GAMEPROTO_WAIT_REQUIRED | GAMEPROTO_WAIT_PARALLEL)

#define MOUNT_WAITS                                                                                \
    "the component pack, the bake, the onLoad scripts and transmit hooks (the mount task, on the " \
    "packet queue); the pack's models and kit models (parallel)"
#define PLAYERMODEL_WAITS                                                                          \
    "the idk and obj configs and the wear models (today a task on the packet queue; visual)"
#define VARP_WAITS                                                                                 \
    "a varp-retyped npc's configs, models and stances (today an NpcTransform task on the packet "  \
    "queue); a varp-transformed loc's re-place (parallel)"
#define SCRIPT_READS "the client scripts its transmit runs (render-blocking, not the packet queue)"

static struct GameProtoPacketKind const k_packet_kinds[PKT_NAME_COUNT] = {
    /* A control frame (net/torirs_control.h) has no name of its own. */
    KIND(PKT_NAME_NONE, L_NO, F_NO, W_NO, NULL),

    /* ---- interface mounts ---- */
    KIND(PKT_NAME_IF_OPENCHAT, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_OPENMAIN_SIDE, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_CLOSE, L_UI, F_MOUNT, W_REQ, "the close task of each slot it empties"),
    KIND(PKT_NAME_IF_SETTAB, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_OPENMAIN, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_OPENSIDE, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_OPENOVERLAY, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_OPENTOP, L_UI, F_MOUNT, W_BOTH,
        "the root's boot (the pump stops while it runs); its models (parallel)"),
    KIND(PKT_NAME_IF_OPENSUB, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_CLOSESUB, L_UI, F_MOUNT, W_REQ, "the slot's close task and its onSubChange hooks"),
    KIND(PKT_NAME_IF_MOVESUB, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_IF_RESYNC_V2, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    KIND(PKT_NAME_TUT_OPEN, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),
    /* It OPENS the welcome screen (exec_open_welcome_screen), the only thing
     * that ever does: a mount, so it latches and flushes as one. */
    KIND(PKT_NAME_LAST_LOGIN_INFO, L_UI, F_MOUNT, W_BOTH, MOUNT_WAITS),

    /* ---- one component's property ---- */
    KIND(PKT_NAME_IF_SETTEXT, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETHIDE, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETCOLOUR, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETEVENTS, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETPOSITION, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETSCROLLPOS, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETROTATESPEED, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETANGLE, L_UI, F_COMP, W_NO, NULL),
    /* Empties the item array of ONE old-format component (exec_if_clearinv_node):
     * a component write like IF_SETTEXT, so a script sent before it that fills
     * the same component keeps its wire order. */
    KIND(PKT_NAME_IF_CLEARINV, L_UI, F_COMP, W_NO, NULL),
    KIND(PKT_NAME_IF_SETMODEL, L_UI, F_COMP, W_PAR, "the model and its textures"),
    KIND(PKT_NAME_IF_SETOBJECT, L_UI, F_COMP, W_PAR, "the obj config and its model"),
    KIND(PKT_NAME_IF_SETANIM, L_UI, F_COMP, W_PAR, "the sequence and its frames"),
    KIND(PKT_NAME_IF_SETNPCHEAD, L_UI, F_COMP, W_PAR, "the npc config and its chathead models"),
    KIND(PKT_NAME_IF_SETNPCHEAD_ACTIVE, L_UI, F_COMP, W_PAR, "the npc config and its chathead models"),
    KIND(PKT_NAME_IF_SETPLAYERHEAD, L_UI, F_COMP, W_PAR, "the idk and obj configs and the head models"),
    KIND(PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR, L_UI, F_COMP, W_REQ, PLAYERMODEL_WAITS),
    KIND(PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE, L_UI, F_COMP, W_REQ, PLAYERMODEL_WAITS),
    KIND(PKT_NAME_IF_SETPLAYERMODEL_OBJ, L_UI, F_COMP, W_REQ, PLAYERMODEL_WAITS),
    KIND(PKT_NAME_IF_SETPLAYERMODEL_SELF, L_UI, F_COMP, W_REQ, PLAYERMODEL_WAITS),

    /* ---- component hooks ---- */
    /* Runs the root's onDialogAbort hooks (exec_trigger_on_dialog_abort). A
     * script the server sent before it runs first, as the reference's inline
     * RUNCLIENTSCRIPT does: held past it, a script that reopens or rewrites the
     * dialogue would run after the hooks that tear it down. */
    KIND(PKT_NAME_TRIGGER_ONDIALOGABORT, L_UI, F_HOOKS, W_NO, NULL),

    /* ---- the interface, not one component ---- */
    KIND(PKT_NAME_IF_SETTAB_ACTIVE, L_UI, F_NO, W_NO, NULL),
    /* Writes the same side_tab IF_SETTAB_ACTIVE writes, and the flash the tab
     * draws (rs_gameproto_exec.c, TUT_FLASH). */
    KIND(PKT_NAME_TUT_FLASH, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_RUNCLIENTSCRIPT, L_UI, F_NO, W_BOTH,
        "the script and every config it reads (on the packet queue when released); the models its "
        "widgets show (parallel)"),
    /* Opens the chatbox's count input (state->chat->dialog_input_open), which
     * the chatbox scripts read. */
    KIND(PKT_NAME_P_COUNTDIALOG, L_UI, F_NO, W_NO, NULL),
    /* The countdown the reboot-timer component reads (RS_SessionInfo). */
    KIND(PKT_NAME_UPDATE_REBOOT_TIMER, L_UI, F_NO, W_NO, NULL),
    /* The multiway icon the gameframe draws (RS_SessionInfo). */
    KIND(PKT_NAME_SET_MULTIWAY, L_UI, F_NO, W_NO, NULL),
    /* The minimap component's state (MinimapView_SetToggleState). */
    KIND(PKT_NAME_MINIMAP_TOGGLE, L_UI, F_NO, W_NO, NULL),

    /* ---- what client scripts read ---- */
    KIND(PKT_NAME_VARP_SMALL, L_VARP, F_NO, W_BOTH, VARP_WAITS),
    KIND(PKT_NAME_VARP_LARGE, L_VARP, F_NO, W_BOTH, VARP_WAITS),
    /* A varp's long value: the executor and the model treat it exactly as the
     * two above (rs_gameproto_exec.c groups the four), so it opens the tick as
     * a plain varp too. */
    KIND(PKT_NAME_VARP_LONG, L_VARP, F_NO, W_BOTH, VARP_WAITS),
    KIND(PKT_NAME_VARP_SYNC, L_UI, F_NO, W_BOTH, VARP_WAITS),
    KIND(PKT_NAME_VARP_RESET, L_UI, F_NO, W_BOTH, VARP_WAITS),
    KIND(PKT_NAME_UPDATE_INV_FULL, L_UI, F_NO, W_PAR, "the slots' icons; " SCRIPT_READS),
    KIND(PKT_NAME_UPDATE_INV_PARTIAL, L_UI, F_NO, W_PAR, "the slots' icons; " SCRIPT_READS),
    KIND(PKT_NAME_UPDATE_INV_STOP_TRANSMIT, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_STAT, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_RUNENERGY, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_RUNWEIGHT, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_IGNORELIST, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CHAT_FILTER_SETTINGS, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_FRIENDLIST, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_FRIENDLIST_LOADED, L_UI, F_NO, W_NO, NULL),
    /* A chat line: the chat store CHAT_GETHISTORY* reads, and the chat
     * transmit that reruns [proc,rebuildchatbox] -- the same class as the
     * friend lists above, so each line is part of its tick's interface. */
    KIND(PKT_NAME_MESSAGE_GAME, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MESSAGE_PRIVATE, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MESSAGE_PRIVATE_ECHO, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MESSAGE_FRIENDCHANNEL, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MESSAGE_CLANCHANNEL, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM, L_UI, F_NO, W_NO, NULL),
    /* The friend-chat, clan and exchange stores the clientscript commands read,
     * each followed by its transmit (exec_clan, the stock and offer stores). */
    KIND(PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_VARCLAN, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_VARCLAN_ENABLE, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_VARCLAN_DISABLE, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CLANCHANNEL_FULL, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CLANCHANNEL_DELTA, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CLANSETTINGS_FULL, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CLANSETTINGS_DELTA, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_STOCKMARKET_SLOT, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_TRADINGPOST, L_UI, F_NO, W_NO, NULL),
    /* The objunlock bitset the clientscript commands read (host.client). */
    KIND(PKT_NAME_OBJUNLOCK_RESET, L_UI, F_NO, W_NO, NULL),
    KIND(PKT_NAME_OBJUNLOCK_UPDATE, L_UI, F_NO, W_NO, NULL),
    /* The group storage commands that would read these are not implemented
     * (rs_gameproto_exec.c, GROUP_FULL): nothing reads them yet. */
    KIND(PKT_NAME_GROUP_FULL, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_GROUP_VAR, L_NO, F_NO, W_NO, NULL),

    /* ---- the fence ---- */
    KIND(PKT_NAME_SERVER_TICK_END, L_NO, F_NO, W_REQ,
        "the tick's held client scripts, released onto the packet queue behind it"),

    /* ---- scene and entities ---- */
    KIND(PKT_NAME_REBUILD_NORMAL, L_NO, F_NO, W_BOTH,
        "the map squares and loc models (awaited, or a background job that holds the pump); the "
        "locs' sequence frames (parallel)"),
    KIND(PKT_NAME_REBUILD_REGION, L_NO, F_NO, W_BOTH,
        "the map squares and loc models (awaited, or a background job that holds the pump); the "
        "locs' sequence frames (parallel)"),
    KIND(PKT_NAME_REBUILD_WORLDENTITY, L_NO, F_NO, W_REQ,
        "the deck's map squares, loc configs and models"),
    KIND(PKT_NAME_PLAYER_INFO, L_NO, F_NO, W_BOTH,
        "a new player's default kit models and seq 808 (today awaited; visual); appearance, "
        "sequence and transmog models (parallel)"),
    KIND(PKT_NAME_NPC_INFO, L_NO, F_NO, W_BOTH,
        "the npc configs and transform rungs a spawn or retype names; bodies and sequences "
        "(parallel)"),
    KIND(PKT_NAME_NPC_INFO_LARGE, L_NO, F_NO, W_BOTH,
        "the npc configs and transform rungs a spawn or retype names; bodies and sequences "
        "(parallel)"),
    KIND(PKT_NAME_WORLDENTITY_INFO, L_NO, F_NO, W_PAR, "the hulls' sequences"),
    KIND(PKT_NAME_CLEAR_ENTITIES, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_NPC_UPDATE_ORIGIN, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_ACTIVE_WORLD, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_PID, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_RESET_ANIMS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_ANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the sequence, its frames and wear models"),
    KIND(PKT_NAME_NPC_ANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the sequence and its frames"),
    KIND(PKT_NAME_NPC_SPOTANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_PLAYER_SPOTANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_NPC_HEADICON_SPECIFIC, L_NO, F_NO, W_NO, NULL),

    /* ---- zones ---- */
    KIND(PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, L_NO, F_NO, W_PAR, "the restored locs' configs and models"),
    KIND(PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED, L_NO, F_NO, W_PAR, "what each entry's own row hands on"),
    KIND(PKT_NAME_LOC_ADD_CHANGE, L_NO, F_NO, W_PAR,
        "the loc's config and models; scenery and collision change with them"),
    KIND(PKT_NAME_LOC_DEL, L_NO, F_NO, W_PAR, "nothing of its own; ordered behind its tile's change"),
    KIND(PKT_NAME_LOC_MERGE, L_NO, F_NO, W_PAR, "the restored loc's config and models"),
    KIND(PKT_NAME_LOC_ANIM, L_NO, F_NO, W_PAR, "the sequence and its frames"),
    KIND(PKT_NAME_LOC_ANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the sequence and its frames"),
    KIND(PKT_NAME_OBJ_ADD, L_NO, F_NO, W_PAR, "the stack's obj config and model"),
    KIND(PKT_NAME_OBJ_DEL, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_OBJ_COUNT, L_NO, F_NO, W_PAR, "the count variant's model"),
    KIND(PKT_NAME_OBJ_REVEAL, L_NO, F_NO, W_PAR, "the stack's obj config and model"),
    KIND(PKT_NAME_OBJ_ADD_SPECIFIC, L_NO, F_NO, W_PAR, "the stack's obj config and model"),
    KIND(PKT_NAME_OBJ_DEL_SPECIFIC, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_OBJ_COUNT_SPECIFIC, L_NO, F_NO, W_PAR, "the count variant's model"),
    KIND(PKT_NAME_OBJ_CUSTOMISE, L_NO, F_NO, W_PAR, "the customised model"),
    KIND(PKT_NAME_OBJ_UNCUSTOMISE, L_NO, F_NO, W_PAR, "the stack's model"),
    KIND(PKT_NAME_OBJ_CUSTOMISE_SPECIFIC, L_NO, F_NO, W_PAR, "the customised model"),
    KIND(PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC, L_NO, F_NO, W_PAR, "the stack's model"),
    KIND(PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_MAP_ANIM, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_MAP_ANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_MAP_PROJANIM, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_PROJANIM_SPECIFIC, L_NO, F_NO, W_PAR, "the graphic's config, model and frames"),
    KIND(PKT_NAME_SCRIPTEDPROJ_ADD, L_NO, F_NO, W_PAR,
        "the graphic's config, model and frames, and its curve scripts"),
    KIND(PKT_NAME_SCRIPTEDPROJ_CHANGE, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SOUND_AREA, L_NO, F_NO, W_PAR, "the sound clip"),

    /* ---- the world's menus and markers ---- */
    KIND(PKT_NAME_UNSET_MAP_FLAG, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_HINT_ARROW, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_HIDENPCOPS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_HIDELOCOPS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_HIDEOBJOPS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_PLAYER_OP, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_INTERACTION_MODE, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_RESET_INTERACTION_MODE, L_NO, F_NO, W_NO, NULL),

    /* ---- the camera ---- */
    KIND(PKT_NAME_CAM_LOOKAT, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_SHAKE, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_MOVETO, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_RESET, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_MOVETO_CYCLES, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_ROTATETO_COORDINATE, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_UNLOCK, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_TARGET, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_MOVETO_ARC, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_SMOOTHRESET, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_ROTATEBY, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_ROTATETO, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_LOOKAT_CYCLES, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_MODE, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_OCULUS_SYNC, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_CAM_SKYBOX, L_NO, F_NO, W_PAR, "the skybox model and its textures"),
    KIND(PKT_NAME_ZBUF, L_NO, F_NO, W_NO, NULL),

    /* ---- audio ---- */
    KIND(PKT_NAME_SYNTH_SOUND, L_NO, F_NO, W_PAR, "the sound clip"),
    KIND(PKT_NAME_MIDI_SONG, L_NO, F_NO, W_PAR, "the song, its patches and samples"),
    KIND(PKT_NAME_MIDI_JINGLE, L_NO, F_NO, W_PAR, "the jingle, its patches and samples"),
    KIND(PKT_NAME_MIDI_SONG_WITHSECONDARY, L_NO, F_NO, W_PAR, "the song, its patches and samples"),
    KIND(PKT_NAME_MIDI_SWAP, L_NO, F_NO, W_PAR, "the song, its patches and samples"),
    KIND(PKT_NAME_MIDI_SONG_STOP, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_AMBIENTSOUND_START, L_NO, F_NO, W_PAR, "the sound clip"),
    KIND(PKT_NAME_AMBIENTSOUND_STOP, L_NO, F_NO, W_NO, NULL),

    /* ---- the session ---- */
    /* A logout ends the session: no fence follows it, so it must not latch. */
    KIND(PKT_NAME_LOGOUT, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_LOGOUT_WITHREASON, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_LOGOUT_TRANSFER, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_FINISH_TRACKING, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_ENABLE_TRACKING, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SEND_PING, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UNKNOWN_STRING, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_REFLECTION_CHECKER, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_PACKET_GROUP_START, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SITE_SETTINGS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_URL_OPEN, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_ACCOUNT_FLAGS, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_HISCORE_REPLY, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_WEBSITE_TOKEN, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_UPDATE_UID192, L_NO, F_NO, W_NO, NULL),
    KIND(PKT_NAME_SET_HEATMAP_ENABLED, L_NO, F_NO, W_NO, NULL),
};

#undef KIND
#undef L_NO
#undef L_UI
#undef L_VARP
#undef F_NO
#undef F_MOUNT
#undef F_COMP
#undef F_HOOKS
#undef W_NO
#undef W_REQ
#undef W_PAR
#undef W_BOTH
#undef MOUNT_WAITS
#undef PLAYERMODEL_WAITS
#undef VARP_WAITS
#undef SCRIPT_READS

struct GameProtoPacketKind const*
gameproto_packet_kind(enum GameProtoPktName packet_type)
{
    assert(packet_type >= PKT_NAME_NONE);
    assert(packet_type < PKT_NAME_COUNT);
    /* A name with no row is a packet nobody stated the latch or the flush of:
     * test-packet-kinds fails first; this is the run-time backstop. */
    assert(k_packet_kinds[packet_type].stated);
    return &k_packet_kinds[packet_type];
}

int
gameproto_packet_latches(enum GameProtoPktName packet_type)
{
    return gameproto_packet_kind(packet_type)->latch != GAMEPROTO_LATCH_NONE;
}

int
gameproto_packet_latches_varp_only(enum GameProtoPktName packet_type)
{
    return gameproto_packet_kind(packet_type)->latch == GAMEPROTO_LATCH_VARP;
}

int
gameproto_packet_flushes_held_scripts(enum GameProtoPktName packet_type)
{
    return gameproto_packet_kind(packet_type)->flush != GAMEPROTO_FLUSH_NONE;
}

/*
 * Every parser's heap payload is ToriMem_Net()'s, except four still on libc
 * because code outside src/net frees them too: _map_rebuild.zones and
 * _update_zone_enclosed.entries (rs_gameproto_exec_test frees them directly),
 * and the chat text of MESSAGE_PRIVATE / MESSAGE_CLANCHANNEL (the revision's
 * chat_unpack; its other callers free the result with ToriMem_Free(ToriMem_Net(), )).
 */
void
gameproto_free(struct RevPacket* p)
{
    if( !p )
        return;

    if( p->_control )
    {
        ToriMem_Free(ToriMem_Net(), p->_control->payload);
        ToriMem_Free(ToriMem_Net(), p->_control);
        p->_control = NULL;
    }

    switch( p->packet_type )
    {
    case PKT_NAME_RUNCLIENTSCRIPT:
        /* The one payload held outside the union; see struct RevPacket. */
        ToriMem_Free(ToriMem_Net(), p->_runclientscript);
        p->_runclientscript = NULL;
        break;
    case PKT_NAME_NPC_INFO:
    case PKT_NAME_NPC_INFO_LARGE:
        ToriMem_Free(ToriMem_Net(), p->_npc_info.data);
        p->_npc_info.data = NULL;
        p->_npc_info.length = 0;
        break;
    case PKT_NAME_PLAYER_INFO:
        ToriMem_Free(ToriMem_Net(), p->_player_info.data);
        p->_player_info.data = NULL;
        p->_player_info.length = 0;
        break;
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
        /* dat2/osrs230 XTEA keys (NULL on the lc254 path), and the instanced
         * descriptor grid (NULL for REBUILD_NORMAL). */
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.region_ids);
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.region_keys);
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.zones);
        p->_map_rebuild.region_ids = NULL;
        p->_map_rebuild.region_keys = NULL;
        p->_map_rebuild.zones = NULL;
        p->_map_rebuild.region_count = 0;
        break;
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL:
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER:
    case PKT_NAME_VARCLAN:
    case PKT_NAME_VARCLAN_ENABLE:
    case PKT_NAME_VARCLAN_DISABLE:
    case PKT_NAME_CLANCHANNEL_FULL:
    case PKT_NAME_CLANCHANNEL_DELTA:
    case PKT_NAME_CLANSETTINGS_FULL:
    case PKT_NAME_CLANSETTINGS_DELTA:
    case PKT_NAME_GROUP_FULL:
        ToriMem_Free(ToriMem_Net(), p->_raw_payload.data);
        p->_raw_payload.data = NULL;
        p->_raw_payload.length = 0;
        break;
    case PKT_NAME_UPDATE_TRADINGPOST:
        ToriMem_Free(ToriMem_Net(), p->_update_tradingpost.offers);
        p->_update_tradingpost.offers = NULL;
        p->_update_tradingpost.count = 0;
        break;
    case PKT_NAME_WORLDENTITY_INFO:
        ToriMem_Free(ToriMem_Net(), p->_worldentity_info.updates);
        ToriMem_Free(ToriMem_Net(), p->_worldentity_info.spawns);
        p->_worldentity_info.updates = NULL;
        p->_worldentity_info.spawns = NULL;
        p->_worldentity_info.count = 0;
        p->_worldentity_info.spawn_count = 0;
        break;
    case PKT_NAME_UNKNOWN_STRING:
    case PKT_NAME_SITE_SETTINGS:
    case PKT_NAME_URL_OPEN:
        ToriMem_Free(ToriMem_Net(), p->_text.text);
        p->_text.text = NULL;
        break;
    case PKT_NAME_LOGOUT_TRANSFER:
        ToriMem_Free(ToriMem_Net(), p->_logout_transfer.host);
        p->_logout_transfer.host = NULL;
        break;
    case PKT_NAME_MESSAGE_CLANCHANNEL:
    case PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM:
        ToriMem_Free(ToriMem_Net(), p->_message_clanchannel.text);
        p->_message_clanchannel.text = NULL;
        break;
    case PKT_NAME_OBJUNLOCK_UPDATE:
        ToriMem_Free(ToriMem_Net(), p->_objunlock_update.entries);
        p->_objunlock_update.entries = NULL;
        p->_objunlock_update.count = 0;
        break;
    case PKT_NAME_GROUP_VAR:
        for( int i = 0; i < p->_group_var.count; i++ )
            ToriMem_Free(ToriMem_Net(), p->_group_var.updates[i].string_value);
        ToriMem_Free(ToriMem_Net(), p->_group_var.updates);
        p->_group_var.updates = NULL;
        p->_group_var.count = 0;
        break;
    case PKT_NAME_SET_WEBSITE_TOKEN:
        ToriMem_Free(ToriMem_Net(), p->_text.text);
        p->_text.text = NULL;
        break;
    case PKT_NAME_MESSAGE_PRIVATE_ECHO:
        ToriMem_Free(ToriMem_Net(), p->_message_private_echo.text);
        p->_message_private_echo.text = NULL;
        break;
    case PKT_NAME_MESSAGE_FRIENDCHANNEL:
        ToriMem_Free(ToriMem_Net(), p->_message_friendchannel.text);
        p->_message_friendchannel.text = NULL;
        break;
    case PKT_NAME_REBUILD_WORLDENTITY:
        ToriMem_Free(ToriMem_Net(), p->_rebuild_wev.data);
        p->_rebuild_wev.data = NULL;
        p->_rebuild_wev.length = 0;
        break;
    case PKT_NAME_UPDATE_INV_FULL:
        ToriMem_Free(ToriMem_Net(), p->_update_inv_full.obj_ids);
        ToriMem_Free(ToriMem_Net(), p->_update_inv_full.obj_counts);
        p->_update_inv_full.obj_ids = NULL;
        p->_update_inv_full.obj_counts = NULL;
        p->_update_inv_full.size = 0;
        break;
    case PKT_NAME_IF_RESYNC_V2:
        ToriMem_Free(ToriMem_Net(), p->_if_resync.mounts);
        ToriMem_Free(ToriMem_Net(), p->_if_resync.events);
        p->_if_resync.mounts = NULL;
        p->_if_resync.events = NULL;
        p->_if_resync.mount_count = 0;
        p->_if_resync.event_count = 0;
        break;
    case PKT_NAME_IF_SETTEXT:
        ToriMem_Free(ToriMem_Net(), p->_if_settext.text);
        p->_if_settext.text = NULL;
        break;
    case PKT_NAME_MESSAGE_GAME:
        ToriMem_Free(ToriMem_Net(), p->_message_game.name);
        ToriMem_Free(ToriMem_Net(), p->_message_game.text);
        p->_message_game.name = NULL;
        p->_message_game.text = NULL;
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        ToriMem_Free(ToriMem_Net(), p->_message_private.text);
        p->_message_private.text = NULL;
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        ToriMem_Free(ToriMem_Net(), p->_update_inv_partial.entries);
        p->_update_inv_partial.entries = NULL;
        p->_update_inv_partial.count = 0;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        ToriMem_Free(ToriMem_Net(), p->_update_zone_enclosed.entries);
        p->_update_zone_enclosed.entries = NULL;
        p->_update_zone_enclosed.count = 0;
        break;
    case PKT_NAME_UPDATE_IGNORELIST:
        ToriMem_Free(ToriMem_Net(), p->_update_ignorelist.names37);
        p->_update_ignorelist.names37 = NULL;
        p->_update_ignorelist.count = 0;
        break;
    case PKT_NAME_SET_PLAYER_OP:
        ToriMem_Free(ToriMem_Net(), p->_set_player_op.text);
        p->_set_player_op.text = NULL;
        break;
    default:
        break;
    }
}
