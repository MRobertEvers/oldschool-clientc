#include "lua_cacheio.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LuaCacheIORequestList*
LuaCacheIORequestList_New(void)
{
    struct LuaCacheIORequestList* list = calloc(1, sizeof(*list));
    return list;
}

void
LuaCacheIORequestList_Free(struct LuaCacheIORequestList* list)
{
    if( !list )
        return;
    free(list->items);
    free(list);
}

void
LuaCacheIORequestList_Clear(struct LuaCacheIORequestList* list)
{
    if( !list )
        return;
    list->count = 0;
}

void
LuaCacheIORequestList_Push(
    struct LuaCacheIORequestList* list,
    enum LuaCacheIOFormat format,
    int table_id,
    int archive_id,
    int flags)
{
    if( !list )
        return;
    if( list->count >= list->capacity )
    {
        int ncap = list->capacity ? list->capacity * 2 : 8;
        struct LuaCacheIORequest* ni =
            realloc(list->items, (size_t)ncap * sizeof(struct LuaCacheIORequest));
        if( !ni )
            return;
        list->items = ni;
        list->capacity = ncap;
    }
    struct LuaCacheIORequest* r = &list->items[list->count++];
    r->format = format;
    r->table_id = table_id;
    r->archive_id = archive_id;
    r->flags = flags;
}

struct LuaGameType*
LuaCacheIO_request_list_new(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)game;
    (void)args;
    struct LuaCacheIORequestList* list = LuaCacheIORequestList_New();
    if( !list )
        return NULL;
    return LuaGameType_NewUserData(list);
}

struct LuaGameType*
LuaCacheIO_request_list_free(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)game;
    assert(args && LuaGameType_GetVarTypeArrayCount(args) >= 1);
    struct LuaCacheIORequestList* list =
        (struct LuaCacheIORequestList*)LuaGameType_GetUserData(LuaGameType_GetVarTypeArrayAt(args, 0));
    LuaCacheIORequestList_Free(list);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaCacheIO_request_list_reset(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)game;
    assert(args && LuaGameType_GetVarTypeArrayCount(args) >= 1);
    struct LuaCacheIORequestList* list =
        (struct LuaCacheIORequestList*)LuaGameType_GetUserData(LuaGameType_GetVarTypeArrayAt(args, 0));
    LuaCacheIORequestList_Clear(list);
    return LuaGameType_NewVoid();
}
