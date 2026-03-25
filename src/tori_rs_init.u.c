#ifndef TORI_RS_INIT_U_C
#define TORI_RS_INIT_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/cache_utils.h"
#include "osrs/configmap.h"
#include "osrs/dash_utils.h"
#include "osrs/gameproto_process.h"
#include "osrs/loginproto.h"
#include "osrs/lua_scripts.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/static_ui.h"
#include "osrs/revconfig/static_ui_load.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"
#define CACHE_DAT_PATH "../cache254"
#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

/** Return the module table stored as upvalue (so require("hostio_utils") works). */
static int
hostio_utils_loader(lua_State* L)
{
    lua_pushvalue(L, lua_upvalueindex(1));
    return 1;
}

/** Load buildcache.lua from filepath (upvalue 1) and return its module table. */
static int
buildcache_file_loader(lua_State* L)
{
    const char* filepath = lua_tostring(L, lua_upvalueindex(1));
    if( luaL_loadfile(L, filepath) != LUA_OK )
        return lua_error(L);
    if( lua_pcall(L, 0, 1, 0) != LUA_OK )
        return lua_error(L);
    return 1;
}

static void
init_rsa(struct GGame* game)
{
    // const char* default_e_decimal =
    //     "58778699976184461502525193738213253649000149147835990136706041084440742975821";
    // const char* default_n_decimal =
    //     "716290052522979803276181679123052729632931329123232429023784926350120820797289405392906563"
    //     "6522363163621000728841182238772712427862772219676577293600221789";

    char const* default_e_hex =
        "81f390b2cf8ca7039ee507975951d5a0b15a87bf8b3f99c966834118c50fd94d"; // pad exponent to
                                                                            // an even number
                                                                            // (prefix a 0 if
                                                                            // needed)
    char const* default_n_hex =
        "88c38748a58228f7261cdc340b5691d7d0975dee0ecdb717609e6bf971eb3fe723ef9d130e468681373976"
        "8ad9472eb46d8bfcc042c1a5fcb05e931f632eea5d";

    rsa_init(&game->rsa, default_e_hex, default_n_hex);
}

struct RenderLoadKeyInt
{
    int key;
};

struct RenderLoadKeyPtr
{
    uintptr_t key;
};

static struct DashMap*
new_render_load_map(
    size_t key_size,
    size_t entry_size,
    int capacity)
{
    struct DashMapConfig config = {
        .buffer = malloc((size_t)capacity * entry_size),
        .buffer_size = (size_t)capacity * entry_size,
        .key_size = key_size,
        .entry_size = entry_size,
    };
    return dashmap_new(&config, 0);
}

