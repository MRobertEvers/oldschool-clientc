#ifndef LUAC_CACHEIO_H
#define LUAC_CACHEIO_H

#include "osrs/lua_sidecar/lua_cacheio.h"
#include "osrs/lua_sidecar/lua_gametypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct CacheDat;
struct CacheDatArchive;

struct CacheDatArchive*
LuaCCacheIO_LoadDat1Archive(
    struct CacheDat* cache_dat,
    int table_id,
    int archive_id,
    int flags);

/** Load every request in order; returns USERDATA_ARRAY_SPREAD of CacheDatArchive* (may be empty). */
struct LuaGameType*
LuaCCacheIO_LoadRequestList(
    struct CacheDat* cache_dat,
    struct LuaCacheIORequestList* list);

/** VarTypeArray: first element is userdata(LuaCacheIORequestList*). */
struct LuaGameType*
LuaCSidecar_CacheIOLoadRequestList(
    struct CacheDat* cache_dat,
    struct LuaGameType* args);

#ifdef __cplusplus
}
#endif

#endif
