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
#include "osrs/scenebuilder.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"
#define CACHE_DAT_PATH "../cache254"
#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3d-raster/src/osrs/scripts"

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

struct GGame*
LibToriRS_GameNew(
    struct GIOQueue* io,
    int graphics3d_width,
    int graphics3d_height)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    dash_init();

    game->io = io;
    game->running = true;

    game->players[ACTIVE_PLAYER_SLOT].alive = false;
    game->players[ACTIVE_PLAYER_SLOT].position.x = 50;
    game->players[ACTIVE_PLAYER_SLOT].position.z = 50;
    game->players[ACTIVE_PLAYER_SLOT].position.height = 0;

    game->netin = ringbuf_new(4096);
    game->netout = ringbuf_new(4096);

    game->login = malloc(sizeof(struct LCLogin));
    memset(game->login, 0, sizeof(struct LCLogin));
    lclogin_init(game->login, 245, NULL, false);
    lclogin_load_rsa_public_key_from_env(game->login);

    game->packet_buffer = malloc(sizeof(struct PacketBuffer));
    memset(game->packet_buffer, 0, sizeof(struct PacketBuffer));

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
    game->camera_world_x = 79;
    game->camera_world_y = -150;
    game->camera_world_z = -64;

    // game->camera_world_x = 3939;
    // game->camera_world_y = -800;
    // game->camera_world_z = 12589;
    // Camera (pitch, yaw, roll): 1916, 328, 0
    game->camera_pitch = 1916;
    game->camera_yaw = 328;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 70;
    game->camera_rotation_speed = 20;
    game->cc = 100000;
    game->latched = false;

    game->sys_dash = dash_new();

    game->scene_elements = vec_new(sizeof(struct SceneElement), 1024);

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

    game->loginproto =
        loginproto_new(game->random_in, game->random_out, &game->rsa, "asdf2", "a", NULL);

    gametask_new_init_io((void*)game, game->io);

    // gametask_new_init_scene_dat((void*)game, 50, 50, 51, 51);

    struct CacheDat* cachedat = cache_dat_new_from_directory(CACHE_DAT_PATH);

    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cachedat, CACHE_DAT_CONFIGS, CONFIG_DAT_INTERFACES);

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);

    int idx = -1;
    int name_hash = archive_name_hash_dat("data");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            idx = i;
            break;
        }
    }

    assert(idx != -1);

    char* file_data = filelist->files[idx];
    int file_data_size = filelist->file_sizes[idx];

    struct CacheDatConfigComponentList* config_interface_list =
        cache_dat_config_component_list_new_decode(file_data, file_data_size);

    assert(config_interface_list != NULL);

    game->L = luaL_newstate();
    luaL_openlibs(game->L);
    game->L_coro = lua_newthread(game->L);

    register_host_io(game->L, game->io);

    // Add scripts dir to package.path so require("hostio_utils") and require("load_scene_dat") find .lua files
    lua_getglobal(game->L, "package");
    lua_getfield(game->L, -1, "path");
    const char* old_path = lua_tostring(game->L, -1);
    lua_pushfstring(game->L, LUA_SCRIPTS_DIR "/?.lua;%s", old_path ? old_path : "");
    lua_setfield(game->L, -3, "path");
    lua_pop(game->L, 2);

    // Load load_scene_dat.lua (returns one function) and call it with game.
    if( luaL_dofile(
            game->L,
            LUA_SCRIPTS_DIR "/load_scene_dat.lua") != LUA_OK )
    {
        const char* err = lua_tostring(game->L, -1);
        fprintf(stderr, "Error loading load_scene_dat.lua: %s\n", err);
        lua_pop(game->L, 1);
    }
    else
    {
        // Call the returned load_scene_dat(game) on the coroutine (it yields in HostIOUtils.await).
        lua_xmove(game->L, game->L_coro, 1);
        lua_pushlightuserdata(game->L_coro, game);

        int nargs = 1;
        for( ;; )
        {
            int nres;
            int status = lua_resume(game->L_coro, game->L, nargs, &nres);
            nargs = 0; // subsequent resumes pass no arguments

            if( status == LUA_YIELD )
            {
                // Coroutine yielded (e.g. waiting on async I/O). Resume again.
                continue;
            }
            if( status == LUA_OK )
            {
                // Coroutine finished. Result(s) on L_coro stack.
                lua_pop(game->L_coro, nres);
                break;
            }
            // Error
            const char* err = lua_tostring(game->L_coro, -1);
            fprintf(stderr, "Error in load_scene_dat coroutine: %s\n", err);
            lua_pop(game->L_coro, 1);
            break;
        }
    }

    return game;
}

#endif