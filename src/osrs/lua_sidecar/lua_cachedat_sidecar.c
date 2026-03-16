#include "lua_cachedat_sidecar.h"

#include "osrs/buildcachedat.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>

static int
lua_cachedat_table(int lua_table_id)
{
    switch( lua_table_id )
    {
    case 0:
        return CACHE_DAT_CONFIGS;
    case 1:
        return CACHE_DAT_MODELS;
    case 2:
        return CACHE_DAT_ANIMATIONS;
    case 3:
        return CACHE_DAT_SOUNDS;
    case 4:
        return CACHE_DAT_MAPS;
    default:
        assert(false);
        return 0;
    }
}

void
lua_cachedat_load_archive(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result)
{
    int table_id = (int)async_call->args[0];
    int archive_id = (int)async_call->args[1];

    int cache_dat_table = lua_cachedat_table(table_id);
    struct CacheDatArchive* archive = NULL;

    switch( cache_dat_table )
    {
    case CACHE_DAT_MAPS:
    {
        int chunk_x = archive_id >> 16;
        int chunk_z = archive_id & 0xFFFF;
        archive = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
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
lua_buildcachedat_load_archives(
    struct CacheDat* cache_dat,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result)
{
    int table_id = (int)async_call->args[0];
    int count = (int)async_call->args[1];

    int cache_dat_table = lua_cachedat_table(table_id);
    struct CacheDatArchive** archives = NULL;
    archives = malloc(count * sizeof(struct CacheDatArchive*));

    switch( cache_dat_table )
    {
    case CACHE_DAT_MAPS:
    {
        for( int i = 0; i < count; i++ )
        {
            int archive_id = (int)async_call->args[2 + i];
            int chunk_x = archive_id >> 16;
            int chunk_z = archive_id & 0xFFFF;
            archives[i] = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
        }
    }
    break;
    default:
        for( int i = 0; i < count; i++ )
        {
            int archive_id = (int)async_call->args[2 + i];
            archives[i] = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
        }
        break;
    }

    LuaCSidecar_ResultPushPtr(result, archives);
    free(archives);
}