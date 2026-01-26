#ifndef PACKETIN_H
#define PACKETIN_H

#include "rscache/rsbuf.h"

#include <assert.h>
#include <stdint.h>

#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)

// From thedaneeffect

// > Names based on https://www.rune-server.ee/runescape-development/
// > rs-503-client-and-server/informative-threads/
// > 622541-great-nxt-beta-dump-thread.html
enum PacketInType_Dane317
{
    PKTIN_CAM_LOOKAT = 177,
    PKTIN_CAM_RESET = 107,
    PKTIN_CAM_SETPOS = 166,
    PKTIN_CAM_SHAKE = 35,
    PKTIN_IF_CHAT = 164,
    PKTIN_IF_CHAT_STICKY = 218,
    PKTIN_CHAT_FILTER_SETTINGS = 206,
    PKTIN_CLEAR_MAP_FLAG = 78,
    PKTIN_FRIENDLIST_LOADED = 221,
    PKTIN_FRIEND_STATUS = 50,
    PKTIN_HINT_ARROW = 254,
    PKTIN_IF_CLOSE = 219,
    PKTIN_IF_SETANGLE = 230,
    PKTIN_IF_SETANIM = 200,
    PKTIN_IF_SETCOLOR = 122,
    PKTIN_IF_SETHIDE = 171,
    PKTIN_IF_SETMODEL = 8,
    PKTIN_IF_SETNPCHEAD = 75,
    PKTIN_IF_SETOBJECT = 246,
    PKTIN_IF_SETPLAYERHEAD = 185,
    PKTIN_IF_SETPOSITION = 70,
    PKTIN_IF_SETSCROLLPOS = 79,
    PKTIN_IF_SETTEXT = 126,
    PKTIN_IF_STOPANIM = 142,
    PKTIN_IF_VIEWPORT = 97,
    PKTIN_IF_VIEWPORT_AND_SIDEBAR = 248,
    PKTIN_IF_VIEWPORT_OVERLAY = 208,
    PKTIN_IGNORE_LIST = 214,
    PKTIN_INPUT_AMOUNT = 27,
    PKTIN_INPUT_NAME = 187,
    PKTIN_INV_CLEAR = 72,
    PKTIN_LAST_LOGIN_INFO = 176,
    PKTIN_LOCAL_PLAYER = 249,
    PKTIN_LOC_ADD = 151,
    PKTIN_LOC_CHANGE = 160,
    PKTIN_LOC_DEL = 101,
    PKTIN_LOGOUT = 109,
    PKTIN_MESSAGE_GAME = 253,
    PKTIN_MESSAGE_PUBLIC = 196,
    PKTIN_MIDI_JINGLE = 121,
    PKTIN_MIDI_SONG = 74,
    PKTIN_MINIMAP_TOGGLE = 99,
    PKTIN_MULTIZONE = 61,
    PKTIN_OBJ_ADD = 44,
    PKTIN_OBJ_COUNT = 84,
    PKTIN_OBJ_DEL = 156,
    PKTIN_OBJ_REVEAL = 215,
    PKTIN_LOC_PLAYER = 147,
    PKTIN_MAP_PROJECTILE = 117,
    PKTIN_REBUILD_REGION = 73,
    PKTIN_REBUILD_REGION_INSTANCE = 241,
    PKTIN_RESET_ANIMS = 1,
    PKTIN_RESET_CLIENT_VARCACHE = 68,
    PKTIN_SET_PLAYER_OP = 104,
    PKTIN_MAP_SOUND = 105,
    PKTIN_MAP_ANIM = 4,
    PKTIN_SYNC_NPCS = 65,
    PKTIN_SYNC_PLAYERS = 81,
    PKTIN_SYNTH_SOUND = 174,
    PKTIN_IF_TAB = 71,
    PKTIN_TAB_HINT = 24,
    PKTIN_TAB_SELECTED = 106,
    PKTIN_UPDATE_INV_FULL = 53,
    PKTIN_UPDATE_INV_PARTIAL = 34,
    PKTIN_UPDATE_REBOOT_TIMER = 114,
    PKTIN_UPDATE_RUNENERGY = 110,
    PKTIN_UPDATE_RUNWEIGHT = 240,
    PKTIN_UPDATE_STAT = 134,
    PKTIN_VARP_LARGE = 87,
    PKTIN_VARP_SMALL = 36,
    PKTIN_ZONE_BASE = 85,
    PKTIN_ZONE_CLEAR = 64,
    PKTIN_ZONE_UPDATE = 60,
};

