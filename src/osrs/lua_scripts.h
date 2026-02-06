#ifndef OSRS_LUA_SCRIPTS_H
#define OSRS_LUA_SCRIPTS_H

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/gio.h"
#include "osrs/gio_assets.h"

#include <stdbool.h>

static int
l_host_io_init(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    gioq_submit(io, GIO_REQ_INIT, 0, 0, 0);
    return 0;
}

static int
l_host_io_dat_map_scenery_load(lua_State* L)
{
    // Retrieve the C pointer for your IO Queue stored at upvalue index 1
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int chunk_x = luaL_checkinteger(L, 1);
    int chunk_z = luaL_checkinteger(L, 2);

    int req_id = gio_assets_dat_map_scenery_load(io, chunk_x, chunk_z); // Use the retrieved pointer

    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_map_terrain_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int chunk_x = luaL_checkinteger(L, 1);
    int chunk_z = luaL_checkinteger(L, 2);
    int req_id = gio_assets_dat_map_terrain_load(io, chunk_x, chunk_z);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_models_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int model_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_dat_models_load(io, model_id);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_animbaseframes_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int animbaseframes_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_dat_animbaseframes_load(io, animbaseframes_id);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_sound_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int sound_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_dat_sound_load(io, sound_id);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_config_texture_sprites_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_dat_config_texture_sprites_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_config_configs_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_dat_config_configs_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_config_version_list_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_dat_config_version_list_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_config_media_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_dat_config_media_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_dat_config_title_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_dat_config_title_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}

static int
l_host_io_poll(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = luaL_checkinteger(L, 1);

    bool success = gioq_poll_for(io, req_id);

    lua_pushboolean(L, success);
    return 1;
}

static int
l_host_io_read(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = luaL_checkinteger(L, 1);

    struct GIOMessage message = { 0 };
    bool success = gioq_read(io, req_id, &message);

    lua_pushboolean(L, success);
    lua_pushinteger(L, message.param_a);
    lua_pushinteger(L, message.param_b);
    lua_pushinteger(L, message.data_size);
    lua_pushlightuserdata(L, message.data);

    gioq_release(io, &message);

    return 5;
}

static const luaL_Reg host_io_funcs[] = {
    { "init",                            l_host_io_init                            },
    { "read",                            l_host_io_read                            },
    { "poll",                            l_host_io_poll                            },
    { "dat_map_scenery_load",            l_host_io_dat_map_scenery_load            },
    { "dat_map_terrain_load",            l_host_io_dat_map_terrain_load            },
    { "dat_models_load",                 l_host_io_dat_models_load                 },
    { "dat_animbaseframes_load",         l_host_io_dat_animbaseframes_load         },
    { "dat_sound_load",                  l_host_io_dat_sound_load                  },
    { "dat_config_texture_sprites_load", l_host_io_dat_config_texture_sprites_load },
    { "dat_config_configs_load",         l_host_io_dat_config_configs_load         },
    { "dat_config_version_list_load",    l_host_io_dat_config_version_list_load    },
    { "dat_config_media_load",           l_host_io_dat_config_media_load           },
    { "dat_config_title_load",           l_host_io_dat_config_title_load           },
    { NULL,                              NULL                                      }
};

static void
register_host_io(
    lua_State* L,
    struct GIOQueue* io)
{
    lua_newtable(L);

    // Push your IO pointer onto the stack
    lua_pushlightuserdata(L, io);

    // luaL_setfuncs adds functions in host_io_funcs to the table on top of stack
    // The '1' tells Lua to associate the 1 lightuserdata as an upvalue for all functions
    luaL_setfuncs(L, host_io_funcs, 1);

    lua_setglobal(L, "HostIO");
}

static int
l_buildcachedat_cache_map_scenery(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int param_a = luaL_checkinteger(L, 1);
    int param_b = luaL_checkinteger(L, 2);
    int data_size = luaL_checkinteger(L, 3);
    void* data = lua_touserdata(L, 4);

    buildcachedat_loader_cache_map_scenery(buildcachedat, param_a, param_b, data_size, data);

    return 0;
}

static int
l_buildcachedat_set_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_set_config_jagfile(buildcachedat, data_size, data);

    return 0;
}

static int
l_buildcachedat_set_versionlist_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_set_versionlist_jagfile(buildcachedat, data_size, data);

    return 0;
}

