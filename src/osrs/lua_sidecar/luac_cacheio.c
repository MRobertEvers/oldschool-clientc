#include "luac_cacheio.h"

#include "osrs/gio_cache_dat.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct CacheDatArchive*
LuaCCacheIO_LoadDat1Archive(
    struct CacheDat* cache_dat,
    int table_id,
    int archive_id,
    int flags)
{
    struct CacheDatArchive* archive = NULL;
    switch( table_id )
    {
    case CACHE_DAT_MAPS:
    {
        int chunk_x = archive_id >> 16;
        int chunk_z = archive_id & 0xFFFF;
        if( flags == 2 )
            archive = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
        else if( flags == 1 )
            archive = gioqb_cache_dat_map_terrain_new_load(cache_dat, chunk_x, chunk_z);
    }
    break;
    default:
        archive = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
        break;
    }
    return archive;
}

struct LuaGameType*
LuaCCacheIO_LoadRequestList(
    struct CacheDat* cache_dat,
    struct LuaCacheIORequestList* list)
{
    if( !list || list->count <= 0 )
        return LuaGameType_NewUserDataArraySpread(0);

    struct LuaGameType* result = LuaGameType_NewUserDataArraySpread(list->count);
    if( !result )
        return NULL;

    for( int i = 0; i < list->count; i++ )
    {
        struct LuaCacheIORequest* req = &list->items[i];
        struct CacheDatArchive* archive = NULL;
        switch( req->format )
        {
        case LUA_CACHEIO_FORMAT_DAT1:
            archive =
                LuaCCacheIO_LoadDat1Archive(cache_dat, req->table_id, req->archive_id, req->flags);
            break;
        case LUA_CACHEIO_FORMAT_DAT2:
            assert(false && "DAT2 cache IO not implemented");
            archive = NULL;
            break;
        default:
            assert(false);
            archive = NULL;
            break;
        }
        assert(archive);
        LuaGameType_UserDataArrayPush(result, archive);
    }
    return result;
}

struct LuaGameType*
LuaCSidecar_CacheIOLoadRequestList(
    struct CacheDat* cache_dat,
    struct LuaGameType* args)
{
    assert(args && LuaGameType_GetVarTypeArrayCount(args) >= 1);
    struct LuaCacheIORequestList* list = (struct LuaCacheIORequestList*)LuaGameType_GetUserData(
        LuaGameType_GetVarTypeArrayAt(args, 0));
    assert(list);
    return LuaCCacheIO_LoadRequestList(cache_dat, list);
}