struct PacketInDefinition
{
    int length;
};

static const struct PacketInDefinition g_packet_in_definitions_dane317[256] = {
    [PKTIN_MAP_ANIM] = { .length = 6 },
    [PKTIN_IF_SETMODEL] = { .length = 4 },
    [PKTIN_TAB_HINT] = { .length = 1 },
    [PKTIN_UPDATE_INV_PARTIAL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_CAM_SHAKE] = { .length = 4 },
    [PKTIN_VARP_SMALL] = { .length = 3 },
    [PKTIN_OBJ_ADD] = { .length = 5 },
    [47] = { .length = 6 },
    [PKTIN_FRIEND_STATUS] = { .length = 9 },
    [PKTIN_UPDATE_INV_FULL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_ZONE_UPDATE] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_MULTIZONE] = { .length = 1 },
    [PKTIN_ZONE_CLEAR] = { .length = 2 },
    [PKTIN_SYNC_NPCS] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_IF_SETPOSITION] = { .length = 6 },
    [PKTIN_IF_TAB] = { .length = 3 },
    [PKTIN_INV_CLEAR] = { .length = 2 },
    [PKTIN_REBUILD_REGION] = { .length = 4 },
    [PKTIN_MIDI_SONG] = { .length = 2 },
    [PKTIN_IF_SETNPCHEAD] = { .length = 4 },
    [PKTIN_IF_SETSCROLLPOS] = { .length = 4 },
    [PKTIN_SYNC_PLAYERS] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_OBJ_COUNT] = { .length = 7 },
    [PKTIN_ZONE_BASE] = { .length = 2 },
    [PKTIN_VARP_LARGE] = { .length = 6 },
    [PKTIN_IF_VIEWPORT] = { .length = 2 },
    [PKTIN_MINIMAP_TOGGLE] = { .length = 1 },
    [PKTIN_LOC_DEL] = { .length = 2 },
    [PKTIN_SET_PLAYER_OP] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_MAP_SOUND] = { .length = 4 },
    [PKTIN_TAB_SELECTED] = { .length = 1 },
    [PKTIN_UPDATE_RUNENERGY] = { .length = 1 },
    [PKTIN_UPDATE_REBOOT_TIMER] = { .length = 2 },
    [PKTIN_MAP_PROJECTILE] = { .length = 15 },
    [PKTIN_MIDI_JINGLE] = { .length = 4 },
    [PKTIN_IF_SETCOLOR] = { .length = 4 },
    [PKTIN_IF_SETTEXT] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_UPDATE_STAT] = { .length = 6 },
    [PKTIN_IF_STOPANIM] = { .length = 2 },
    [PKTIN_LOC_PLAYER] = { .length = 14 },
    [PKTIN_LOC_ADD] = { .length = 4 },
    [PKTIN_OBJ_DEL] = { .length = 3 },
    [PKTIN_LOC_CHANGE] = { .length = 4 },
    [PKTIN_IF_CHAT] = { .length = 2 },
    [PKTIN_CAM_SETPOS] = { .length = 6 },
    [PKTIN_IF_SETHIDE] = { .length = 3 },
    [PKTIN_SYNTH_SOUND] = { .length = 5 },
    [PKTIN_LAST_LOGIN_INFO] = { .length = 10 },
    [PKTIN_CAM_LOOKAT] = { .length = 6 },
    [PKTIN_IF_SETPLAYERHEAD] = { .length = 2 },
    [PKTIN_MESSAGE_PUBLIC] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_IF_SETANIM] = { .length = 4 },
    [PKTIN_CHAT_FILTER_SETTINGS] = { .length = 3 },
    [PKTIN_IF_VIEWPORT_OVERLAY] = { .length = 2 },
    [PKTIN_IGNORE_LIST] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_OBJ_REVEAL] = { .length = 7 },
    [PKTIN_IF_CHAT_STICKY] = { .length = 2 },
    [PKTIN_FRIENDLIST_LOADED] = { .length = 1 },
    [PKTIN_IF_SETANGLE] = { .length = 8 },
    [PKTIN_UPDATE_RUNWEIGHT] = { .length = 2 },
    [PKTIN_REBUILD_REGION_INSTANCE] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_IF_SETOBJECT] = { .length = 6 },
    [PKTIN_IF_VIEWPORT_AND_SIDEBAR] = { .length = 4 },
    [PKTIN_LOCAL_PLAYER] = { .length = 3 },
    [PKTIN_MESSAGE_GAME] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_HINT_ARROW] = { .length = 6 }
};

