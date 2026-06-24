#include "../../src2/commands/libtorirs_command_queue.h"
#include "../../src2/games/runescape.h"
#include "../../src2/libtorirs.h"
#include "../../src2/libtorirs_internal.h"
#include "../../src2/platforms/platform_sdl2/platform_sdl2.h"
#include "../../src2/platforms/platform_sdl2/platform_sdl2_renderer_soft3d.h"
#include "../../src2/platforms/platform_x/cachelib.h"
#include "../../src2/platforms/platform_x/cachelib_platform.h"
#include "../../src2/platforms/platform_x_io_reactor.h"
#include "../../src2/platforms/platform_x_lua.h"
#include "../../src2/render/libtorirs_render.h"
#include "../../src2/scripting/libtorirs_scriptapi.h"
#include "../../src2/scripting/libtorirs_scripting.h"
#include "../../src2/toridraw/toridraw.h"
#include "../../src2/toridraw/toridraw_model.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, "FAIL: ");                                                                \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                     \
        return 1;                                                                                  \
    } while( 0 )

enum
{
    PROJECTILE_MODEL_ID = 3081,
    PROJECTILE_SEQ_ID = 659,
    TICK_MS = 20,
};

static bool
has_flag(
    int argc,
    char* argv[],
    char const* flag)
{
    for( int i = 1; i < argc; i++ )
    {
        if( argv[i] && strcmp(argv[i], flag) == 0 )
            return true;
    }
    return false;
}

static bool
chdir_to_script_root(void)
{
    static char const* const candidates[] = {
        "../../src2/programs/sdl2",
        "../../../src2/programs/sdl2",
        "../src2/programs/sdl2",
        NULL,
    };

    char script_path[512];
    for( int i = 0; candidates[i]; i++ )
    {
        snprintf(
            script_path,
            sizeof(script_path),
            "%s/../../revs/scripts/init_runescape.lua",
            candidates[i]);
        FILE* script = fopen(script_path, "rb");
        if( !script )
            continue;
        fclose(script);
        return chdir(candidates[i]) == 0;
    }

    return false;
}

