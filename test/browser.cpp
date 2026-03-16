#include "platforms/platform_impl2_emscripten_sdl2.h"
#include "tori_rs.h"

#include <emscripten.h>
#include <stdio.h>

extern "C" {
#include "platforms/browser2/luajs_sidecar.h"
}

extern "C" {
EMSCRIPTEN_KEEPALIVE
void
test_export(
    int x,
    int y)
{
    printf("C++ received: %d, %d\n", x, y);
}
}

void
signal_browser_ready()
{
    emscripten_run_script(
        "window.dispatchEvent(new CustomEvent('rs_client_ready', { "
        "detail: { timestamp: Date.now() } "
        "}));");
}

void
signal_browser_looped()
{
    emscripten_run_script(
        "window.dispatchEvent(new CustomEvent('rs_client_looped', { "
        "detail: { timestamp: Date.now() } "
        "}));");
}

void
emscripten_main_loop(void* arg)
{
    struct Platform2_Emscripten_SDL2* platform = (struct Platform2_Emscripten_SDL2*)arg;

    uint64_t timestamp_ms = SDL_GetTicks64();

    Platform2_Emscripten_SDL2_PollEvents(platform);

    Platform2_Emscripten_SDL2_RunLuaScripts(platform);

    signal_browser_looped();
}

struct LuaGameType*
luajs_sidecar_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_Emscripten_SDL2* platform = (struct Platform2_Emscripten_SDL2*)ctx;

    // Dispatch by string name.
    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_STRING);
    printf("Lua callback received command gametype\n");
    int len = LuaGameType_GetStringLength(command_gametype);
    printf("Command length: %d\n", len);
    const char* command = LuaGameType_GetString(command_gametype);
    for( int i = 0; i < 10; i++ )
    {
        printf("%c", (uint8_t)command[i]);
    }
    printf("\n");
    printf("Command: %s\n", command);

    return LuaGameType_NewVoid();
}

int
main(
    int argc,
    char* argv[])
{
    struct Platform2_Emscripten_SDL2* platform = Platform2_Emscripten_SDL2_New();
    platform->secret = 5;

    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    Platform2_Emscripten_SDL2_InitForSoft3D(platform, 1024, 768);

    luajs_sidecar_set_callback(luajs_sidecar_callback, platform);

    signal_browser_ready();

    emscripten_set_main_loop_arg(emscripten_main_loop, platform, 0, 1);

    return 0;
}