static inline int
packetin_size_dane317(int packet_type)
{
    assert(packet_type >= 0 && packet_type < 256);
    return g_packet_in_definitions_dane317[packet_type].length;
}

enum PacketInType_LC254
{
    // interfaces
    PKTIN_LC254_IF_OPENCHAT = 141,
    PKTIN_LC254_IF_OPENMAIN_SIDE = 249,
    PKTIN_LC254_IF_CLOSE = 174,
    PKTIN_LC254_IF_SETTAB = 91,
    PKTIN_LC254_IF_OPENMAIN = 197,
    PKTIN_LC254_IF_OPENSIDE = 187,
    PKTIN_LC254_IF_SETTAB_ACTIVE = 138,
    PKTIN_LC254_IF_OPENOVERLAY = 85,

    // updating interfaces
    PKTIN_LC254_IF_SETCOLOUR = 38,
    PKTIN_LC254_IF_SETHIDE = 227,
    PKTIN_LC254_IF_SETOBJECT = 222,
    PKTIN_LC254_IF_SETMODEL = 211,
    PKTIN_LC254_IF_SETANIM = 95,
    PKTIN_LC254_IF_SETPLAYERHEAD = 161,
    PKTIN_LC254_IF_SETTEXT = 41,
    PKTIN_LC254_IF_SETNPCHEAD = 3,
    PKTIN_LC254_IF_SETPOSITION = 27,
    PKTIN_LC254_IF_SETSCROLLPOS = 14,

    // tutorial area
    PKTIN_LC254_TUT_FLASH = 58,
    PKTIN_LC254_TUT_OPEN = 239,

    // inventory
    PKTIN_LC254_UPDATE_INV_STOP_TRANSMIT = 168,
    PKTIN_LC254_UPDATE_INV_FULL = 28,
    PKTIN_LC254_UPDATE_INV_PARTIAL = 170,

    // camera control
    PKTIN_LC254_CAM_LOOKAT = 0,
    PKTIN_LC254_CAM_SHAKE = 225,
    PKTIN_LC254_CAM_MOVETO = 55,
    PKTIN_LC254_CAM_RESET = 167,

    // entity updates
    PKTIN_LC254_NPC_INFO = 123,
    PKTIN_LC254_PLAYER_INFO = 87,

    // input tracking
    PKTIN_LC254_FINISH_TRACKING = 29,
    PKTIN_LC254_ENABLE_TRACKING = 251,

    // social
    PKTIN_LC254_MESSAGE_GAME = 73,
    PKTIN_LC254_UPDATE_IGNORELIST = 63,
    PKTIN_LC254_CHAT_FILTER_SETTINGS = 24,
    PKTIN_LC254_MESSAGE_PRIVATE = 60,
    PKTIN_LC254_UPDATE_FRIENDLIST = 111,
    PKTIN_LC254_FRIENDLIST_LOADED = 255,

    // misc
    PKTIN_LC254_UNSET_MAP_FLAG = 108,
    PKTIN_LC254_UPDATE_RUNWEIGHT = 164,
    PKTIN_LC254_HINT_ARROW = 64,
    PKTIN_LC254_UPDATE_REBOOT_TIMER = 143,
    PKTIN_LC254_UPDATE_STAT = 136,
    PKTIN_LC254_UPDATE_RUNENERGY = 94,
    PKTIN_LC254_RESET_ANIMS = 203,
    PKTIN_LC254_UPDATE_PID = 213,
    PKTIN_LC254_LAST_LOGIN_INFO = 146,
    PKTIN_LC254_LOGOUT = 21,
    PKTIN_LC254_P_COUNTDIALOG = 5,
    PKTIN_LC254_SET_MULTIWAY = 75,
    PKTIN_LC254_SET_PLAYER_OP = 204,

    // maps
    PKTIN_LC254_REBUILD_NORMAL = 209,

    // vars
    PKTIN_LC254_VARP_SMALL = 186,
    PKTIN_LC254_VARP_LARGE = 196,
    PKTIN_LC254_VARP_SYNC = 140,

    // audio
    PKTIN_LC254_SYNTH_SOUND = 25,
    PKTIN_LC254_MIDI_SONG = 163,
    PKTIN_LC254_MIDI_JINGLE = 242,

