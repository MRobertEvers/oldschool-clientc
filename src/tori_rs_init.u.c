#ifndef TORI_RS_INIT_U_C
#define TORI_RS_INIT_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "graphics/dash.h"
#include "osrs/cache_utils.h"
#include "osrs/clientscript_vm.h"
#include "osrs/configmap.h"
#include "osrs/dash_utils.h"
#include "osrs/gameproto_process.h"
#include "osrs/interface_state.h"
#include "osrs/loginproto.h"
#include "osrs/lua_scripts.h"
#include "osrs/minimap.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_load.h"
#include "osrs/rs_component_state.h"
#include "osrs/rsa.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/scene2.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"
#include "osrs/zone_state.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_PATH
#define CACHE_PATH "../cache"
#endif
#ifndef CACHE_DAT_PATH
#define CACHE_DAT_PATH "../cache254"
#endif
#ifndef LUA_SCRIPTS_DIR
#define LUA_SCRIPTS_DIR "../src/osrs/scripts"
#endif

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

#include "platforms/common/platform_memory.h"

struct GGame*
LibToriRS_GameNew(
    struct ToriRSNetSharedBuffer* net_shared,
    int graphics3d_width,
    int graphics3d_height)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    struct PlatformMemoryInfo mem = { 0 };
    platform_get_memory_info(&mem);
    printf("GameNew: Memory info: %zu / %zu / %zu\n", mem.heap_used, mem.heap_total, mem.heap_peak);

    dash_init();

    game->net_shared = net_shared;

    game->viewport_offset_x = 0;
    game->viewport_offset_y = 0;

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
    // Camera (x, y, z): 4939, -850, 3406 : 38, 26
    game->camera_world_x = 4939;
    game->camera_world_y = -850;
    game->camera_world_z = 3406;

    // game->camera_world_x = 3939;
    // game->camera_world_y = -800;
    // game->camera_world_z = 12589;
    // Camera (pitch, yaw, roll): 1916, 328, 0
    // Camera (pitch, yaw, roll): 48, 1856, 0
    // Camera (pitch, yaw, roll): 148, 1856, 0
    game->camera_pitch = 148;
    game->camera_yaw = 1856;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 70;
    game->camera_rotation_speed = 20;
    game->next_camera_save_ms = 0;
    game->cc = 100000;
    game->latched = false;

    game->sys_dash = dash_new();

    platform_get_memory_info(&mem);
    printf(
        "GameNew: Memory info after dash_new: %zu / %zu / %zu\n",
        mem.heap_used,
        mem.heap_total,
        mem.heap_peak);

    game->sys_painter_buffer = painter_buffer_new();

    platform_get_memory_info(&mem);
    printf(
        "GameNew: Memory info after painter_buffer_new: %zu / %zu / %zu\n",
        mem.heap_used,
        mem.heap_total,
        mem.heap_peak);
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
    game->buildcache = NULL;

    game->iface = interface_state_new();

    platform_get_memory_info(&mem);
    printf(
        "GameNew: Memory info after painter_buffer_new: %zu / %zu / %zu\n",
        mem.heap_used,
        mem.heap_total,
        mem.heap_peak);

    game->random_in = isaac_new(NULL, 0);
    game->random_out = isaac_new(NULL, 0);
    init_rsa(game);

    game->packet_buffer = malloc(sizeof(struct PacketBuffer));
    memset(game->packet_buffer, 0, sizeof(struct PacketBuffer));
    packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);

    game->loginproto = NULL; /* created by LibToriRS_NetConnectLogin */

    platform_get_memory_info(&mem);
    printf(
        "GameNew: Memory info pre uiscene_new: %zu / %zu / %zu\n",
        mem.heap_used,
        mem.heap_total,
        mem.heap_peak);

    game->ui_scene = uiscene_new(256);
    game->scene2 = scene2_new(20000, 4000);
    game->ui_root_buffer = uitree_new(64);
    game->ui_stack = uitree_new(64);
    game->inv_pool = uitree_inv_pool_new(32);
    game->uitree_stack_top = -1;
    game->uitree_current = -1;
    game->clientscript_vm = clientscript_vm_new();
    game->uiscene_queued_commands = LibToriRS_RenderCommandBufferNew(64);
    game->minimap_dynamic_commands = minimap_commands_new(64);

    platform_get_memory_info(&mem);
    printf(
        "GameNew: Memory info after uiscene_new: %zu / %zu / %zu\n",
        mem.heap_used,
        mem.heap_total,
        mem.heap_peak);

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

void
LibToriRS_GameFree(struct GGame* game)
{
    if( !game )
        return;

    if( game->world )
        world_free(game->world);

    if( game->scene2 )
    {
        scene2_free(game->scene2);
        game->scene2 = NULL;
    }

    interface_state_free(game->iface);
    game->iface = NULL;

    if( game->buildcachedat )
        buildcachedat_free(game->buildcachedat);
    if( game->buildcache )
        buildcache_free(game->buildcache);

    if( game->sys_dash )
        dash_free(game->sys_dash);
    if( game->sys_painter_buffer )
    {
        free(game->sys_painter_buffer->commands);
        free(game->sys_painter_buffer);
    }

    free(game->position);
    free(game->view_port);
    free(game->iface_view_port);
    free(game->camera);

    if( game->random_in )
        isaac_free(game->random_in);
    if( game->random_out )
        isaac_free(game->random_out);

    mp_clear(&game->rsa.exponent);
    mp_clear(&game->rsa.modulus);

    if( game->packet_buffer )
    {
        free(game->packet_buffer->data);
        free(game->packet_buffer);
    }
    if( game->loginproto )
        loginproto_free(game->loginproto);

    if( game->ui_scene )
        uiscene_free(game->ui_scene);
    if( game->inv_pool )
        uitree_inv_pool_free(game->inv_pool);
    game->inv_pool = NULL;
    if( game->ui_root_buffer )
        uitree_free(game->ui_root_buffer);
    if( game->ui_stack )
        uitree_free(game->ui_stack);
    if( game->clientscript_vm )
        clientscript_vm_free(game->clientscript_vm);
    if( game->rs_component_state )
        rs_component_state_pool_free(game->rs_component_state);

    lua_buildcache_free_init_configmaps(game);

    script_queue_clear(&game->script_queue);

    free(game);
}

#endif