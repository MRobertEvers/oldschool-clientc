#ifndef OSRS_LUA_SCRIPTS_H
#define OSRS_LUA_SCRIPTS_H

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/buildcache.h"
#include "osrs/buildcache_loader.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/cache_utils.h"
#include "osrs/configmap.h"
#include "osrs/game.h"
#include "osrs/gameproto_exec.h"
#include "osrs/gio.h"
#include "osrs/gio_assets.h"
#include "osrs/minimap.h"
#include "osrs/painters.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/sprites.h"
#include "osrs/rscache/tables/textures.h"
#include "osrs/scenebuilder.h"
#include "osrs/texture.h"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

/* Asset loaders for BuildCache path (non-Dat; same as task_init_scene) */
static int
l_host_io_asset_map_scenery_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int chunk_x = luaL_checkinteger(L, 1);
    int chunk_z = luaL_checkinteger(L, 2);
    int req_id = gio_assets_map_scenery_load(io, chunk_x, chunk_z);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_config_scenery_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_config_scenery_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_model_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int model_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_model_load(io, model_id);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_map_terrain_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int chunk_x = luaL_checkinteger(L, 1);
    int chunk_z = luaL_checkinteger(L, 2);
    int req_id = gio_assets_map_terrain_load(io, chunk_x, chunk_z);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_config_underlay_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_config_underlay_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_config_overlay_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_config_overlay_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_texture_definitions_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_texture_definitions_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_spritepack_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int spritepack_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_spritepack_load(io, spritepack_id);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_config_sequences_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int req_id = gio_assets_config_sequences_load(io);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_animation_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int frame_archive_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_animation_load(io, frame_archive_id);
    lua_pushinteger(L, req_id);
    return 1;
}
static int
l_host_io_asset_framemap_load(lua_State* L)
{
    struct GIOQueue* io = (struct GIOQueue*)lua_touserdata(L, lua_upvalueindex(1));
    int framemap_id = luaL_checkinteger(L, 1);
    int req_id = gio_assets_framemap_load(io, framemap_id);
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
    { "asset_map_scenery_load",          l_host_io_asset_map_scenery_load          },
    { "asset_config_scenery_load",       l_host_io_asset_config_scenery_load       },
    { "asset_model_load",                l_host_io_asset_model_load                },
    { "asset_map_terrain_load",          l_host_io_asset_map_terrain_load          },
    { "asset_config_underlay_load",      l_host_io_asset_config_underlay_load      },
    { "asset_config_overlay_load",       l_host_io_asset_config_overlay_load       },
    { "asset_texture_definitions_load",  l_host_io_asset_texture_definitions_load  },
    { "asset_spritepack_load",           l_host_io_asset_spritepack_load           },
    { "asset_config_sequences_load",     l_host_io_asset_config_sequences_load     },
    { "asset_animation_load",            l_host_io_asset_animation_load            },
    { "asset_framemap_load",             l_host_io_asset_framemap_load             },
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
l_buildcachedat_init_floortypes_from_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_init_floortypes_from_config_jagfile(buildcachedat);

    return 0;
}