    // zones
    PKTIN_LC254_UPDATE_ZONE_PARTIAL_FOLLOWS = 173,
    PKTIN_LC254_UPDATE_ZONE_FULL_FOLLOWS = 159,
    PKTIN_LC254_UPDATE_ZONE_PARTIAL_ENCLOSED = 61,

    // zone protocol
    PKTIN_LC254_P_LOCMERGE = 218,
    PKTIN_LC254_LOC_ANIM = 30,
    PKTIN_LC254_OBJ_DEL = 115,
    PKTIN_LC254_OBJ_REVEAL = 8,
    PKTIN_LC254_LOC_ADD_CHANGE = 70,
    PKTIN_LC254_MAP_PROJANIM = 37,
    PKTIN_LC254_LOC_DEL = 88,
    PKTIN_LC254_OBJ_COUNT = 98,
    PKTIN_LC254_MAP_ANIM = 114,
    PKTIN_LC254_OBJ_ADD = 120
};

static const struct PacketInDefinition g_packet_in_definitions_lc254[256] = {
    [PKTIN_LC254_IF_OPENCHAT] = { .length = 2 },
    [PKTIN_LC254_IF_OPENMAIN_SIDE] = { .length = 4 },
    [PKTIN_LC254_IF_CLOSE] = { .length = 0 },
    [PKTIN_LC254_IF_SETTAB] = { .length = 3 },
    [PKTIN_LC254_IF_OPENMAIN] = { .length = 2 },
    [PKTIN_LC254_IF_OPENSIDE] = { .length = 2 },
    [PKTIN_LC254_IF_SETTAB_ACTIVE] = { .length = 1 },
    [PKTIN_LC254_IF_OPENOVERLAY] = { .length = 2 },
    [PKTIN_LC254_IF_SETCOLOUR] = { .length = 4 },
    [PKTIN_LC254_IF_SETHIDE] = { .length = 3 },
    [PKTIN_LC254_IF_SETOBJECT] = { .length = 6 },
    [PKTIN_LC254_IF_SETMODEL] = { .length = 4 },
    [PKTIN_LC254_IF_SETANIM] = { .length = 4 },
    [PKTIN_LC254_IF_SETPLAYERHEAD] = { .length = 2 },
    [PKTIN_LC254_IF_SETTEXT] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_IF_SETNPCHEAD] = { .length = 4 },
    [PKTIN_LC254_IF_SETPOSITION] = { .length = 6 },
    [PKTIN_LC254_IF_SETSCROLLPOS] = { .length = 4 },
    [PKTIN_LC254_TUT_FLASH] = { .length = 1 },
    [PKTIN_LC254_TUT_OPEN] = { .length = 2 },
    [PKTIN_LC254_UPDATE_INV_STOP_TRANSMIT] = { .length = 2 },
    [PKTIN_LC254_UPDATE_INV_FULL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_UPDATE_INV_PARTIAL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_CAM_LOOKAT] = { .length = 6 },
    [PKTIN_LC254_CAM_SHAKE] = { .length = 4 },
    [PKTIN_LC254_CAM_MOVETO] = { .length = 6 },
    [PKTIN_LC254_CAM_RESET] = { .length = 0 },
    [PKTIN_LC254_NPC_INFO] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_PLAYER_INFO] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_FINISH_TRACKING] = { .length = 0 },
    [PKTIN_LC254_ENABLE_TRACKING] = { .length = 0 },
    [PKTIN_LC254_MESSAGE_GAME] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_LC254_UPDATE_IGNORELIST] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_CHAT_FILTER_SETTINGS] = { .length = 3 },
    [PKTIN_LC254_MESSAGE_PRIVATE] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_LC254_UPDATE_FRIENDLIST] = { .length = 9 },
    [PKTIN_LC254_FRIENDLIST_LOADED] = { .length = 1 },
    [PKTIN_LC254_UNSET_MAP_FLAG] = { .length = 0 },
    [PKTIN_LC254_UPDATE_RUNWEIGHT] = { .length = 2 },
    [PKTIN_LC254_HINT_ARROW] = { .length = 6 },
    [PKTIN_LC254_UPDATE_REBOOT_TIMER] = { .length = 2 },
    [PKTIN_LC254_UPDATE_STAT] = { .length = 6 },
    [PKTIN_LC254_UPDATE_RUNENERGY] = { .length = 1 },
    [PKTIN_LC254_RESET_ANIMS] = { .length = 0 },
    [PKTIN_LC254_UPDATE_PID] = { .length = 3 },
    [PKTIN_LC254_LAST_LOGIN_INFO] = { .length = 10 },
    [PKTIN_LC254_LOGOUT] = { .length = 0 },
    [PKTIN_LC254_P_COUNTDIALOG] = { .length = 0 },
    [PKTIN_LC254_SET_MULTIWAY] = { .length = 1 },
    [PKTIN_LC254_SET_PLAYER_OP] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_LC254_REBUILD_NORMAL] = { .length = 4 },
    [PKTIN_LC254_VARP_SMALL] = { .length = 3 },
    [PKTIN_LC254_VARP_LARGE] = { .length = 6 },
    [PKTIN_LC254_VARP_SYNC] = { .length = 0 },
    [PKTIN_LC254_SYNTH_SOUND] = { .length = 5 },
    [PKTIN_LC254_MIDI_SONG] = { .length = 2 },
    [PKTIN_LC254_MIDI_JINGLE] = { .length = 4 },
    [PKTIN_LC254_UPDATE_ZONE_PARTIAL_FOLLOWS] = { .length = 2 },
    [PKTIN_LC254_UPDATE_ZONE_FULL_FOLLOWS] = { .length = 2 },
    [PKTIN_LC254_UPDATE_ZONE_PARTIAL_ENCLOSED] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC254_P_LOCMERGE] = { .length = 14 },
    [PKTIN_LC254_LOC_ANIM] = { .length = 4 },
    [PKTIN_LC254_OBJ_DEL] = { .length = 3 },
    [PKTIN_LC254_OBJ_REVEAL] = { .length = 7 },
    [PKTIN_LC254_LOC_ADD_CHANGE] = { .length = 4 },
    [PKTIN_LC254_MAP_PROJANIM] = { .length = 15 },
    [PKTIN_LC254_LOC_DEL] = { .length = 2 },
    [PKTIN_LC254_OBJ_COUNT] = { .length = 7 },
    [PKTIN_LC254_MAP_ANIM] = { .length = 6 },
    [PKTIN_LC254_OBJ_ADD] = { .length = 5 },
};

