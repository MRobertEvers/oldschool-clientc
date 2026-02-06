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

static const luaL_Reg buildcachedat_funcs[] = {
    { "cache_map_scenery", l_buildcachedat_cache_map_scenery },
    { NULL,                NULL                              }
};

static void
register_buildcachedat(
    lua_State* L,
    struct BuildCacheDat* buildcachedat)
{
    lua_newtable(L);

    // Push your BuildCacheDat pointer onto the stack
    lua_pushlightuserdata(L, buildcachedat);

    // luaL_setfuncs adds functions in buildcachedat_funcs to the table on top of stack
    // The '1' tells Lua to associate the 1 lightuserdata as an upvalue for all functions
    luaL_setfuncs(L, buildcachedat_funcs, 1);
    lua_setglobal(L, "BuildCacheDat");
}

#endif