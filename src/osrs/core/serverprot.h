#ifndef OSRS_CORE_SERVERPROT_H
#define OSRS_CORE_SERVERPROT_H

/* Stable versioned packet type identifiers, shared between C and Lua.
 *
 * The protocol itself has no single version.  Each packet type carries its
 * own version suffix (_V1, _V2, …).  A future revision that changes the wire
 * layout of UPDATE_STAT would add SERVERPROT_UPDATE_STAT_V2 while leaving
 * every other entry untouched.
 *
 * Values are sequential and opcode-independent.  When the parse layer
 * decodes a revision-specific wire opcode it tags the resulting RevServerProtPacket
 * with the matching SERVERPROT_*_V1 constant.  Lua receives this value from
 * pop_next_packet and dispatches on it without ever knowing the wire opcode.
 *
 * New entries must be appended at the end; existing values must not change
 * so that the Lua packet_types table stays in sync.
 */

enum ServerProt
{
    SERVERPROT_REBUILD_NORMAL_V1               =  1,
    SERVERPROT_NPC_INFO_V1                     =  2,
    SERVERPROT_PLAYER_INFO_V1                  =  3,
    SERVERPROT_UPDATE_INV_FULL_V1              =  4,
    SERVERPROT_UPDATE_INV_PARTIAL_V1           =  5,
    SERVERPROT_UPDATE_INV_STOP_TRANSMIT_V1     =  6,
    SERVERPROT_IF_SETTAB_V1                    =  7,
    SERVERPROT_IF_SETTAB_ACTIVE_V1             =  8,
    SERVERPROT_IF_SETCOLOUR_V1                 =  9,
    SERVERPROT_IF_SETHIDE_V1                   = 10,
    SERVERPROT_IF_SETOBJECT_V1                 = 11,
    SERVERPROT_IF_SETMODEL_V1                  = 12,
    SERVERPROT_IF_SETANIM_V1                   = 13,
    SERVERPROT_IF_SETPLAYERHEAD_V1             = 14,
    SERVERPROT_IF_SETTEXT_V1                   = 15,
    SERVERPROT_IF_SETNPCHEAD_V1                = 16,
    SERVERPROT_IF_SETPOSITION_V1               = 17,
    SERVERPROT_IF_SETSCROLLPOS_V1              = 18,
    SERVERPROT_IF_OPENCHAT_V1                  = 19,
    SERVERPROT_IF_OPENMAIN_V1                  = 20,
    SERVERPROT_IF_OPENSIDE_V1                  = 21,
    SERVERPROT_IF_OPENMAIN_SIDE_V1             = 22,
    SERVERPROT_IF_CLOSE_V1                     = 23,
    SERVERPROT_CAM_LOOKAT_V1                   = 24,
    SERVERPROT_CAM_MOVETO_V1                   = 25,
    SERVERPROT_CAM_SHAKE_V1                    = 26,
    SERVERPROT_CAM_RESET_V1                    = 27,
    SERVERPROT_VARP_SMALL_V1                   = 28,
    SERVERPROT_VARP_LARGE_V1                   = 29,
    SERVERPROT_VARP_SYNC_V1                    = 30,
    /* Zone LOC packets (and zone OBJ): wire layout has no terrain plane. The reference client
     * applies them on `minusedlevel` from the local PLAYER_INFO block (teleport-style update
     * carries 2-bit level). Here PLAYER_INFO_V1 updates GGame.local_player_plane; LOC_* handlers
     * use that when resolving MapBuildLocEntity / spawns. */
    SERVERPROT_UPDATE_ZONE_PARTIAL_FOLLOWS_V1  = 31,
    SERVERPROT_UPDATE_ZONE_FULL_FOLLOWS_V1     = 32,
    SERVERPROT_UPDATE_ZONE_PARTIAL_ENCLOSED_V1 = 33,
    SERVERPROT_LOC_ADD_CHANGE_V1               = 34,
    SERVERPROT_LOC_DEL_V1                      = 35,
    SERVERPROT_LOC_ANIM_V1                     = 36,
    SERVERPROT_LOC_MERGE_V1                    = 37,
    SERVERPROT_OBJ_ADD_V1                      = 38,
    SERVERPROT_OBJ_DEL_V1                      = 39,
    SERVERPROT_OBJ_REVEAL_V1                   = 40,
    SERVERPROT_OBJ_COUNT_V1                    = 41,
    SERVERPROT_MAP_ANIM_V1                     = 42,
    SERVERPROT_MAP_PROJANIM_V1                 = 43,
    SERVERPROT_UNSET_MAP_FLAG_V1               = 44,
    SERVERPROT_UPDATE_RUNWEIGHT_V1             = 45,
    SERVERPROT_UPDATE_RUNENERGY_V1             = 46,
    SERVERPROT_UPDATE_STAT_V1                  = 47,
    SERVERPROT_HINT_ARROW_V1                   = 48,
    SERVERPROT_RESET_ANIMS_V1                  = 49,
    SERVERPROT_UPDATE_PID_V1                   = 50,
    SERVERPROT_SET_MULTIWAY_V1                 = 51,
    SERVERPROT_SET_PLAYER_OP_V1                = 52,
    SERVERPROT_MESSAGE_GAME_V1                 = 53,
    SERVERPROT_MESSAGE_PRIVATE_V1              = 54,
    SERVERPROT_CHAT_FILTER_SETTINGS_V1         = 55,
    SERVERPROT_TUT_FLASH_V1                    = 56,
    SERVERPROT_TUT_OPEN_V1                     = 57,
    SERVERPROT_LOGOUT_V1                       = 58,
    SERVERPROT_P_COUNTDIALOG_V1                = 59,
    SERVERPROT_UPDATE_REBOOT_TIMER_V1          = 60,
    SERVERPROT_UPDATE_FRIENDLIST_V1            = 61,
    SERVERPROT_UPDATE_IGNORELIST_V1            = 62,
    SERVERPROT_SYNTH_SOUND_V1                  = 63,
    SERVERPROT_MIDI_SONG_V1                    = 64,
    SERVERPROT_MIDI_JINGLE_V1                  = 65,
    SERVERPROT_FINISH_TRACKING_V1              = 66,
    SERVERPROT_ENABLE_TRACKING_V1              = 67,
    SERVERPROT_LAST_LOGIN_INFO_V1              = 68,
};

#endif