static inline int
packetin_size_lc254(int packet_type)
{
    assert(packet_type >= 0 && packet_type < 256);
    return g_packet_in_definitions_lc254[packet_type].length;
}

enum PacketInType_LC245_2
{
    PKTIN_LC245_2_IF_OPENCHAT = 7,
    PKTIN_LC245_2_IF_OPENMAIN_SIDE = 229,
    PKTIN_LC245_2_IF_CLOSE = 174,
    PKTIN_LC245_2_IF_SETTAB = 29,
    PKTIN_LC245_2_IF_OPENMAIN = 177,
    PKTIN_LC245_2_IF_OPENSIDE = 236,
    PKTIN_LC245_2_IF_SETTAB_ACTIVE = 8,

    // updating interfaces
    PKTIN_LC245_2_IF_SETCOLOUR = 135,
    PKTIN_LC245_2_IF_SETHIDE = 225,
    PKTIN_LC245_2_IF_SETOBJECT = 153,
    PKTIN_LC245_2_IF_SETMODEL = 60,
    PKTIN_LC245_2_IF_SETANIM = 69,
    PKTIN_LC245_2_IF_SETPLAYERHEAD = 83,
    PKTIN_LC245_2_IF_SETTEXT = 32,
    PKTIN_LC245_2_IF_SETNPCHEAD = 76,
    PKTIN_LC245_2_IF_SETPOSITION = 230,
    PKTIN_LC245_2_IF_SETSCROLLPOS = 226,

    // tutorial area
    PKTIN_LC245_2_TUT_FLASH = 132,
    PKTIN_LC245_2_TUT_OPEN = 152,

    // inventory
    PKTIN_LC245_2_UPDATE_INV_STOP_TRANSMIT = 143,
    PKTIN_LC245_2_UPDATE_INV_FULL = 156,
    PKTIN_LC245_2_UPDATE_INV_PARTIAL = 95,

    // camera control
    PKTIN_LC245_2_CAM_LOOKAT = 123,
    PKTIN_LC245_2_CAM_SHAKE = 103,
    PKTIN_LC245_2_CAM_MOVETO = 86,
    PKTIN_LC245_2_CAM_RESET = 134,

