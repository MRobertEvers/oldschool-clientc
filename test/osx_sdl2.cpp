extern "C" {
#include "libgame.h"
#include "libgame.u.h"
#include "libinput.h"
}
#include "platforms/platform_impl_osx_sdl2.h"
#include "platforms/platform_impl_osx_sdl2_renderer_soft3d.h"

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

    if( !PlatformImpl_OSX_SDL2_InitForSoft3D(platform, 1024, 768) )
    {
        printf("Failed to initialize platform\n");
        return 1;
    }

    struct Renderer* renderer = PlatformImpl_OSX_SDL2_Renderer_Soft3D_New(1024, 768);
    if( !renderer )
    {
        printf("Failed to create renderer\n");
        return 1;
    }

    if( !PlatformImpl_OSX_SDL2_Renderer_Soft3D_Init(renderer, platform) )
    {
        printf("Failed to initialize renderer\n");
        return 1;
    }

    struct Game* game = game_new();
    if( !game )
    {
        printf("Failed to create game\n");
        return 1;
    }
    if( !game_init(game) )
    {
        printf("Failed to initialize game\n");
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

        game_process_input(game, input);

        game_step_main_loop(game);

        PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(renderer, game);
    }

    return 0;
}