#include "../../commands/libtorirs_command_queue.h"
#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "../../platforms/platform_js_capi.h"
#include "../../platforms/platform_sdl2/platform_sdl2.h"
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_webgl1.h"
#include "../../scripting/libtorirs_scripting.h"

#include <SDL.h>
#include <assert.h>
#include <emscripten.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

enum BrowserMainLoopState
{
    BROWSER_MAIN_LOOP_STATE_FRAME,
    BROWSER_MAIN_LOOP_STATE_WAITING_FOR_LUA
};

static struct LibToriRS_Instance* g_instance;
static struct LibToriPlatformSDL2* g_platform;
static struct LibToriPlatformSDL2_RendererWebGL1* g_renderer;
static struct LibToriRS_CommandQueue* g_command_queue;
static enum BrowserMainLoopState g_state = BROWSER_MAIN_LOOP_STATE_FRAME;

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_BrowserMainUnlock(void)
{
    assert(g_state == BROWSER_MAIN_LOOP_STATE_WAITING_FOR_LUA);
    g_state = BROWSER_MAIN_LOOP_STATE_FRAME;
}

void
browser_main_loop(void)
{
    switch( g_state )
    {
    case BROWSER_MAIN_LOOP_STATE_FRAME:
    {
        uint64_t time = SDL_GetTicks64();

        LibToriPlatformSDL2_PollEvents(g_platform, g_command_queue);
        LibToriRS_TickInput(g_instance, g_command_queue, time);

        if( !LibToriRS_IsRunning(g_instance) )
        {
            emscripten_cancel_main_loop();
            return;
        }

        if( !LibToriRS_ScriptQueueIsEmpty(LibToriRS_GetScriptQueue(g_instance)) )
        {
            g_state = BROWSER_MAIN_LOOP_STATE_WAITING_FOR_LUA;
            LibToriPlatformJS_CAPI_EmscriptenHost_LuaMainLoop();
            return;
        }
        LibToriRS_IOQueueClear(LibToriRS_GetIOQueue(g_instance));
        LibToriRS_ScriptQueueClear(LibToriRS_GetScriptQueue(g_instance));

        LibToriPlatformSDL2_RendererWebGL1_Render(g_renderer, g_instance);

        break;
    }
    case BROWSER_MAIN_LOOP_STATE_WAITING_FOR_LUA:
        break;
    }
}

int
main(
    int argc,
    char* argv[])
{
    bool const use_soft3d = has_flag(argc, argv, "--soft3d");
    bool const use_runescape = has_flag(argc, argv, "--runescape");
    if( use_soft3d )
        printf("Warning: --soft3d is not supported in the browser build (WebGL1 only)\n");
    if( use_runescape )
        printf("Game: runescape world\n");

    struct LibToriRS_Instance* instance = LibToriRS_InstanceNew();
    if( !instance )
    {
        printf("Failed to create instance\n");
        return 1;
    }

    struct LibToriPlatformSDL2* platform = LibToriPlatformSDL2_New();
    if( !platform )
    {
        printf("Failed to create SDL2 platform\n");
        return 1;
    }

    const int screen_w = 800;
    const int screen_h = 600;
    // if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
    // {
    //     printf("Failed to init SDL2 window\n");
    //     return 1;
    // }
    if( !LibToriPlatformSDL2_InitForWebGL1(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 window\n");
        return 1;
    }

    struct LibToriPlatformSDL2_RendererWebGL1* renderer_webgl1 =
        LibToriPlatformSDL2_RendererWebGL1_New(screen_w, screen_h);
    if( !renderer_webgl1 )
    {
        printf("Failed to create WebGL1 renderer\n");
        return 1;
    }
    if( !LibToriPlatformSDL2_RendererWebGL1_Init(
            renderer_webgl1, LibToriPlatformSDL2_GetWindow(platform)) )
    {
        printf("Failed to init WebGL1 renderer\n");
        return 1;
    }

    struct LibToriRS_CommandQueue* command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
    {
        printf("Failed to create command queue\n");
        return 1;
    }

    char const* init_script = use_runescape ? "init_runescape.lua" : "init.lua";
    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(LibToriRS_GetScriptQueue(instance), init_script);
    if( !script )
    {
        printf("Failed to queue %s\n", init_script);
        return 1;
    }
    script->is_inline = false;

    uint64_t time = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time);

    LibToriPlatformJS_CAPI_InitializeEmscriptenHost(instance);

    g_instance = instance;
    g_platform = platform;
    g_renderer = renderer_webgl1;
    g_command_queue = command_queue;

    emscripten_set_main_loop(browser_main_loop, 0, 0);

    return 0;
}
