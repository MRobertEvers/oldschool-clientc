extern "C" {
#include "libgame.h"
#include "libgame.u.h"
#include "libinput.h"
#include "osrs/cache.h"
#include "osrs/xtea_config.h"
#include "shared_tables.h"
}
#include "platforms/platform_impl_emscripten_sdl2.h"
#include "platforms/platform_impl_emscripten_sdl2_renderer_webgl1.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

// Global state for Emscripten main loop
struct GameState
{
    struct Platform* platform;
    struct RendererEmscripten_SDL2WebGL1* renderer;
    struct Game* game;
    struct GameIO* input;
    struct GameGfxOpList* gfx_op_list;
};

// Verify xteas.json is in virtual filesystem (preloaded via --preload-file)
// clang-format off
EM_JS(void, verify_xteas_loaded, (), {
    console.log("=== Verifying xteas.json in virtual filesystem ===");
    try {
        const stat = FS.stat("/cache/xteas.json");
        console.log("✓ xteas.json found in virtual FS - size:", stat.size, "bytes");
    } catch (err) {
        console.error("✗ xteas.json NOT found in virtual FS:", err);
        console.error("  Make sure the build includes: --preload-file cache/xteas.json@/cache/xteas.json");
    }
});
// clang-format on

// Main loop callback for Emscripten
void
emscripten_main_loop(void* arg)
{
    struct GameState* game_state = (struct GameState*)arg;

    if( !game_state )
        return;

    PlatformImpl_Emscripten_SDL2_PollEvents(game_state->platform, game_state->input);

    game_step_main_loop(game_state->game, game_state->input, game_state->gfx_op_list);

    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Render(
        game_state->renderer, game_state->game, game_state->gfx_op_list);
}

int
main(int argc, char* argv[])
{
    printf("===================================\n");
    printf("Emscripten WebGL1 / OpenGL ES 2.0\n");
    printf("===================================\n");

    // Verify xteas.json is in virtual filesystem
    // xteas.json is preloaded at build time via --preload-file (included in .data file)
    // This is a small file (~few KB) that contains XTEA encryption keys
    //
    // REQUIRED SETUP FOR WEBSOCKET CACHE CONNECTION:
    // 1. Start the asset_server (TCP server on port 4949):
    //    $ ./build/asset_server
    //
    // 2. Start websockify to bridge WebSocket (8080) to TCP (4949):
    //    $ ./scripts/start_websockify_bridge.sh
    //    (or manually: python -m websockify 8080 localhost:4949)
    //
    // 3. Serve the app:
    //    $ ./scripts/serve_emscripten.py
    //    (or: cd build.em && python3 -m http.server 8000)
    //
    // With ASYNCIFY enabled, Emscripten transforms blocking POSIX socket calls
    // (connect, send, recv) into async operations using WebSocket.
    // The websockify bridge forwards WebSocket connections to the TCP asset_server.
    // Cache files are loaded on-demand via WebSocket (NOT preloaded in .data file).
    printf("Verifying xteas.json in virtual filesystem...\n");
    verify_xteas_loaded();

    struct Platform* platform = PlatformImpl_Emscripten_SDL2_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    // Initialize for WebGL1 - note: InitForSoft3D actually just initializes SDL2
    if( !PlatformImpl_Emscripten_SDL2_InitForWebGL1(platform, 1024, 768) )
    {
        printf("Failed to initialize platform\n");
        return 1;
    }

    struct RendererEmscripten_SDL2WebGL1* renderer =
        PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_New(1024, 768);
    if( !renderer )
    {
        printf("Failed to create renderer\n");
        return 1;
    }

    if( !PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Init(renderer, platform) )
    {
        printf("Failed to initialize renderer\n");
        return 1;
    }

    // if( !PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_InitSoft3D(
    //         renderer, 1600, 900, "soft3d-canvas") )
    // {
    //     printf("Failed to initialize soft3d renderer\n");
    //     return 1;
    // }

    struct GameGfxOpList* gfx_op_list = game_gfx_op_list_new(1024);
    if( !gfx_op_list )
    {
        printf("Failed to create gfx op list\n");
        return 1;
    }

    struct Game* game = game_new(0, gfx_op_list);
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

    game_init(game, input);

    // Set up global state for Emscripten main loop
    struct GameState* game_state = new GameState();
    game_state->platform = platform;
    game_state->renderer = renderer;
    game_state->game = game;
    game_state->input = input;
    game_state->gfx_op_list = gfx_op_list;

    printf("Starting Emscripten main loop...\n");

    // Start the Emscripten main loop
    // 0 = run at browser's refresh rate, 1 = simulate infinite loop
    emscripten_set_main_loop_arg(emscripten_main_loop, game_state, 0, 1);

    // Cleanup (will only run if main loop exits, which shouldn't happen)
    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Shutdown(renderer);
    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Free(renderer);
    PlatformImpl_Emscripten_SDL2_Shutdown(platform);
    game_free(game);
    gameio_free(input);
    game_gfx_op_list_free(gfx_op_list);
    delete game_state;

    return 0;
}
