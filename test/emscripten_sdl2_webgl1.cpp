extern "C" {
#include "libgame.h"
#include "libgame.u.h"
#include "libinput.h"
}
#include "platforms/platform_impl_emscripten_sdl2.h"
#include "platforms/platform_impl_emscripten_sdl2_renderer_webgl1.h"

#include <emscripten.h>
#include <stdio.h>

// Global state for Emscripten main loop
struct GameState
{
    struct Platform* platform;
    struct RendererEmscripten_SDL2WebGL1* renderer;
    struct Game* game;
    struct GameInput* input;
    struct GameGfxOpList* gfx_op_list;
};

static struct GameState* g_game_state = nullptr;

// Main loop callback for Emscripten
void
emscripten_main_loop()
{
    if( !g_game_state )
        return;

    PlatformImpl_Emscripten_SDL2_PollEvents(g_game_state->platform, g_game_state->input);

    game_step_main_loop(g_game_state->game, g_game_state->input, g_game_state->gfx_op_list);

    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Render(
        g_game_state->renderer, g_game_state->game, g_game_state->gfx_op_list);
}

int
main(int argc, char* argv[])
{
    printf("===================================\n");
    printf("Emscripten WebGL1 / OpenGL ES 2.0\n");
    printf("===================================\n");

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

    if( !PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_InitSoft3D(
            renderer, 1600, 900, "soft3d-canvas") )
    {
        printf("Failed to initialize soft3d renderer\n");
        return 1;
    }

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

    struct GameInput* input = GameInput_New();
    if( !input )
    {
        printf("Failed to create input\n");
        return 1;
    }

    // Set up global state for Emscripten main loop
    g_game_state = new GameState();
    g_game_state->platform = platform;
    g_game_state->renderer = renderer;
    g_game_state->game = game;
    g_game_state->input = input;
    g_game_state->gfx_op_list = gfx_op_list;

    printf("Starting Emscripten main loop...\n");

    // Start the Emscripten main loop
    // 0 = run at browser's refresh rate, 1 = simulate infinite loop
    emscripten_set_main_loop(emscripten_main_loop, 0, 1);

    // Cleanup (will only run if main loop exits, which shouldn't happen)
    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Shutdown(renderer);
    PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Free(renderer);
    PlatformImpl_Emscripten_SDL2_Shutdown(platform);
    game_free(game);
    GameInput_Free(input);
    game_gfx_op_list_free(gfx_op_list);
    delete g_game_state;

    return 0;
}