static int
l_buildcachedat_cache_map_terrain(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int param_a = luaL_checkinteger(L, 1);
    int param_b = luaL_checkinteger(L, 2);
    int data_size = luaL_checkinteger(L, 3);
    void* data = lua_touserdata(L, 4);

    buildcachedat_loader_cache_map_terrain(buildcachedat, param_a, param_b, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_floortype(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_load_floortype(buildcachedat);

    return 0;
}

static int
l_buildcachedat_load_scenery_configs(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_load_scenery_configs(buildcachedat);

    return 0;
}

static int
l_buildcachedat_get_all_scenery_locs(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    
    int* loc_ids = NULL;
    int* chunk_x = NULL;
    int* chunk_z = NULL;
    
    int count = buildcachedat_loader_get_all_scenery_locs(
        buildcachedat, &loc_ids, &chunk_x, &chunk_z);
    
    lua_newtable(L);
    for( int i = 0; i < count; i++ )
    {
        lua_newtable(L);
        lua_pushinteger(L, loc_ids[i]);
        lua_setfield(L, -2, "loc_id");
        lua_pushinteger(L, chunk_x[i]);
        lua_setfield(L, -2, "chunk_x");
        lua_pushinteger(L, chunk_z[i]);
        lua_setfield(L, -2, "chunk_z");
        lua_rawseti(L, -2, i + 1);
    }
    
    free(loc_ids);
    free(chunk_x);
    free(chunk_z);
    
    return 1;
}

static int
l_buildcachedat_get_scenery_model_ids(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int loc_id = luaL_checkinteger(L, 1);
    
    int* model_ids = NULL;
    int count = buildcachedat_loader_get_scenery_model_ids(
        buildcachedat, loc_id, &model_ids);
    
    lua_newtable(L);
    for( int i = 0; i < count; i++ )
    {
        lua_pushinteger(L, model_ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    
    if( model_ids )
        free(model_ids);
    
    return 1;
}

static int
l_buildcachedat_cache_model(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int model_id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);

    buildcachedat_loader_cache_model(buildcachedat, model_id, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_textures(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_load_textures(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_sequences(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_load_sequences(buildcachedat);

    return 0;
}

static int
l_buildcachedat_get_animbaseframes_count(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    int count = buildcachedat_loader_get_animbaseframes_count(buildcachedat);
    
    lua_pushinteger(L, count);

    return 1;
}

static int
l_buildcachedat_cache_animbaseframes(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int animbaseframes_id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);

    buildcachedat_loader_cache_animbaseframes(buildcachedat, animbaseframes_id, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_media(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_load_media(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_title(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_load_title(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_load_idkits(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_load_idkits(buildcachedat);

    return 0;
}

static int
l_buildcachedat_load_objects(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_load_objects(buildcachedat);

    return 0;
}

static int
l_buildcachedat_finalize_scene(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int map_sw_x = luaL_checkinteger(L, 1);
    int map_sw_z = luaL_checkinteger(L, 2);
    int map_ne_x = luaL_checkinteger(L, 3);
    int map_ne_z = luaL_checkinteger(L, 4);

    buildcachedat_loader_finalize_scene(buildcachedat, game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    return 0;
}

static const luaL_Reg buildcachedat_funcs[] = {
    { "cache_map_scenery",        l_buildcachedat_cache_map_scenery        },
    { "set_config_jagfile",       l_buildcachedat_set_config_jagfile       },
    { "set_versionlist_jagfile",  l_buildcachedat_set_versionlist_jagfile  },
    { "cache_map_terrain",        l_buildcachedat_cache_map_terrain        },
    { "load_floortype",           l_buildcachedat_load_floortype           },
    { "load_scenery_configs",     l_buildcachedat_load_scenery_configs     },
    { "get_all_scenery_locs",     l_buildcachedat_get_all_scenery_locs     },
    { "get_scenery_model_ids",    l_buildcachedat_get_scenery_model_ids    },
    { "cache_model",              l_buildcachedat_cache_model              },
    { "load_textures",            l_buildcachedat_load_textures            },
    { "load_sequences",           l_buildcachedat_load_sequences           },
    { "get_animbaseframes_count", l_buildcachedat_get_animbaseframes_count },
    { "cache_animbaseframes",     l_buildcachedat_cache_animbaseframes     },
    { "load_media",               l_buildcachedat_load_media               },
    { "load_title",               l_buildcachedat_load_title               },
    { "load_idkits",              l_buildcachedat_load_idkits              },
    { "load_objects",             l_buildcachedat_load_objects             },
    { "finalize_scene",           l_buildcachedat_finalize_scene           },
    { NULL,                       NULL                                     }
};

static void
register_buildcachedat(
    lua_State* L,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game)
{
    lua_newtable(L);

    // Push both BuildCacheDat and GGame pointers onto the stack
    lua_pushlightuserdata(L, buildcachedat);
    lua_pushlightuserdata(L, game);

    // luaL_setfuncs adds functions in buildcachedat_funcs to the table on top of stack
    // The '2' tells Lua to associate the 2 lightuserdata as upvalues for all functions
    luaL_setfuncs(L, buildcachedat_funcs, 2);
    lua_setglobal(L, "BuildCacheDat");
}

#endif