static int
l_buildcachedat_init_scenery_configs_from_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_init_scenery_configs_from_config_jagfile(buildcachedat);

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

    int count =
        buildcachedat_loader_get_all_scenery_locs(buildcachedat, &loc_ids, &chunk_x, &chunk_z);

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
    int count = buildcachedat_loader_get_scenery_model_ids(buildcachedat, loc_id, &model_ids);

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
l_buildcachedat_get_npc_model_ids(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int npc_id = luaL_checkinteger(L, 1);

    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(buildcachedat, npc_id);
    lua_newtable(L);
    if( !npc || !npc->models )
        return 1;
    for( int i = 0; i < npc->models_count; i++ )
    {
        lua_pushinteger(L, npc->models[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcachedat_get_idk_model_ids(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int idk_id = luaL_checkinteger(L, 1);

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
    lua_newtable(L);
    if( !idk || !idk->models )
        return 1;
    for( int i = 0; i < idk->models_count; i++ )
    {
        lua_pushinteger(L, idk->models[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcachedat_get_obj_model_ids(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    int obj_id = luaL_checkinteger(L, 1);

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    lua_newtable(L);
    if( !obj )
        return 1;
    int idx = 0;
    if( obj->model != -1 )
    {
        idx++;
        lua_pushinteger(L, obj->model);
        lua_rawseti(L, -2, idx);
    }
    if( obj->manwear != -1 )
    {
        idx++;
        lua_pushinteger(L, obj->manwear);
        lua_rawseti(L, -2, idx);
    }
    if( obj->manwear2 != -1 )
    {
        idx++;
        lua_pushinteger(L, obj->manwear2);
        lua_rawseti(L, -2, idx);
    }
    if( obj->manwear3 != -1 )
    {
        idx++;
        lua_pushinteger(L, obj->manwear3);
        lua_rawseti(L, -2, idx);
    }
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
l_buildcachedat_cache_textures(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_cache_textures(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_init_sequences_from_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_init_sequences_from_config_jagfile(buildcachedat);

    return 0;
}

static int
l_buildcachedat_get_animbaseframes_count_from_versionlist_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    int count =
        buildcachedat_loader_get_animbaseframes_count_from_versionlist_jagfile(buildcachedat);

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
l_buildcachedat_cache_media(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_cache_media(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_cache_title(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(2));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);

    buildcachedat_loader_cache_title(buildcachedat, game, data_size, data);

    return 0;
}

static int
l_buildcachedat_init_idkits_from_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_init_idkits_from_config_jagfile(buildcachedat);

    return 0;
}

static int
l_buildcachedat_init_objects_from_config_jagfile(lua_State* L)
{
    struct BuildCacheDat* buildcachedat =
        (struct BuildCacheDat*)lua_touserdata(L, lua_upvalueindex(1));

    buildcachedat_loader_init_objects_from_config_jagfile(buildcachedat);

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

    buildcachedat_loader_finalize_scene(
        buildcachedat, game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    return 0;
}

static const luaL_Reg buildcachedat_funcs[] = {
    { "cache_map_scenery",                                 l_buildcachedat_cache_map_scenery                   },
    { "set_config_jagfile",                                l_buildcachedat_set_config_jagfile                  },
    { "set_versionlist_jagfile",                           l_buildcachedat_set_versionlist_jagfile             },
    { "cache_map_terrain",                                 l_buildcachedat_cache_map_terrain                   },
    { "init_floortypes_from_config_jagfile",               l_buildcachedat_init_floortypes_from_config_jagfile },
    { "init_scenery_configs_from_config_jagfile",
     l_buildcachedat_init_scenery_configs_from_config_jagfile                                                  },
    { "get_all_scenery_locs",                              l_buildcachedat_get_all_scenery_locs                },
    { "get_scenery_model_ids",                             l_buildcachedat_get_scenery_model_ids               },
    { "get_npc_model_ids",                                 l_buildcachedat_get_npc_model_ids                   },
    { "get_idk_model_ids",                                 l_buildcachedat_get_idk_model_ids                   },
    { "get_obj_model_ids",                                 l_buildcachedat_get_obj_model_ids                   },
    { "cache_model",                                       l_buildcachedat_cache_model                         },
    { "cache_textures",                                    l_buildcachedat_cache_textures                      },
    { "init_sequences_from_config_jagfile",                l_buildcachedat_init_sequences_from_config_jagfile  },
    { "get_animbaseframes_count_from_versionlist_jagfile",
     l_buildcachedat_get_animbaseframes_count_from_versionlist_jagfile                                         },
    { "cache_animbaseframes",                              l_buildcachedat_cache_animbaseframes                },
    { "cache_media",                                       l_buildcachedat_cache_media                         },
    { "cache_title",                                       l_buildcachedat_cache_title                         },
    { "init_idkits_from_config_jagfile",                   l_buildcachedat_init_idkits_from_config_jagfile     },
    { "init_objects_from_config_jagfile",                  l_buildcachedat_init_objects_from_config_jagfile    },
    { "finalize_scene",                                    l_buildcachedat_finalize_scene                      },
    { NULL,                                                NULL                                                }
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

/* BuildCache (game->buildcache) API for init_scene.lua - same flow as task_init_scene.c */
#define LUA_BUILDCACHE_MAX_IDS 2048

static void
lua_buildcache_free_init_configmaps(struct GGame* game)
{
    if( game->init_scenery_configmap )
    {
        configmap_free(game->init_scenery_configmap);
        game->init_scenery_configmap = NULL;
    }
    if( game->init_texture_definitions_configmap )
    {
        configmap_free(game->init_texture_definitions_configmap);
        game->init_texture_definitions_configmap = NULL;
    }
    if( game->init_sequences_configmap )
    {
        configmap_free(game->init_sequences_configmap);
        game->init_sequences_configmap = NULL;
    }
}

static int
l_buildcache_ensure(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int wx_sw = luaL_checkinteger(L, 1);
    int wz_sw = luaL_checkinteger(L, 2);
    int wx_ne = luaL_checkinteger(L, 3);
    int wz_ne = luaL_checkinteger(L, 4);
    int size_x = luaL_checkinteger(L, 5);
    int size_z = luaL_checkinteger(L, 6);

    if( !game->buildcache )
        game->buildcache = buildcache_new();

    if( !game->sys_painter )
    {
        game->sys_painter = painter_new(size_x, size_z, MAP_TERRAIN_LEVELS);
        game->sys_painter_buffer = painter_buffer_new();
    }
    if( !game->sys_minimap )
    {
        game->sys_minimap = minimap_new(wx_sw, wz_sw, wx_ne, wz_ne, MAP_TERRAIN_LEVELS);
    }
    if( !game->scenebuilder )
        game->scenebuilder = scenebuilder_new_painter(game->sys_painter, game->sys_minimap);

    lua_buildcache_free_init_configmaps(game);
    return 0;
}

static int
l_buildcache_add_map_scenery(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int mapx = luaL_checkinteger(L, 1);
    int mapz = luaL_checkinteger(L, 2);
    (void)luaL_checkinteger(L, 3); /* param_b unused; mapx/mapz passed explicitly */
    int data_size = luaL_checkinteger(L, 4);
    void* data = lua_touserdata(L, 5);

    buildcache_loader_add_map_scenery(game->buildcache, mapx, mapz, data_size, data);
    return 0;
}

static int
l_buildcache_get_scenery_ids(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int seen[LUA_BUILDCACHE_MAX_IDS];
    int nseen = 0;
    memset(seen, 0, sizeof(seen));

    struct DashMapIter* iter = buildcache_iter_new_map_scenery(game->buildcache);
    int mapx = 0, mapz = 0;
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcache_iter_next_map_scenery(iter, &mapx, &mapz)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            int id = locs->locs[i].loc_id;
            int j = 0;
            for( ; j < nseen && seen[j] != id; j++ )
                ;
            if( j == nseen && nseen < LUA_BUILDCACHE_MAX_IDS )
                seen[nseen++] = id;
        }
    }
    buildcache_iter_free_map_scenery(iter);

    lua_createtable(L, nseen, 0);
    for( int i = 0; i < nseen; i++ )
    {
        lua_pushinteger(L, seen[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcache_add_config_scenery(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    int ids_size = (int)lua_rawlen(L, 3);
    int ids[LUA_BUILDCACHE_MAX_IDS];
    if( ids_size > LUA_BUILDCACHE_MAX_IDS )
        ids_size = LUA_BUILDCACHE_MAX_IDS;
    for( int i = 0; i < ids_size; i++ )
    {
        lua_rawgeti(L, 3, i + 1);
        ids[i] = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    buildcache_loader_add_config_scenery(
        game->buildcache, game, data_size, data, ids, ids_size);
    return 0;
}

static int
l_buildcache_get_queued_models_and_sequences(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int models[LUA_BUILDCACHE_MAX_IDS], sequences[LUA_BUILDCACHE_MAX_IDS];
    int nmodels = 0, nseq = 0;
    memset(models, 0, sizeof(models));
    memset(sequences, 0, sizeof(sequences));

    struct DashMapIter* iter = buildcache_iter_new_map_scenery(game->buildcache);
    int mapx = 0, mapz = 0;
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcache_iter_next_map_scenery(iter, &mapx, &mapz)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheConfigLocation* config_loc =
                configmap_get(game->init_scenery_configmap, locs->locs[i].loc_id);
            if( !config_loc )
                continue;
            if( config_loc->seq_id > 0 )
            {
                int sid = config_loc->seq_id;
                int j = 0;
                for( ; j < nseq && sequences[j] != sid; j++ )
                    ;
                if( j == nseq && nseq < LUA_BUILDCACHE_MAX_IDS )
                    sequences[nseq++] = sid;
            }
            int* shapes = config_loc->shapes;
            int** model_id_sets = config_loc->models;
            int* lengths = config_loc->lengths;
            int shapes_and_model_count = config_loc->shapes_and_model_count;
            if( !model_id_sets )
                continue;
            if( !shapes )
            {
                int count = lengths[0];
                for( int k = 0; k < count && nmodels < LUA_BUILDCACHE_MAX_IDS; k++ )
                {
                    int mid = model_id_sets[0][k];
                    if( !mid )
                        continue;
                    int j = 0;
                    for( ; j < nmodels && models[j] != mid; j++ )
                        ;
                    if( j == nmodels )
                        models[nmodels++] = mid;
                }
            }
            else
            {
                for( int k = 0; k < shapes_and_model_count && nmodels < LUA_BUILDCACHE_MAX_IDS;
                     k++ )
                {
                    int count_inner = lengths[k];
                    for( int j = 0; j < count_inner; j++ )
                    {
                        int mid = model_id_sets[k][j];
                        if( !mid )
                            continue;
                        int jj = 0;
                        for( ; jj < nmodels && models[jj] != mid; jj++ )
                            ;
                        if( jj == nmodels )
                            models[nmodels++] = mid;
                    }
                }
            }
        }
    }
    buildcache_iter_free_map_scenery(iter);

    lua_createtable(L, 0, 2);
    lua_createtable(L, nmodels, 0);
    for( int i = 0; i < nmodels; i++ )
    {
        lua_pushinteger(L, models[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "models");
    lua_createtable(L, nseq, 0);
    for( int i = 0; i < nseq; i++ )
    {
        lua_pushinteger(L, sequences[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "sequences");
    return 1;
}

static int
l_buildcache_add_model(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int model_id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);
    buildcache_loader_add_model(game->buildcache, model_id, data_size, data);
    return 0;
}

static int
l_buildcache_add_map_terrain(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int mapx = luaL_checkinteger(L, 1);
    int mapz = luaL_checkinteger(L, 2);
    int data_size = luaL_checkinteger(L, 3);
    void* data = lua_touserdata(L, 4);
    buildcache_loader_add_map_terrain(
        game->buildcache, mapx, mapz, data_size, data);
    return 0;
}

static int
l_buildcache_add_config_underlay(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);
    buildcache_loader_add_config_underlay(game->buildcache, data_size, data);
    return 0;
}

static int
l_buildcache_add_config_overlay(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);
    buildcache_loader_add_config_overlay(game->buildcache, data_size, data);
    return 0;
}

static int
l_buildcache_get_queued_texture_ids(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int ids[LUA_BUILDCACHE_MAX_IDS];
    int n = 0;
    memset(ids, 0, sizeof(ids));

    struct CacheConfigOverlay* config_overlay = NULL;
    struct DashMapIter* iter = buildcache_iter_new_config_overlay(game->buildcache);
    while( (config_overlay = buildcache_iter_next_config_overlay(iter)) )
    {
        if( config_overlay->texture > 0 )
        {
            int tid = config_overlay->texture;
            int j = 0;
            for( ; j < n && ids[j] != tid; j++ )
                ;
            if( j == n && n < LUA_BUILDCACHE_MAX_IDS )
                ids[n++] = tid;
        }
    }
    buildcache_iter_free_config_overlay(iter);

    int model_id = 0;
    struct CacheModel* model = NULL;
    iter = buildcache_iter_new_models(game->buildcache);
    while( (model = buildcache_iter_next_models(iter, &model_id)) )
    {
        if( !model->face_textures )
            continue;
        for( int i = 0; i < model->face_count; i++ )
        {
            int ft = model->face_textures[i];
            if( ft != -1 && n < LUA_BUILDCACHE_MAX_IDS )
            {
                int j = 0;
                for( ; j < n && ids[j] != ft; j++ )
                    ;
                if( j == n )
                    ids[n++] = ft;
            }
        }
    }
    buildcache_iter_free_models(iter);

    iter = buildcache_iter_new_map_scenery(game->buildcache);
    int mapx = 0, mapz = 0;
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcache_iter_next_map_scenery(iter, &mapx, &mapz)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheConfigLocation* config_loc =
                configmap_get(game->init_scenery_configmap, locs->locs[i].loc_id);
            if( !config_loc || !config_loc->retexture_count || !config_loc->retextures_to )
                continue;
            for( int r = 0; r < config_loc->retexture_count && n < LUA_BUILDCACHE_MAX_IDS; r++ )
            {
                int tid = config_loc->retextures_to[r];
                int j = 0;
                for( ; j < n && ids[j] != tid; j++ )
                    ;
                if( j == n )
                    ids[n++] = tid;
            }
        }
    }
    buildcache_iter_free_map_scenery(iter);

    lua_createtable(L, n, 0);
    for( int i = 0; i < n; i++ )
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcache_add_texture_definitions(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    int ids_size = (int)lua_rawlen(L, 3);
    int ids[LUA_BUILDCACHE_MAX_IDS];
    if( ids_size > LUA_BUILDCACHE_MAX_IDS )
        ids_size = LUA_BUILDCACHE_MAX_IDS;
    for( int i = 0; i < ids_size; i++ )
    {
        lua_rawgeti(L, 3, i + 1);
        ids[i] = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    buildcache_loader_add_texture_definitions(
        game->buildcache, game, data_size, data, ids, ids_size);
    return 0;
}

static int
l_buildcache_get_queued_spritepack_ids(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int ids[LUA_BUILDCACHE_MAX_IDS];
    int n = 0;
    memset(ids, 0, sizeof(ids));
    struct CacheTexture* tex = NULL;
    struct DashMapIter* iter = dashmap_iter_new(game->init_texture_definitions_configmap);
    while( (tex = (struct CacheTexture*)configmap_iter_next(iter, NULL)) )
    {
        for( int i = 0; tex->sprite_ids && i < tex->sprite_ids_count && n < LUA_BUILDCACHE_MAX_IDS;
             i++ )
        {
            int sid = tex->sprite_ids[i];
            int j = 0;
            for( ; j < n && ids[j] != sid; j++ )
                ;
            if( j == n )
                ids[n++] = sid;
        }
    }
    dashmap_iter_free(iter);
    lua_createtable(L, n, 0);
    for( int i = 0; i < n; i++ )
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcache_add_spritepack(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);
    buildcache_loader_add_spritepack(game->buildcache, id, data_size, data);
    return 0;
}

static int
l_buildcache_build_textures(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int id = 0;
    struct CacheTexture* texture_definition = NULL;
    struct DashMapIter* iter = dashmap_iter_new(game->init_texture_definitions_configmap);
    while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter, &id)) )
    {
        struct DashTexture* texture =
            texture_new_from_definition(texture_definition, game->buildcache->spritepacks_hmap);
        buildcache_add_texture(game->buildcache, id, texture);
        dash3d_add_texture(game->sys_dash, id, texture);
    }
    dashmap_iter_free(iter);
    return 0;
}

static int
l_buildcache_add_config_sequences(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int data_size = luaL_checkinteger(L, 1);
    void* data = lua_touserdata(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    int ids_size = (int)lua_rawlen(L, 3);
    int ids[LUA_BUILDCACHE_MAX_IDS];
    if( ids_size > LUA_BUILDCACHE_MAX_IDS )
        ids_size = LUA_BUILDCACHE_MAX_IDS;
    for( int i = 0; i < ids_size; i++ )
    {
        lua_rawgeti(L, 3, i + 1);
        ids[i] = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    buildcache_loader_add_config_sequences(
        game->buildcache, game, data_size, data, ids, ids_size);
    return 0;
}

static int
l_buildcache_get_queued_frame_archive_ids(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int ids[LUA_BUILDCACHE_MAX_IDS];
    int n = 0;
    memset(ids, 0, sizeof(ids));
    struct CacheConfigSequence* sequence = NULL;
    struct DashMapIter* iter = dashmap_iter_new(game->init_sequences_configmap);
    while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter, NULL)) )
    {
        for( int i = 0; sequence->frame_ids && i < sequence->frame_count; i++ )
        {
            int frame_id = sequence->frame_ids[i];
            int archive_id = (frame_id >> 16) & 0xFFFF;
            int j = 0;
            for( ; j < n && ids[j] != archive_id; j++ )
                ;
            if( j == n && n < LUA_BUILDCACHE_MAX_IDS )
                ids[n++] = archive_id;
        }
    }
    dashmap_iter_free(iter);
    lua_createtable(L, n, 0);
    for( int i = 0; i < n; i++ )
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcache_add_frame_blob(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);
    buildcache_loader_add_frame_blob(game->buildcache, id, data_size, data);
    return 0;
}

static int
l_buildcache_get_queued_framemap_ids(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int ids[LUA_BUILDCACHE_MAX_IDS];
    int n = 0;
    memset(ids, 0, sizeof(ids));
    struct CacheConfigSequence* sequence = NULL;
    struct DashMapIter* iter = dashmap_iter_new(game->init_sequences_configmap);
    while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter, NULL)) )
    {
        for( int i = 0; i < sequence->frame_count; i++ )
        {
            int frame_id = sequence->frame_ids[i];
            int frame_archive_id = (frame_id >> 16) & 0xFFFF;
            int frame_file_id = frame_id & 0xFFFF;
            struct CacheFrameBlob* frame_blob =
                buildcache_get_frame_blob(game->buildcache, frame_archive_id);
            if( !frame_blob )
                continue;
            struct FileList* fl = (struct FileList*)frame_blob;
            if( frame_file_id < 1 || frame_file_id - 1 >= fl->file_count )
                continue;
            char* file_data = fl->files[frame_file_id - 1];
            int file_data_size = fl->file_sizes[frame_file_id - 1];
            int framemap_id = frame_framemap_id_from_file(file_data, file_data_size);
            int j = 0;
            for( ; j < n && ids[j] != framemap_id; j++ )
                ;
            if( j == n && n < LUA_BUILDCACHE_MAX_IDS )
                ids[n++] = framemap_id;
        }
    }
    dashmap_iter_free(iter);
    lua_createtable(L, n, 0);
    for( int i = 0; i < n; i++ )
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int
l_buildcache_add_framemap(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int id = luaL_checkinteger(L, 1);
    int data_size = luaL_checkinteger(L, 2);
    void* data = lua_touserdata(L, 3);
    buildcache_loader_add_framemap(game->buildcache, id, data_size, data);
    return 0;
}

static int
l_buildcache_build_frame_anims(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    struct CacheConfigSequence* sequence = NULL;
    struct DashMapIter* iter = dashmap_iter_new(game->init_sequences_configmap);
    while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter, NULL)) )
    {
        for( int i = 0; i < sequence->frame_count; i++ )
        {
            int frame_id = sequence->frame_ids[i];
            int frame_archive_id = (frame_id >> 16) & 0xFFFF;
            int frame_file_id = sequence->frame_ids[i] & 0xFFFF;
            struct CacheFrameBlob* frame_blob =
                buildcache_get_frame_blob(game->buildcache, frame_archive_id);
            if( !frame_blob )
                continue;
            struct FileList* fl = (struct FileList*)frame_blob;
            if( frame_file_id - 1 >= fl->file_count )
                frame_file_id = fl->file_count;
            if( frame_file_id < 1 || frame_file_id - 1 >= fl->file_count )
                continue;
            char* file_data = fl->files[frame_file_id - 1];
            int file_data_size = fl->file_sizes[frame_file_id - 1];
            int framemap_id = frame_framemap_id_from_file(file_data, file_data_size);
            struct CacheFramemap* framemap = buildcache_get_framemap(game->buildcache, framemap_id);
            if( !framemap )
                continue;
            struct CacheFrame* cache_frame =
                frame_new_decode2(frame_id, framemap, file_data, file_data_size);
            buildcache_add_frame_anim(game->buildcache, frame_id, cache_frame);
        }
    }
    dashmap_iter_free(iter);
    return 0;
}

static int
l_buildcache_build_scene(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    int wx_sw = luaL_checkinteger(L, 1);
    int wz_sw = luaL_checkinteger(L, 2);
    int wx_ne = luaL_checkinteger(L, 3);
    int wz_ne = luaL_checkinteger(L, 4);
    int size_x = luaL_checkinteger(L, 5);
    int size_z = luaL_checkinteger(L, 6);
    game->scene = scenebuilder_load_from_buildcache(
        game->scenebuilder, wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z, game->buildcache);
    return 0;
}

static const luaL_Reg buildcache_funcs[] = {
    { "ensure",                          l_buildcache_ensure                          },
    { "add_map_scenery",                 l_buildcache_add_map_scenery                 },
    { "get_scenery_ids",                 l_buildcache_get_scenery_ids                 },
    { "add_config_scenery",              l_buildcache_add_config_scenery              },
    { "get_queued_models_and_sequences", l_buildcache_get_queued_models_and_sequences },
    { "add_model",                       l_buildcache_add_model                       },
    { "add_map_terrain",                 l_buildcache_add_map_terrain                 },
    { "add_config_underlay",             l_buildcache_add_config_underlay             },
    { "add_config_overlay",              l_buildcache_add_config_overlay              },
    { "get_queued_texture_ids",          l_buildcache_get_queued_texture_ids          },
    { "add_texture_definitions",         l_buildcache_add_texture_definitions         },
    { "get_queued_spritepack_ids",       l_buildcache_get_queued_spritepack_ids       },
    { "add_spritepack",                  l_buildcache_add_spritepack                  },
    { "build_textures",                  l_buildcache_build_textures                  },
    { "add_config_sequences",            l_buildcache_add_config_sequences            },
    { "get_queued_frame_archive_ids",    l_buildcache_get_queued_frame_archive_ids    },
    { "add_frame_blob",                  l_buildcache_add_frame_blob                  },
    { "get_queued_framemap_ids",         l_buildcache_get_queued_framemap_ids         },
    { "add_framemap",                    l_buildcache_add_framemap                    },
    { "build_frame_anims",               l_buildcache_build_frame_anims               },
    { "build_scene",                     l_buildcache_build_scene                     },
    { NULL,                              NULL                                         }
};

static void
register_buildcache(
    lua_State* L,
    struct GGame* game)
{
    lua_newtable(L);
    lua_pushlightuserdata(L, game);
    luaL_setfuncs(L, buildcache_funcs, 1);
    lua_setglobal(L, "BuildCache");
}

static int
l_gameproto_get_npc_ids_from_packet(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    (void)game;
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    static struct PktNpcInfoReader reader;
    reader.extended_count = 0;
    reader.current_op = 0;
    reader.max_ops = 2048;
    struct PktNpcInfoOp ops[2048];
    int count = pkt_npc_info_reader_read(&reader, &item->packet._npc_info, ops, 2048);

    lua_newtable(L);
    int idx = 0;
    for( int i = 0; i < count; i++ )
    {
        if( ops[i].kind == PKT_NPC_INFO_OPBITS_NPCTYPE )
        {
            idx++;
            lua_pushinteger(L, ops[i]._bitvalue);
            lua_rawseti(L, -2, idx);
        }
    }
    return 1;
}

static int
l_gameproto_exec_npc_info(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    gameproto_exec_npc_info(game, &item->packet);
    return 0;
}

static int
l_gameproto_get_rebuild_bounds(lua_State* L)
{
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    lua_pushinteger(L, item->packet._map_rebuild.zonex);
    lua_pushinteger(L, item->packet._map_rebuild.zonez);
    return 2;
}

static int
l_gameproto_exec_rebuild(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    gameproto_exec_rebuild_normal(game, &item->packet);
    return 0;
}

static int
l_gameproto_get_player_appearance_ids(lua_State* L)
{
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    static struct PktPlayerInfoReader reader;
    reader.extended_count = 0;
    reader.current_op = 0;
    reader.max_ops = 2048;
    struct PktPlayerInfoOp ops[2048];
    int count = pkt_player_info_reader_read(&reader, &item->packet._player_info, ops, 2048);

    lua_newtable(L); /* idk_ids */
    lua_newtable(L); /* obj_ids */
    int idk_idx = 0;
    int obj_idx = 0;

    for( int i = 0; i < count; i++ )
    {
        if( ops[i].kind != PKT_PLAYER_INFO_OP_APPEARANCE )
            continue;
        struct PlayerAppearance appearance;
        player_appearance_decode(
            &appearance, ops[i]._appearance.appearance, ops[i]._appearance.len);
        struct AppearanceOp op;
        for( int slot = 0; slot < 12; slot++ )
        {
            appearances_decode(&op, appearance.appearance, slot);
            if( op.kind == APPEARANCE_KIND_IDK )
            {
                idk_idx++;
                lua_pushinteger(L, op.id);
                lua_rawseti(L, -3, idk_idx);
            }
            else if( op.kind == APPEARANCE_KIND_OBJ )
            {
                obj_idx++;
                lua_pushinteger(L, op.id);
                lua_rawseti(L, -2, obj_idx);
            }
        }
    }
    return 2;
}

static int
l_gameproto_exec_player_info(lua_State* L)
{
    struct GGame* game = (struct GGame*)lua_touserdata(L, lua_upvalueindex(1));
    struct RevPacket_LC245_2_Item* item = (struct RevPacket_LC245_2_Item*)lua_touserdata(L, 1);

    gameproto_exec_player_info(game, &item->packet);
    return 0;
}

static const luaL_Reg gameproto_funcs[] = {
    { "get_npc_ids_from_packet",   l_gameproto_get_npc_ids_from_packet   },
    { "exec_npc_info",             l_gameproto_exec_npc_info             },
    { "get_rebuild_bounds",        l_gameproto_get_rebuild_bounds        },
    { "exec_rebuild",              l_gameproto_exec_rebuild              },
    { "get_player_appearance_ids", l_gameproto_get_player_appearance_ids },
    { "exec_player_info",          l_gameproto_exec_player_info          },
    { NULL,                        NULL                                  }
};

static void
register_gameproto(
    lua_State* L,
    struct GGame* game)
{
    lua_newtable(L);
    lua_pushlightuserdata(L, game);
    luaL_setfuncs(L, gameproto_funcs, 1);
    lua_setglobal(L, "GameProto");
}

#endif