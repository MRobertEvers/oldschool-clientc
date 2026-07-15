#ifndef LUA_CACHEDAT_SIDECAR_H
#define LUA_CACHEDAT_SIDECAR_H

#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/dat1disk/dat1disk.h"

struct LuaGameType*
LuaCSidecar_CachedatLoadArchive(
    struct RSCacheDat1Disk* cache_dat,
    struct LuaGameType* args);

struct LuaGameType*
LuaCSidecar_CachedatLoadArchives(
    struct RSCacheDat1Disk* cache_dat,
    struct LuaGameType* args);

#endif