    // entity updates
    PKTIN_LC245_2_NPC_INFO = 105,
    PKTIN_LC245_2_PLAYER_INFO = 161,

    // input tracking
    PKTIN_LC245_2_FINISH_TRACKING = 165,
    PKTIN_LC245_2_ENABLE_TRACKING = 28,

    // social
    PKTIN_LC245_2_MESSAGE_GAME = 175,
    PKTIN_LC245_2_UPDATE_IGNORELIST = 181,
    PKTIN_LC245_2_CHAT_FILTER_SETTINGS = 2,
    PKTIN_LC245_2_MESSAGE_PRIVATE = 207,
    PKTIN_LC245_2_UPDATE_FRIENDLIST = 109,

    // misc
    PKTIN_LC245_2_UNSET_MAP_FLAG = 233,
    PKTIN_LC245_2_UPDATE_RUNWEIGHT = 70,
    PKTIN_LC245_2_HINT_ARROW = 243,
    PKTIN_LC245_2_UPDATE_REBOOT_TIMER = 26,
    PKTIN_LC245_2_UPDATE_STAT = 110,
    PKTIN_LC245_2_UPDATE_RUNENERGY = 208,
    PKTIN_LC245_2_RESET_ANIMS = 144,
    PKTIN_LC245_2_UPDATE_PID = 49,
    PKTIN_LC245_2_LAST_LOGIN_INFO = 238,
    PKTIN_LC245_2_LOGOUT = 36,
    PKTIN_LC245_2_P_COUNTDIALOG = 56,
    PKTIN_LC245_2_SET_MULTIWAY = 35,

    // maps
    PKTIN_LC245_2_REBUILD_NORMAL = 66,

    // vars
    PKTIN_LC245_2_VARP_SMALL = 192,
    PKTIN_LC245_2_VARP_LARGE = 75,
    PKTIN_LC245_2_RESET_CLIENT_VARCACHE = 25,

    // audio
    PKTIN_LC245_2_SYNTH_SOUND = 209,
    PKTIN_LC245_2_MIDI_SONG = 96,
    PKTIN_LC245_2_MIDI_JINGLE = 39,

    // zones
    PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS = 203,
    PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS = 140,
    PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED = 15,

    // zone protocol
    PKTIN_LC245_2_LOC_MERGE = 188,
    PKTIN_LC245_2_LOC_ANIM = 71,
    PKTIN_LC245_2_OBJ_DEL = 13,
    PKTIN_LC245_2_OBJ_REVEAL = 190,
    PKTIN_LC245_2_LOC_ADD_CHANGE = 119,
    PKTIN_LC245_2_MAP_PROJANIM = 187,
    PKTIN_LC245_2_LOC_DEL = 198,
    PKTIN_LC245_2_OBJ_COUNT = 151,
    PKTIN_LC245_2_MAP_ANIM = 141,
    PKTIN_LC245_2_OBJ_ADD = 94
};

