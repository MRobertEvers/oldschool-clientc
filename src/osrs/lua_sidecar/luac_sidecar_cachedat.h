#ifndef LUA_CACHEDAT_SIDECAR_H
#define LUA_CACHEDAT_SIDECAR_H

#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/cache_dat.h"

struct LuaGameType*
LuaCSidecar_CachedatLoadArchive(
    struct CacheDat* cache_dat,
    struct LuaGameType* args);

#endif