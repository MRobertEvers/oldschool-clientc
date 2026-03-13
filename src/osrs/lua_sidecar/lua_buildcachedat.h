#ifndef LUA_BUILDCACHEDAT_H
#define LUA_BUILDCACHEDAT_H

#include "osrs/buildcachedat.h"
#include "osrs/scripts/buildcache.lua.h"
#include "platforms/common/luac_sidecar.h"

void
lua_buildcachedat_dispatch_async_call(
    struct Platform2_OSX_SDL2* platform,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result);

#endif