static char const*
resolve_cache_dat_path(
    int argc,
    char* argv[])
{
    for( int i = 1; i < argc; i++ )
    {
        if( !argv[i] || argv[i][0] == '\0' )
            continue;
        if( argv[i][0] == '-' )
            continue;
        return argv[i];
    }

    static char const* const candidates[] = {
        "cache254",
        "../cache254",
        "../../cache254",
        "../../../cache254",
        "../../../../cache254",
        NULL,
    };

    char dat_path[512];
    for( int i = 0; candidates[i]; i++ )
    {
        snprintf(dat_path, sizeof(dat_path), "%s/main_file_cache.dat", candidates[i]);
        FILE* f = fopen(dat_path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

static bool
run_init_script(
    struct LibToriRS_Instance* instance,
    struct LibToriPlatformX_Lua* lua,
    struct LibToriPlatformX_IOReactor* io_reactor)
{
    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(LibToriRS_GetScriptQueue(instance), "init_runescape.lua");
    if( !script )
        return false;
    script->is_inline = false;

    while( !LibToriRS_ScriptQueueIsEmpty(LibToriRS_GetScriptQueue(instance)) )
    {
        while( LibToriRS_TasksRun(instance) )
            LibToriPlatformX_IOReactorProcess(io_reactor, LibToriRS_GetIOQueue(instance));

        int rc = LibToriPlatformX_LuaRun(lua, instance);
        while( rc == LIBTORI_PLATFORM_X_LUA_YIELDED )
        {
            LibToriPlatformX_IOReactorProcess(io_reactor, LibToriRS_GetIOQueue(instance));
            rc = LibToriPlatformX_LuaContinue(lua, instance);
        }
        if( rc != LIBTORI_PLATFORM_X_LUA_OK )
            return false;
    }

    return true;
}

static bool
load_model_into_game_cache(
    struct LibToriRS_Instance* instance,
    struct LibToriPlatformX_IOReactor* io_reactor,
    int model_id)
{
    struct LibToriRS_IOQueue* io = LibToriRS_GetIOQueue(instance);
    if( !io || !io_reactor )
        return false;

    LibToriRS_ScriptAPI_Dat1_ModelFetch(instance, model_id, io);

    bool loaded = false;
    for( int i = 0; i < 64; i++ )
    {
        LibToriPlatformX_IOReactorProcess(io_reactor, io);
        if( LibToriRS_ScriptAPI_Dat1_ModelLoad(instance, io) )
        {
            loaded = true;
            break;
        }
    }
    if( !loaded )
        return false;

    LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(instance, model_id);
    return true;
}

static void
render_frame(
    struct LibToriPlatformSDL2* platform,
    struct LibToriPlatformSDL2_RendererSoft3D* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    int projectile_element_id,
    int* out_draw_model_count,
    bool* out_projectile_drawn,
    bool paced)
{
    LibToriPlatformSDL2_PollEvents(platform, command_queue);

    struct LibToriPlatformSDL2_RendererSoft3D_Stats stats = {
        .track_element_id = projectile_element_id,
        .draw_model_count = 0,
        .track_element_drawn = false,
    };
    LibToriPlatformSDL2_RendererSoft3D_Render(renderer, instance, &stats);

    *out_draw_model_count = stats.draw_model_count;
    *out_projectile_drawn = stats.track_element_drawn;

    if( paced )
        SDL_Delay(TICK_MS);
}

static void
run_interactive_loop(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    struct LibToriPlatformSDL2* platform,
    struct LibToriPlatformSDL2_RendererSoft3D* renderer)
{
    uint64_t time_ms = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time_ms);

    while( !LibToriRS_CommandQueue_IsQuit(command_queue) )
    {
        time_ms = SDL_GetTicks64();
        LibToriPlatformSDL2_PollEvents(platform, command_queue);
        LibToriRS_TickInput(instance, command_queue, time_ms);
        LibToriPlatformSDL2_RendererSoft3D_Render(renderer, instance, NULL);
        SDL_Delay(TICK_MS);
    }
}

static int
test_projectile_movement_and_painter(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    struct LibToriPlatformX_IOReactor* io_reactor,
    struct LibToriPlatformSDL2* platform,
    struct LibToriPlatformSDL2_RendererSoft3D* renderer)
{
    struct GameRunescape* game = instance->runescape;
    if( !game || !game->world_built || !game->world || !game->world->load_complete )
        FAIL("runescape world not built");

    int const projectile_model_id = PROJECTILE_MODEL_ID;
    if( !load_model_into_game_cache(instance, io_reactor, projectile_model_id) )
        FAIL("could not load model %d", projectile_model_id);

    int const camera_sx = game->camera_position->x / 128;
    int const camera_sz = game->camera_position->z / 128;
    int const spawn_sx = camera_sx + 2;
    int const spawn_sz = camera_sz + 2;
    int const dst_sx = camera_sx + 7;
    int const dst_sz = camera_sz + 2;
    int const level = 0;
    int const seq_id = PROJECTILE_SEQ_ID;
    int const test_delay = 0;

    int projectile_idx = game_runescape_spawn_projectile(
        game,
        projectile_model_id,
        seq_id,
        spawn_sx,
        spawn_sz,
        dst_sx,
        dst_sz,
        level,
        RUNESCAPE_PROJECTILE_STARTHEIGHT,
        RUNESCAPE_PROJECTILE_ENDHEIGHT,
        test_delay,
        RUNESCAPE_PROJECTILE_ANGLE,
        RUNESCAPE_PROJECTILE_LENGTH,
        RUNESCAPE_PROJECTILE_OFFSET,
        RUNESCAPE_PROJECTILE_STEP);
    if( projectile_idx < 0 )
        FAIL("game_runescape_spawn_projectile failed");

    struct WorldProjectile* projectile = &game->world->projectiles[projectile_idx];
    int const element_id = projectile->element_id;
    int const start_pos_x = (int)projectile->x;
    int const start_grid_x = start_pos_x >> 7;

    if( start_grid_x != spawn_sx )
        FAIL(
            "projectile spawn grid mismatch (grid %d; expected %d)",
            start_grid_x,
            spawn_sx);

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( !element )
        FAIL("projectile scene element missing");

    if( element->anim_seq_id != seq_id )
        FAIL("projectile anim_seq_id mismatch (got %d, expected %d)", element->anim_seq_id, seq_id);
    if( !element->animation || element->animation->frame_count <= 0 )
        FAIL("sequence %d did not resolve to an animation", seq_id);

    int const start_anim_frame = element->anim_frame;

    uint64_t time_ms = 0;
    LibToriRS_InitTime(instance, time_ms);

    bool projectile_drawn = false;
    int draw_model_count = 0;
    int max_y = (int)projectile->y;
    int min_y = (int)projectile->y;
    bool saw_pitch = false;

    for( int frame = 0; frame < 60; frame++ )
    {
        time_ms += TICK_MS;
        LibToriRS_TickInput(instance, command_queue, time_ms);

        int frame_draws = 0;
        bool frame_projectile_drawn = false;
        render_frame(
            platform,
            renderer,
            instance,
            command_queue,
            element_id,
            &frame_draws,
            &frame_projectile_drawn,
            true);

        draw_model_count += frame_draws;
        if( frame_projectile_drawn )
            projectile_drawn = true;

        if( !projectile->launched )
            continue;

        int const pos_y = (int)projectile->y;
        if( pos_y > max_y )
            max_y = pos_y;
        if( pos_y < min_y )
            min_y = pos_y;
        if( projectile->pitch != 0 )
            saw_pitch = true;

        if( (int)projectile->x <= start_pos_x )
            FAIL("projectile did not move east along arc at frame %d", frame);
    }

    if( !projectile->launched )
        FAIL("projectile never launched");

    if( max_y == min_y )
        FAIL("projectile Y did not change (no arc)");

    if( !saw_pitch )
        FAIL("projectile pitch never updated from velocity");

    if( !projectile_drawn )
        FAIL("render pipeline never emitted DRAW_MODEL for projectile element");

    if( draw_model_count <= 0 )
        FAIL("no TORIRSRC_DRAW_MODEL commands emitted during simulation frames");

    element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( !element )
        FAIL("projectile scene element missing after sim");

    for( int i = 0; i < 200; i++ )
    {
        time_ms += TICK_MS;
        LibToriRS_TickInput(instance, command_queue, time_ms);
        int ignored_draws = 0;
        bool ignored_drawn = false;
        render_frame(
            platform,
            renderer,
            instance,
            command_queue,
            element_id,
            &ignored_draws,
            &ignored_drawn,
            false);
    }

    element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( !element )
        FAIL("projectile scene element missing after animation sim");
    if( element->animation && element->animation->frame_count > 1 &&
        element->anim_frame == start_anim_frame )
        FAIL("projectile animation frame did not advance");

    bool despawned = false;
    for( int i = 0; i < 200 && !despawned; i++ )
    {
        time_ms += TICK_MS;
        LibToriRS_TickInput(instance, command_queue, time_ms);
        int ignored_draws = 0;
        bool ignored_drawn = false;
        render_frame(
            platform,
            renderer,
            instance,
            command_queue,
            element_id,
            &ignored_draws,
            &ignored_drawn,
            false);
        if( !projectile->alive )
            despawned = true;
    }

    if( !despawned )
        FAIL("projectile did not despawn after reaching t2");

    return 0;
}

int
main(
    int argc,
    char* argv[])
{
    char const* cache_dat_path = NULL;

    if( !chdir_to_script_root() )
        FAIL("could not locate init_runescape.lua (expected src2/programs/sdl2 working dir)");

    cache_dat_path = resolve_cache_dat_path(argc, argv);
    if( !cache_dat_path )
    {
        printf("SKIP: cache254 not found\n");
        return 0;
    }

    struct LibToriRS_Instance* instance = NULL;
    struct LibToriPlatformX_Lua* lua = NULL;
    struct LibToriPlatformX_IOReactor* io_reactor = NULL;
    struct RSCacheDat2DiskLib* cache = NULL;
    struct LibToriRS_CommandQueue* command_queue = NULL;

    instance = LibToriRS_InstanceNew();
    if( !instance )
        FAIL("LibToriRS_InstanceNew failed");

    lua = LibToriPlatformX_LuaNew(instance);
    if( !lua )
        FAIL("LibToriPlatformX_LuaNew failed");

    cache = cachelib_new(CACHE_MODE_DAT1);
    if( !cache )
        FAIL("cachelib_new failed");

    if( cachelib_platform_init(cache, cache_dat_path) != 1 )
        FAIL("cachelib_platform_init failed for %s", cache_dat_path);

    io_reactor = LibToriPlatformX_IOReactorNew(cache);
    if( !io_reactor )
        FAIL("LibToriPlatformX_IOReactorNew failed");

    command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
        FAIL("LibToriRS_CommandQueue_New failed");

    if( !run_init_script(instance, lua, io_reactor) )
        FAIL("init_runescape.lua failed");

    if( !instance->runescape || !instance->runescape->world_built )
        FAIL("world not built after init script");

    struct LibToriPlatformSDL2* platform = LibToriPlatformSDL2_New();
    if( !platform )
        FAIL("LibToriPlatformSDL2_New failed");

    int const screen_w = 800;
    int const screen_h = 600;
    int const render_w = 765;
    int const render_h = 503;

    if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
        FAIL("LibToriPlatformSDL2_InitForSoft3D failed");

    struct LibToriPlatformSDL2_RendererSoft3D* renderer_soft3d =
        LibToriPlatformSDL2_RendererSoft3D_New(render_w, render_h);
    if( !renderer_soft3d )
        FAIL("LibToriPlatformSDL2_RendererSoft3D_New failed");

    if( !LibToriPlatformSDL2_RendererSoft3D_Init(
            renderer_soft3d, LibToriPlatformSDL2_GetWindow(platform)) )
        FAIL("LibToriPlatformSDL2_RendererSoft3D_Init failed");

    SDL_SetWindowTitle(LibToriPlatformSDL2_GetWindow(platform), "runescape_world_sim");

    int rc = test_projectile_movement_and_painter(
        instance, command_queue, io_reactor, platform, renderer_soft3d);
    if( rc != 0 )
    {
        LibToriPlatformSDL2_RendererSoft3D_Free(renderer_soft3d);
        LibToriPlatformSDL2_Free(platform);
        LibToriRS_CommandQueue_Free(command_queue);
        LibToriPlatformX_IOReactorFree(io_reactor);
        cachelib_free(cache);
        LibToriPlatformX_LuaFree(lua);
        LibToriRS_InstanceFree(instance);
        return rc;
    }

    printf("PASS: runescape_world_sim\n");
    fflush(stdout);

    if( has_flag(argc, argv, "--interactive") )
    {
        struct GameRunescape* game = instance->runescape;
        int const camera_sx = game->camera_position->x / 128;
        int const camera_sz = game->camera_position->z / 128;
        (void)game_runescape_spawn_projectile(
            game,
            PROJECTILE_MODEL_ID,
            PROJECTILE_SEQ_ID,
            camera_sx,
            camera_sz,
            camera_sx + 5,
            camera_sz,
            0,
            RUNESCAPE_PROJECTILE_STARTHEIGHT,
            RUNESCAPE_PROJECTILE_ENDHEIGHT,
            RUNESCAPE_PROJECTILE_DELAY,
            RUNESCAPE_PROJECTILE_ANGLE,
            RUNESCAPE_PROJECTILE_LENGTH,
            RUNESCAPE_PROJECTILE_OFFSET,
            RUNESCAPE_PROJECTILE_STEP);

        printf("Interactive mode: WASD move, arrows rotate, close window to exit\n");
        fflush(stdout);
        run_interactive_loop(instance, command_queue, platform, renderer_soft3d);
    }

    LibToriPlatformSDL2_RendererSoft3D_Free(renderer_soft3d);
    LibToriPlatformSDL2_Free(platform);
    _exit(0);
}