struct GGame*
LibToriRS_GameNew(
    struct ToriRSNetSharedBuffer* net_shared,
    int graphics3d_width,
    int graphics3d_height)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    dash_init();

    game->net_shared = net_shared;

    // Initialize interface IDs to -1 (no interface)
    game->viewport_interface_id = -1;
    game->sidebar_interface_id = -1;
    game->chat_interface_id = -1;

    game->chat_public_mode = 0;
    game->chat_private_mode = 0;
    game->chat_trade_mode = 0;

    // Initialize tab system
    game->selected_tab = 3; // Default to inventory tab (tab 3)
    for( int i = 0; i < 14; i++ )
    {
        game->tab_interface_id[i] = -1;
    }

    // Initialize item selection
    game->selected_item = -1;
    game->selected_interface = -1;
    game->selected_area = 0;
    game->selected_cycle = 0;

    for( int i = 0; i < MAX_COMPONENT_SCROLL_IDS; i++ )
        game->component_scroll_position[i] = 0;

    game->current_hovered_interface_id = -1;
    game->scroll_arrow_hold_cycles_last = 0;

    game->clicked_tile_valid = 0;
    game->clicked_tile_x = 0;
    game->clicked_tile_z = 0;
    game->scene_base_tile_x = 0;
    game->scene_base_tile_z = 0;
    game->zone_base_x = 0;
    game->zone_base_z = 0;
    game->path_tile_count = 0;

    game->running = true;

    // x =
    // 7616
    // z =
    // 8100
    game->camera_world_x = 0;
    game->camera_world_y = -500;
    game->camera_world_z = 0;

    // Camera (x, y, z): 3939, -800, 12589 : 30, 98
    // Camera (pitch, yaw, roll): 388, 1556, 0

    // Camera (x, y, z): 3790, -780, 4355 : 29, 34
    // Camera (x, y, z): 79, -150, -64 : 0, 0
    // Camera (x, y, z): -194, -850, -470 : -1, -3
    game->camera_world_x = -194;
    game->camera_world_y = -850;
    game->camera_world_z = -470;

    // game->camera_world_x = 3939;
    // game->camera_world_y = -800;
    // game->camera_world_z = 12589;
    // Camera (pitch, yaw, roll): 1916, 328, 0
    // Camera (pitch, yaw, roll): 48, 1856, 0
    game->camera_pitch = 48;
    game->camera_yaw = 1856;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 70;
    game->camera_rotation_speed = 20;
    game->cc = 100000;
    game->latched = false;

    game->sys_dash = dash_new();
    game->sys_painter_buffer = painter_buffer_new();
    game->sys_painter = painter_new(104, 104, MAP_TERRAIN_LEVELS);

    game->position = malloc(sizeof(struct DashPosition));
    memset(game->position, 0, sizeof(struct DashPosition));
    game->view_port = malloc(sizeof(struct DashViewPort));
    memset(game->view_port, 0, sizeof(struct DashViewPort));
    game->camera = malloc(sizeof(struct DashCamera));
    memset(game->camera, 0, sizeof(struct DashCamera));
    game->iface_view_port = malloc(sizeof(struct DashViewPort));
    memset(game->iface_view_port, 0, sizeof(struct DashViewPort));

    game->view_port->stride = graphics3d_width;
    game->view_port->width = graphics3d_width;
    game->view_port->height = graphics3d_height;
    game->view_port->x_center = graphics3d_width / 2;
    game->view_port->y_center = graphics3d_height / 2;

    game->camera->fov_rpi2048 = 512;
    game->camera->near_plane_z = 50;

    game->buildcachedat = buildcachedat_new();
    game->buildcache = buildcache_new();

    game->random_in = isaac_new(NULL, 0);
    game->random_out = isaac_new(NULL, 0);
    init_rsa(game);

    game->packet_buffer = malloc(sizeof(struct PacketBuffer));
    memset(game->packet_buffer, 0, sizeof(struct PacketBuffer));
    packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);

    game->loginproto = NULL; /* created by LibToriRS_NetConnectLogin */

    game->ui_scene = uiscene_new(512);
    game->static_ui = static_ui_buffer_new(512);
    game->ui_render_command_buffer = LibToriRS_RenderCommandBufferNew(2048);

    // struct CacheDatConfigComponentList* config_interface_list =
    //     cache_dat_config_component_list_new_decode(file_data, file_data_size);

    // assert(config_interface_list != NULL);

    struct ScriptArgs args;
    script_queue_init(&game->script_queue);
    {
        args = (struct ScriptArgs){
            .tag = SCRIPT_INIT,
        };
        script_queue_push(&game->script_queue, &args);

        args = (struct ScriptArgs){
            .tag = SCRIPT_INIT_UI,
        };
        script_queue_push(&game->script_queue, &args);

        args  = (struct ScriptArgs){
            .tag = SCRIPT_LOAD_SCENE_DAT,
            .u.load_scene_dat = {
                .wx_sw = 50 * 64,
                .wz_sw = 52 * 64,
                .wx_ne = 51 * 64,
                .wz_ne = 53 * 64,
                .size_x = 104,
                .size_z = 104,
            },
        };
        script_queue_push(&game->script_queue, &args);
    }

    return game;
}

void
LibToriRS_GameSetWorldViewportSize(
    struct GGame* game,
    int width,
    int height)
{
    if( !game || !game->view_port )
        return;
    if( width < 1 )
        width = 1;
    if( height < 1 )
        height = 1;

    game->view_port->width = width;
    game->view_port->height = height;
    game->view_port->stride = width;
    game->view_port->x_center = width / 2;
    game->view_port->y_center = height / 2;
}

#endif