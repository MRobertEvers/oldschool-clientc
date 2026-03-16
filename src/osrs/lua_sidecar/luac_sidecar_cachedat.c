#include "luac_sidecar_cachedat.h"

#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>

void
LuaCSidecar_CachedatLoadArchive(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result)
{
    int table_id = (int)async_call->args[0];
    int archive_id = (int)async_call->args[1];
    int flags = (int)async_call->args[2];

    int cache_dat_table = table_id;
    struct CacheDatArchive* archive = NULL;

    switch( cache_dat_table )
    {
    case CACHE_DAT_MAPS:
    {
        int chunk_x = archive_id >> 16;
        int chunk_z = archive_id & 0xFFFF;
        if( flags == 2 )
        {
            archive = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
        }
        else if( flags == 1 )
        {
            archive = gioqb_cache_dat_map_terrain_new_load(cache_dat, chunk_x, chunk_z);
        }
    }
    break;
    default:
        archive = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
        break;
    }

    assert(archive);
    LuaCSidecar_ResultPushPtr(result, archive);
}

void
LuaCSidecar_CachedatLoadArchives(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result)
{
    int table_id = (int)async_call->args[0];
    int count = (int)async_call->args[1];
    int* archive_ids = (int*)async_call->args[2];
    int flags = (int)async_call->args[3];

    int cache_dat_table = table_id;
    struct CacheDatArchive** archives = NULL;
    archives = malloc(count * sizeof(struct CacheDatArchive*));

    switch( cache_dat_table )
    {
    case CACHE_DAT_MAPS:
    {
        for( int i = 0; i < count; i++ )
        {
            int archive_id = archive_ids[i];
            int chunk_x = archive_id >> 16;
            int chunk_z = archive_id & 0xFFFF;
            if( flags == 2 )
            {
                archives[i] = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
            }
            else if( flags == 1 )
            {
                archives[i] = gioqb_cache_dat_map_terrain_new_load(cache_dat, chunk_x, chunk_z);
            }
        }
    }
    break;
    default:
        for( int i = 0; i < count; i++ )
        {
            int archive_id = archive_ids[i];
            archives[i] = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
        }
        break;
    }

    LuaCSidecar_ResultPushPtrArray(result, archives, count);
}