static const struct PacketInDefinition g_packet_in_definitions_lc245_2[256] = {
    [PKTIN_LC245_2_IF_OPENCHAT] = { .length = 2 },
    [PKTIN_LC245_2_IF_OPENMAIN_SIDE] = { .length = 4 },
    [PKTIN_LC245_2_IF_CLOSE] = { .length = 0 },
    [PKTIN_LC245_2_IF_SETTAB] = { .length = 3 },
    [PKTIN_LC245_2_IF_OPENMAIN] = { .length = 2 },
    [PKTIN_LC245_2_IF_OPENSIDE] = { .length = 2 },
    [PKTIN_LC245_2_IF_SETTAB_ACTIVE] = { .length = 1 },
    [PKTIN_LC245_2_IF_SETCOLOUR] = { .length = 4 },
    [PKTIN_LC245_2_IF_SETHIDE] = { .length = 3 },
    [PKTIN_LC245_2_IF_SETOBJECT] = { .length = 6 },
    [PKTIN_LC245_2_IF_SETMODEL] = { .length = 4 },
    [PKTIN_LC245_2_IF_SETANIM] = { .length = 4 },
    [PKTIN_LC245_2_IF_SETPLAYERHEAD] = { .length = 2 },
    [PKTIN_LC245_2_IF_SETTEXT] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_IF_SETNPCHEAD] = { .length = 4 },
    [PKTIN_LC245_2_IF_SETPOSITION] = { .length = 6 },
    [PKTIN_LC245_2_IF_SETSCROLLPOS] = { .length = 4 },
    [PKTIN_LC245_2_TUT_FLASH] = { .length = 1 },
    [PKTIN_LC245_2_TUT_OPEN] = { .length = 2 },
    [PKTIN_LC245_2_UPDATE_INV_STOP_TRANSMIT] = { .length = 2 },
    [PKTIN_LC245_2_UPDATE_INV_FULL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_UPDATE_INV_PARTIAL] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_CAM_LOOKAT] = { .length = 6 },
    [PKTIN_LC245_2_CAM_SHAKE] = { .length = 4 },
    [PKTIN_LC245_2_CAM_MOVETO] = { .length = 6 },
    [PKTIN_LC245_2_CAM_RESET] = { .length = 0 },
    [PKTIN_LC245_2_NPC_INFO] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_PLAYER_INFO] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_FINISH_TRACKING] = { .length = 0 },
    [PKTIN_LC245_2_ENABLE_TRACKING] = { .length = 0 },
    [PKTIN_LC245_2_MESSAGE_GAME] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_LC245_2_UPDATE_IGNORELIST] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_CHAT_FILTER_SETTINGS] = { .length = 3 },
    [PKTIN_LC245_2_MESSAGE_PRIVATE] = { .length = PKTIN_LENGTH_VARU8 },
    [PKTIN_LC245_2_UPDATE_FRIENDLIST] = { .length = 9 },
    [PKTIN_LC245_2_UNSET_MAP_FLAG] = { .length = 0 },
    [PKTIN_LC245_2_UPDATE_RUNWEIGHT] = { .length = 2 },
    [PKTIN_LC245_2_HINT_ARROW] = { .length = 6 },
    [PKTIN_LC245_2_UPDATE_REBOOT_TIMER] = { .length = 2 },
    [PKTIN_LC245_2_UPDATE_STAT] = { .length = 6 },
    [PKTIN_LC245_2_UPDATE_RUNENERGY] = { .length = 1 },
    [PKTIN_LC245_2_RESET_ANIMS] = { .length = 0 },
    [PKTIN_LC245_2_UPDATE_PID] = { .length = 3 },
    [PKTIN_LC245_2_LAST_LOGIN_INFO] = { .length = 10 },
    [PKTIN_LC245_2_LOGOUT] = { .length = 0 },
    [PKTIN_LC245_2_P_COUNTDIALOG] = { .length = 0 },
    [PKTIN_LC245_2_SET_MULTIWAY] = { .length = 1 },
    [PKTIN_LC245_2_REBUILD_NORMAL] = { .length = 4 },
    [PKTIN_LC245_2_VARP_SMALL] = { .length = 3 },
    [PKTIN_LC245_2_VARP_LARGE] = { .length = 6 },
    [PKTIN_LC245_2_RESET_CLIENT_VARCACHE] = { .length = 0 },
    [PKTIN_LC245_2_SYNTH_SOUND] = { .length = 5 },
    [PKTIN_LC245_2_MIDI_SONG] = { .length = 2 },
    [PKTIN_LC245_2_MIDI_JINGLE] = { .length = 4 },
    [PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS] = { .length = 2 },
    [PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS] = { .length = 2 },
    [PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED] = { .length = PKTIN_LENGTH_VARU16 },
    [PKTIN_LC245_2_LOC_MERGE] = { .length = 14 },
    [PKTIN_LC245_2_LOC_ANIM] = { .length = 4 },
    [PKTIN_LC245_2_OBJ_DEL] = { .length = 3 },
    [PKTIN_LC245_2_OBJ_REVEAL] = { .length = 7 },
    [PKTIN_LC245_2_LOC_ADD_CHANGE] = { .length = 4 },
    [PKTIN_LC245_2_MAP_PROJANIM] = { .length = 15 },
    [PKTIN_LC245_2_LOC_DEL] = { .length = 2 },
    [PKTIN_LC245_2_OBJ_COUNT] = { .length = 7 },
    [PKTIN_LC245_2_MAP_ANIM] = { .length = 6 },
    [PKTIN_LC245_2_OBJ_ADD] = { .length = 5 },
};

static inline int
packetin_size_lc245_2(int packet_type)
{
    assert(packet_type >= 0 && packet_type < 256);
    return g_packet_in_definitions_lc245_2[packet_type].length;
}

#endif