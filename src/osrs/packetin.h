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
enum PacketInType
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

static const struct PacketInDefinition g_packet_in_definitions[256] = {
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
packetin_size(int packet_type)
{
    assert(packet_type >= 0 && packet_type < 256);
    return g_packet_in_definitions[packet_type].length;
}

static inline int
packetin_write_rebuild_region(
    uint8_t* data,
    int data_size,
    int zonex,
    int zonez)
{
    assert(data_size > 4);

    struct RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };

    p1(&buffer, PKTIN_REBUILD_REGION);
    p2(&buffer, zonex);
    p2(&buffer, zonez);

    return buffer.position;
}

#endif