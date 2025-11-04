extern "C" {
#include "libgame.h"
#include "libgame.u.h"
#include "libinput.h"
}
#include "platforms/platform_impl_osx_sdl2.h"
#include "platforms/platform_impl_osx_sdl2_renderer_opengl3.h"
// #include "platforms/platform_impl_osx_sdl2_renderer_soft3d.h"

#include <stdio.h>

int
main(int argc, char* argv[])
{
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

    while( game_is_running(game) )
    {
        PlatformImpl_OSX_SDL2_PollEvents(platform, input);

        game_step_main_loop(game, input, gfx_op_list);

        // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(renderer, game, gfx_op_list);
        PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Render(renderer, game, gfx_op_list);

        game_gfx_op_list_reset(gfx_op_list);
    }

    // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Shutdown(renderer);
    // PlatformImpl_OSX_SDL2_Renderer_Soft3D_Free(renderer);
    PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Shutdown(renderer);
    PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Free(renderer);
    PlatformImpl_OSX_SDL2_Shutdown(platform);
    PlatformImpl_OSX_SDL2_Free(platform);
    game_free(game);
    GameInput_Free(input);

    return 0;
}