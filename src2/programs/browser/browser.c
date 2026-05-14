#include "../../libtorirs.h"
#include "../../scripting/libtorirs_scripting.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>

enum BrowserMainLoopState
{
    BROWSER_MAIN_LOOP_STATE_BEGIN,
    BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS,
    BROWSER_MAIN_LOOP_STATE_END
};

static struct LibToriRS_Instance* g_browser_instance;
static enum BrowserMainLoopState state = BROWSER_MAIN_LOOP_STATE_BEGIN;

// clang-format off
EM_JS(
    void,
    tori_browser_dispatch_lua_main,
    (void* instance_ptr),
    {
        if( typeof Module.__toriStartBrowserLuaMainLoop === "function" )
        {
            Module.__toriStartBrowserLuaMainLoop(instance_ptr);
        }
    });
// clang-format on

EMSCRIPTEN_KEEPALIVE
void
ToriPlatformEmscripten_JSHost_BrowserMainUnlock(void)
{
    assert(state == BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS);
    state = BROWSER_MAIN_LOOP_STATE_END;
}

EMSCRIPTEN_KEEPALIVE
void
ToriPlatformEmscripten_WASMHost_RunLuaScript(void)
{
    /* Lua is driven from JS via Module.__toriStartBrowserLuaMainLoop (see browser_main_loop BEGIN).
     */
}

void
browser_main_loop(void)
{
    switch( state )
    {
    case BROWSER_MAIN_LOOP_STATE_BEGIN:
        state = BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS;
        tori_browser_dispatch_lua_main(g_browser_instance);
        break;
    case BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS:
        break;
    case BROWSER_MAIN_LOOP_STATE_END:
        state = BROWSER_MAIN_LOOP_STATE_BEGIN;
        return;
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

    g_browser_instance = instance;

    struct LibToriRS_ScriptQueue* script_queue = LibToriRS_GetScriptQueue(instance);
    if( !script_queue )
    {
        printf("Failed to get script queue\n");
        return 1;
    }
    LibToriRS_ScriptQueueEmplace(
        script_queue,
        "print(\"Hello from browser!\")\nlocal result = coroutine.yield()\nprint(\"Result: \" .. "
        "result)");

    emscripten_set_main_loop(browser_main_loop, 0, 1);

    return 0;
}
