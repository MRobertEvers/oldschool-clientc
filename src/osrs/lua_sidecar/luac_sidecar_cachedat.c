#include "luac_sidecar_cachedat.h"

#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_cacheio.h"
#include "osrs/lua_sidecar/luac_sidecar.h"

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

    struct CacheDatArchive* archive =
        LuaCCacheIO_LoadDat1Archive(cache_dat, table_id, archive_id, flags);

    assert(archive);
    return LuaGameType_NewUserData(archive);
}

struct LuaGameType*
LuaCSidecar_CachedatLoadArchives(
    struct CacheDat* cache_dat,
    struct LuaGameType* args)
{
    assert(args);
    int count = LuaGameType_GetVarTypeArrayCount(args);
    assert((count % 3) == 0);

    int triplet_count = count / 3;
    if( triplet_count <= 0 )
        return LuaGameType_NewUserDataArraySpread(0);

    struct CacheDatArchive** archives =
        (struct CacheDatArchive**)malloc((size_t)triplet_count * sizeof(struct CacheDatArchive*));
    if( !archives )
        return NULL;

    for( int i = 0; i < triplet_count; i++ )
    {
        int base = i * 3;
        int table_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 0));
        int archive_id = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 1));
        int flags = LuaGameType_GetInt(LuaGameType_GetVarTypeArrayAt(args, base + 2));

        archives[i] = LuaCCacheIO_LoadDat1Archive(cache_dat, table_id, archive_id, flags);
    }

    struct LuaGameType* result = LuaGameType_NewUserDataArraySpread(triplet_count);
    for( int i = 0; i < triplet_count; i++ )
        LuaGameType_UserDataArrayPush(result, archives[i]);
    free(archives);
    return result;
}
