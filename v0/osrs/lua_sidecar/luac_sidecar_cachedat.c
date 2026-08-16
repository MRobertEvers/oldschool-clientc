#include "luac_sidecar_cachedat.h"

#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/rscache/dat1disk/dat1disk.h"

#include <assert.h>

struct LuaGameType*
LuaCSidecar_CachedatLoadArchive(
    struct RSCacheDat1Disk* cache_dat,
    struct LuaGameType* args)
{
    assert(args && LuaGameType_GetVarTypeArrayCount(args) >= 3);

    int table_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 0));
    int archive_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 1));
    int flags = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, 2));

    int cache_dat_table = table_id;
    struct RSCacheDat1Disk_Archive* archive = NULL;

    switch( cache_dat_table )
    {
    case RSCacheDat1Disk_Table_Maps:
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
        archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, table_id, archive_id);
        break;
    }

    assert(archive);
    return LuaGameType_NewUserData(archive);
}

struct LuaGameType*
LuaCSidecar_CachedatLoadArchives(
    struct RSCacheDat1Disk* cache_dat,
    struct LuaGameType* args)
{
    assert(args);
    int count = LuaGameType_GetVarTypeArrayCount(args);
    assert((count % 3) == 0);

    int triplet_count = count / 3;
    if( triplet_count <= 0 )
        return LuaGameType_NewUserDataArraySpread(0);

    struct RSCacheDat1Disk_Archive** archives =
        (struct RSCacheDat1Disk_Archive**)malloc((size_t)triplet_count * sizeof(struct RSCacheDat1Disk_Archive*));
    if( !archives )
        return NULL;

    for( int i = 0; i < triplet_count; i++ )
    {
        int base = i * 3;
        int table_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 0));
        int archive_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 1));
        int flags = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 2));

        int cache_dat_table = table_id;
        struct RSCacheDat1Disk_Archive* archive = NULL;

        switch( cache_dat_table )
        {
        case RSCacheDat1Disk_Table_Maps:
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
            archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, table_id, archive_id);
            break;
        }

        archives[i] = archive;
    }

    struct LuaGameType* result = LuaGameType_NewUserDataArraySpread(triplet_count);
    for( int i = 0; i < triplet_count; i++ )
        LuaGameType_UserDataArrayPush(result, archives[i]);
    free(archives);
    return result;
}
