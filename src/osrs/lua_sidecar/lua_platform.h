#ifndef LUA_PLATFORM_H
#define LUA_PLATFORM_H

#include "osrs/lua_sidecar/lua_gametypes.h"

struct LuaGameScript
{
    char name[64];
    struct LuaGameType* args;
    /** Owned RevPacket_LC245_2_Item* freed after script run (see gameproto_free_lc245_2_item). */
    void* lc245_packet_item_to_free;
};

struct LuaGameScript*
LuaGameScript_New();

void
LuaGameScript_Free(struct LuaGameScript* script);

char*
LuaGameScript_GetName(struct LuaGameScript* game_script);

struct LuaGameType*
LuaGameScript_GetArgs(struct LuaGameScript* game_script);

#endif