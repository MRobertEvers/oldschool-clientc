#ifndef LUA_CACHEDAT_SIDECAR_H
#define LUA_CACHEDAT_SIDECAR_H

#include "osrs/rscache/cache_dat.h"

void
lua_cachedat_load_archive(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result);

void
lua_cachedat_load_archives(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result);

#endif