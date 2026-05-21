#include "../../commands/libtorirs_command_queue.h"
#include "../../libtorirs.h"
#include "../../platforms/platform_js_capi.h"
#include "../../platforms/platform_sdl2.h"
#include "../../scripting/libtorirs_scripting.h"

#include <SDL.h>
#include <assert.h>
#include <emscripten.h>
#include <stdio.h>

enum BrowserMainLoopState
{
    BROWSER_MAIN_LOOP_STATE_FRAME,
    BROWSER_MAIN_LOOP_STATE_WAITING_FOR_LUA
};

static struct LibToriRS_Instance* g_instance;
static struct LibToriPlatformSDL2* g_platform;
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
        }

        LibToriRS_GameStep(g_instance);

        LibToriPlatformSDL2_Render(g_platform, g_instance, 0);
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
    (void)argc;
    (void)argv;

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
    if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 window\n");
        return 1;
    }

    struct LibToriRS_CommandQueue* command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
    {
        printf("Failed to create command queue\n");
        return 1;
    }

    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(LibToriRS_GetScriptQueue(instance), "init.lua");
    if( !script )
    {
        printf("Failed to queue init.lua\n");
        return 1;
    }
    script->is_inline = false;

    uint64_t time = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time);

    LibToriPlatformJS_CAPI_InitializeEmscriptenHost(instance);

    g_instance = instance;
    g_platform = platform;
    g_command_queue = command_queue;

    emscripten_set_main_loop(browser_main_loop, 0, 1);

    return 0;
}
