#ifndef LUAC_GAMETYPES_H
#define LUAC_GAMETYPES_H

struct lua_State;
struct LuaGameType;

/** Read value at stack idx, return malloc'd LuaGameType. Caller must LuaGameType_Free. */
struct LuaGameType*
LuacGameType_FromLua(
    struct lua_State* L,
    int idx);

/** Push LuaGameType onto stack. Does not free gt. */
int
LuacGameType_PushToLua(
    struct lua_State* L,
    struct LuaGameType* gt);

#endif
