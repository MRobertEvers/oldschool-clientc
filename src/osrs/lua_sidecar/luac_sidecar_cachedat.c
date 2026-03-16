#include "luac_sidecar_cachedat.h"

#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>

struct LuaGameType*
LuaCSidecar_CachedatLoadArchive(
    struct CacheDat* cache_dat,
    struct LuaGameType* args)
{
    assert(args && LuaGameType_GetVarTypeArrayCount(args) >= 3);

    int table_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 0));
    int archive_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 1));
    int flags = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 2));

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
    return LuaGameType_NewUserData(archive);
}
