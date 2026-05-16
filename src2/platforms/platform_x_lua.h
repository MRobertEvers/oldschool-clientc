#ifndef PLATFORM_X_LUA_H
#define PLATFORM_X_LUA_H

#include "../libtorirs.h"

struct LibToriPlatformX_Lua;

struct LibToriPlatformX_Lua*
LibToriPlatformX_LuaNew(struct LibToriRS_Instance* instance);

void
LibToriPlatformX_LuaFree(struct LibToriPlatformX_Lua* lua);

#define LIBTORI_PLATFORM_X_LUA_OK 0
#define LIBTORI_PLATFORM_X_LUA_ERROR -1
#define LIBTORI_PLATFORM_X_LUA_YIELDED 1

int
LibToriPlatformX_LuaRun(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance);

int
LibToriPlatformX_LuaContinue(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance);

int
LibToriPlatformX_LuaReadYieldCount(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance);

struct LibToriRS_ScriptValue*
LibToriPlatformX_LuaReadYieldValue(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance,
    int index);

#endif