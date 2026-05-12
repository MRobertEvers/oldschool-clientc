#ifndef LUA_CACHEIO_H
#define LUA_CACHEIO_H

#include "osrs/lua_sidecar/lua_gametypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct GGame;

/** Physical cache container (dat1 vs dat2). */
enum LuaCacheIOFormat
{
    LUA_CACHEIO_FORMAT_DAT1 = 1,
    LUA_CACHEIO_FORMAT_DAT2 = 2,
};

/** One load request (dat-agnostic). */
struct LuaCacheIORequest
{
    enum LuaCacheIOFormat format;
    int table_id;
    int archive_id;
    int flags;
};

struct LuaCacheIORequestList
{
    struct LuaCacheIORequest* items;
    int count;
    int capacity;
};

struct LuaCacheIORequestList*
LuaCacheIORequestList_New(void);

void
LuaCacheIORequestList_Free(struct LuaCacheIORequestList* list);

void
LuaCacheIORequestList_Clear(struct LuaCacheIORequestList* list);

void
LuaCacheIORequestList_Push(
    struct LuaCacheIORequestList* list,
    enum LuaCacheIOFormat format,
    int table_id,
    int archive_id,
    int flags);

struct LuaGameType*
LuaCacheIO_request_list_new(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaCacheIO_request_list_free(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaCacheIO_request_list_reset(
    struct GGame* game,
    struct LuaGameType* args);

#ifdef __cplusplus
}
#endif

#endif
