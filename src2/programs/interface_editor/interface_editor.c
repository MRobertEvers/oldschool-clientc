#include "../../libtorirs.h"
#include "../../libtorirs_internal.h"
#include "../../commands/libtorirs_command_queue.h"
#include "../../games/interface_editor.h"
#include "../../platforms/platform_sdl2/platform_sdl2.h"
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_soft3d.h"
#include "../../platforms/platform_x/cachelib.h"
#include "../../platforms/platform_x/cachelib_platform.h"
#include "../../platforms/platform_x/cache_path_resolve.h"
#include "../../scripting/libtorirs_scriptapi.h"
#include "../../toriauxlib/c/toriauxlibcache.h"
#include "../../toriauxlib/toriauxlib.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool
argv_is_cache_path_candidate(int argc, char* argv[], int i)
{
    if( i < 1 || i >= argc || !argv[i] || argv[i][0] == '\0' || argv[i][0] == '-' )
        return false;
    if( i > 1 && strcmp(argv[i - 1], "--verify") == 0 )
        return false;
    return true;
}

static char const*
resolve_cache_dat_path(int argc, char* argv[])
{
    char const* xrsps = cache_path_resolve_xrsps_osrs237();
    char const* osrs = cache_path_resolve_osrs_repo();

    for( int i = 1; i < argc; i++ )
    {
        if( !argv_is_cache_path_candidate(argc, argv, i) )
            continue;
        if( !cache_path_has_dat2(argv[i]) )
        {
            fprintf(
                stderr,
                "Warning: cache path %s has no main_file_cache.dat2; using auto-resolve\n",
                argv[i]);
            break;
        }
        if( xrsps && osrs && cache_path_same_dir(argv[i], osrs) )
        {
            fprintf(
                stderr,
                "Warning: 3draster/cache is an older revision (CS2 scripts won't decode); "
                "using xrsps rev-237 cache instead\n");
            return xrsps;
        }
        return argv[i];
    }

    /* Prefer xrsps-typescript rev-237 (parity / TS interface editor canonical cache). */
    if( xrsps )
        return xrsps;

    if( osrs )
        return osrs;

    static char const* const candidates[] = {
        "cache",
        "../cache",
        "../../cache",
        "../../../cache",
        "../../../../cache",
        "../../../../../cache",
        NULL,
    };
    char dat_path[512];
    for( int i = 0; candidates[i]; i++ )
    {
        snprintf(dat_path, sizeof(dat_path), "%s/main_file_cache.dat2", candidates[i]);
        FILE* f = fopen(dat_path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

static int
parse_verify_interface_id(int argc, char* argv[])
{
    for( int i = 1; i + 1 < argc; i++ )
    {
        if( strcmp(argv[i], "--verify") == 0 )
            return atoi(argv[i + 1]);
    }
    return -1;
}

static int
parse_cs2_trailer_flag(int argc, char* argv[])
{
    for( int i = 1; i + 1 < argc; i++ )
    {
        if( strcmp(argv[i], "--cs2-trailer") == 0 )
        {
            if( strcmp(argv[i + 1], "legacy") == 0 )
                return CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
            return CLIENTSCRIPT_DECODE_TRAILER_MODERN;
        }
    }
    return CLIENTSCRIPT_DECODE_TRAILER_MODERN;
}

int
main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    struct RSCacheDat2DiskLib* cache = NULL;
    struct LibToriPlatformSDL2* platform = NULL;
    struct LibToriPlatformSDL2_RendererSoft3D* renderer = NULL;
    struct LibToriRS_CommandQueue* command_queue = NULL;
    struct LibToriRS_Instance* instance = NULL;

    char const* cache_dat_path = resolve_cache_dat_path(argc, argv);
    if( !cache_dat_path )
    {
        fprintf(
            stderr,
            "Failed to find cache directory (expected main_file_cache.dat2). "
            "Pass the cache path as an argument.\n");
        return 1;
    }

    printf("Interface Editor\n");
    printf("Using cache: %s\n", cache_dat_path);

    instance = LibToriRS_InstanceNewWithCacheMode((int)TORIAUXLIBCACHE_MODE_DAT2);
    if( !instance )
    {
        fprintf(stderr, "Failed to create instance\n");
        return 1;
    }

    cache = cachelib_new(CACHE_MODE_DAT2);
    if( !cache || cachelib_platform_init(cache, cache_dat_path) != 1 )
    {
        fprintf(stderr, "Failed to init cache\n");
        goto error_exit;
    }

    struct RSCacheDat2Disk* dat2_disk = cachelib_dat2_disk(cache);
    if( dat2_disk )
        ToriAuxLibCache_SetDat2Disk(ToriAuxLib_C(LibToriRS_GetToriAuxLib(instance)), dat2_disk);

    LibToriRS_ScriptAPI_Game_InterfaceEditor_Init(instance);
    if( !instance->interface_editor )
    {
        fprintf(stderr, "Failed to init interface editor game\n");
        goto error_exit;
    }

    GameInterfaceEditor_SetDat2Cache(instance->interface_editor, dat2_disk);
    int const cs2_trailer_flags = parse_cs2_trailer_flag(argc, argv);
    GameInterfaceEditor_SetClientscriptDecodeFlags(instance->interface_editor, cs2_trailer_flags);
    ToriAuxLibCache_SetClientscriptDecodeFlags(
        ToriAuxLib_C(LibToriRS_GetToriAuxLib(instance)), cs2_trailer_flags);
    if( !GameInterfaceEditor_EnumerateInterfaceGroups(instance->interface_editor) )
        fprintf(stderr, "Warning: no interface groups found in cache\n");

    int const verify_group_id = parse_verify_interface_id(argc, argv);
    if( verify_group_id >= 0 )
    {
        if( !GameInterfaceEditor_LoadGroup(instance->interface_editor, verify_group_id) )
        {
            fprintf(stderr, "verify: failed to load interface %d\n", verify_group_id);
            goto error_exit;
        }
        GameInterfaceEditor_RunScripts(instance->interface_editor);
        int const dynamic_count =
            GameInterfaceEditor_CountDynamicCs2Children(instance->interface_editor);
        printf(
            "verify: interface %d loaded, dynamic_cs2_children=%d\n",
            verify_group_id,
            dynamic_count);
        GameInterfaceEditor_PrintDynamicCs2Children(instance->interface_editor);
        fflush(stdout);
        fflush(stderr);
        int const verify_rc = dynamic_count > 0 ? 0 : 2;
        _exit(verify_rc);
    }

    platform = LibToriPlatformSDL2_New();
    if( !platform )
        goto error_exit;

    if( !LibToriPlatformSDL2_InitForSoft3D(platform, IE_WINDOW_W, IE_WINDOW_H) )
        goto error_exit;

    renderer = LibToriPlatformSDL2_RendererSoft3D_New(IE_WINDOW_W, IE_WINDOW_H);
    if( !renderer ||
        !LibToriPlatformSDL2_RendererSoft3D_Init(
            renderer, LibToriPlatformSDL2_GetWindow(platform)) )
        goto error_exit;

    command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
        goto error_exit;

    uint64_t time = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time);

    while( LibToriRS_IsRunning(instance) )
    {
        time = SDL_GetTicks64();
        LibToriPlatformSDL2_PollEvents(platform, command_queue);
        LibToriRS_TickInput(instance, command_queue, time);
        if( !LibToriRS_IsRunning(instance) )
            break;
        LibToriPlatformSDL2_RendererSoft3D_Render(renderer, instance, NULL);
        SDL_Delay(1);
    }

    LibToriRS_CommandQueue_Free(command_queue);
    LibToriPlatformSDL2_RendererSoft3D_Free(renderer);
    LibToriPlatformSDL2_Free(platform);
    cachelib_free(cache);
    LibToriRS_InstanceFree(instance);
    return 0;

error_exit:
    if( command_queue )
        LibToriRS_CommandQueue_Free(command_queue);
    if( renderer )
        LibToriPlatformSDL2_RendererSoft3D_Free(renderer);
    if( platform )
        LibToriPlatformSDL2_Free(platform);
    if( cache )
        cachelib_free(cache);
    if( instance )
        LibToriRS_InstanceFree(instance);
    return 1;
}
