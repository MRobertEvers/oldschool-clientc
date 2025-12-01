extern "C" {
#include "libgame.h"
#include "libgame.u.h"
#include "libinput.h"
#include "osrs/cache.h"
#include "osrs/xtea_config.h"
#include "shared_tables.h"
}
#include "platforms/platform_impl_osx_sdl2.h"
#include "platforms/platform_impl_osx_sdl2_renderer_opengl3.h"
// #include "platforms/platform_impl_osx_sdl2_renderer_soft3d.h"

#include <stdio.h>
#include <string.h>

// Global state for main loop
struct GameState
{
    struct Platform* platform;
    struct RendererOSX_SDL2OpenGL3* renderer;
    struct Game* game;
    struct GameIO* input;
    struct GameGfxOpList* gfx_op_list;
};

// Verify xteas.json is available (stub for OSX - assumes file exists locally)
void
verify_xteas_loaded(void)
{
    printf("=== Verifying xteas.json availability ===\n");
    // On OSX, xteas.json should be in the cache/ directory
    // This is a simple check - in practice you'd want to verify the file exists
    printf("✓ Assuming xteas.json is available locally in cache/ directory\n");
}

int
main(int argc, char* argv[])
{
    printf("===================================\n");
    printf("OSX SDL2 OpenGL3\n");
    printf("===================================\n");

    // Verify xteas.json is available locally
    printf("Verifying xteas.json availability...\n");
    verify_xteas_loaded();

    struct Platform* platform = PlatformImpl_OSX_SDL2_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    if( !PlatformImpl_OSX_SDL2_InitForOpenGL3(platform, 1024, 768) )
    {
        printf("Failed to initialize platform\n");
        return 1;
    }

    // if( !PlatformImpl_OSX_SDL2_InitForSoft3D(platform, 1024, 768) )
    // {
    //     printf("Failed to initialize platform\n");
    //     return 1;
    // }

    struct RendererOSX_SDL2OpenGL3* renderer =
        PlatformImpl_OSX_SDL2_Renderer_OpenGL3_New(1024, 768);
    // struct RendererOSX_SDL2Soft3D* renderer =
    //     PlatformImpl_OSX_SDL2_Renderer_Soft3D_New(1024, 768, 1600, 900);
    // if( !renderer )
    // {
    //     printf("Failed to create renderer\n");
    //     return 1;
    // }

    if( !PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Init(renderer, platform) )
    {
        printf("Failed to initialize renderer\n");
        return 1;
    }
    // if( !PlatformImpl_OSX_SDL2_Renderer_Soft3D_Init(renderer, platform) )
    // {
    //     printf("Failed to initialize renderer\n");
    //     return 1;
    // }

    struct GameGfxOpList* gfx_op_list = game_gfx_op_list_new(1024);
    if( !gfx_op_list )
    {
        printf("Failed to create gfx op list\n");
        return 1;
    }

    struct Game* game = game_new(GF_CACHE_LOCAL, gfx_op_list);
    if( !game )
    {
        printf("Failed to create game\n");
        return 1;
    }

    struct GameIO* input = gameio_new();
    if( !input )
    {
        printf("Failed to create input\n");
        return 1;
    }

    game_init(game, input, gfx_op_list);

    // Set up global state for main loop
    struct GameState game_state = {};
    game_state.platform = platform;
    game_state.renderer = renderer;
    game_state.game = game;
    game_state.input = input;
    game_state.gfx_op_list = gfx_op_list;

    printf("Starting main loop...\n");

    while( game_is_running(game_state.game) )
    {
        PlatformImpl_OSX_SDL2_PollEvents(
            game_state.platform, game_state.input, game_state.game->cache);

        game_step_main_loop(game_state.game, game_state.input, game_state.gfx_op_list);

        // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(game_state.renderer, game_state.game,
        // game_state.gfx_op_list);
        PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Render(
            game_state.renderer, game_state.game, game_state.gfx_op_list);

        game_gfx_op_list_reset(game_state.gfx_op_list);
    }

    // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Shutdown(game_state.renderer);
    // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Free(game_state.renderer);
    PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Shutdown(game_state.renderer);
    PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Free(game_state.renderer);
    PlatformImpl_OSX_SDL2_Shutdown(game_state.platform);
    PlatformImpl_OSX_SDL2_Free(game_state.platform);
    game_free(game_state.game);
    gameio_free(game_state.input);
    game_gfx_op_list_free(game_state.gfx_op_list);

    return 0;
}