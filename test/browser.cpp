#include "platforms/platform_impl2_emscripten_sdl2.h"
#include "platforms/platform_impl2_emscripten_sdl2_renderer_soft3d.h"
#include "platforms/platform_impl2_emscripten_sdl2_renderer_webgl1.h"
#include "tori_rs.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/lua_sidecar/lua_buildcachedat.h"
#include "osrs/lua_sidecar/lua_dash.h"
#include "osrs/lua_sidecar/lua_game.h"
#include "platforms/browser2/luajs_sidecar.h"
}

#include <emscripten/bind.h>

using namespace emscripten;

#include <emscripten/bind.h>

using namespace emscripten;

// clang-format off
EMSCRIPTEN_BINDINGS(painters_module) {
    class_<PaintersElementCommand>("PaintersElementCommand")
        .constructor<>()
        
        // Raw access to the full 32-bit word
        .property("_packed", &PaintersElementCommand::_packed)

        // Entity Bitfields
        .property("kind", 
            select_overload<uint32_t(const PaintersElementCommand& record)>(
                [](const PaintersElementCommand& record) -> uint32_t {
                    return record._entity._bf_kind;
                }),
            select_overload<void(PaintersElementCommand& record, uint32_t val)>(
                [](PaintersElementCommand& record, uint32_t val) {
                    record._entity._bf_kind = val;
                })
        )
        .property("entity_id", 
            select_overload<uint32_t(const PaintersElementCommand& record)>(
                [](const PaintersElementCommand& record) -> uint32_t {
                    return record._entity._bf_entity;
                }),
            select_overload<void(PaintersElementCommand& record, uint32_t val)>(
                [](PaintersElementCommand& record, uint32_t val) {
                    record._entity._bf_entity = val;
                })
        )

        // Terrain Bitfields
        .property("terrain_x", 
            select_overload<uint32_t(const PaintersElementCommand& record)>(
                [](const PaintersElementCommand& record) -> uint32_t {
                    return record._terrain._bf_terrain_x;
                })
        )
        .property("terrain_z", 
            select_overload<uint32_t(const PaintersElementCommand& record)>(
                [](const PaintersElementCommand& record) -> uint32_t {
                    return record._terrain._bf_terrain_z;
                })
        )
        .property("terrain_y", 
            select_overload<uint32_t(const PaintersElementCommand& record)>(
                [](const PaintersElementCommand& record) -> uint32_t {
                    return record._terrain._bf_terrain_y;
                })
        );
}
// clang-format on

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
set_game_ptr(struct GGame* game)
{
    // clang-format off
    EM_ASM({
        window.gamePtr = $0;
    }, (uintptr_t)game);
    // clang-format on
}

static struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer = NULL;
static struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer_webgl1 = NULL;
static bool g_use_webgl1 = true;

void
emscripten_main_loop(void* arg)
{
    struct Platform2_Emscripten_SDL2* platform = (struct Platform2_Emscripten_SDL2*)arg;

    if( !renderer_webgl1 || !platform->window )
        return;

    uint64_t timestamp_ms = SDL_GetTicks64();

    LibToriRS_NetPump(platform->current_game);

    Platform2_Emscripten_SDL2_PollEvents(platform);

    Platform2_Emscripten_SDL2_RunLuaScripts(platform);

    LibToriRS_GameStep(platform->current_game, platform->input, platform->render_command_buffer);

    PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Render(
        renderer_webgl1, platform->current_game, platform->render_command_buffer);

    signal_browser_looped();
}

struct LuaGameType*
luajs_sidecar_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_Emscripten_SDL2* platform = (struct Platform2_Emscripten_SDL2*)ctx;

    // LuaGameType_Print(args);

    // Dispatch by string name.
    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_STRING);
    const char* command = LuaGameType_GetString(command_gametype);

    struct LuaGameType* result = NULL;
    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);

    if( LuaBuildCacheDat_CommandHasPrefix((char*)command) )
    {
        result = LuaBuildCacheDat_DispatchCommand(bcd, (char*)command, args_view);
    }
    else if( LuaDash_CommandHasPrefix((char*)command) )
    {
        result = LuaDash_DispatchCommand(
            platform->current_game->sys_dash, bcd, (char*)command, args_view);
    }
    else if( LuaGame_CommandHasPrefix((char*)command) )
    {
        result = LuaGame_DispatchCommand(platform->current_game, (char*)command, args_view);
    }

    LuaGameType_Free(args_view);

    if( result )
        return result;
    else
        return LuaGameType_NewVoid();

    return LuaGameType_NewVoid();
}

int
main(
    int argc,
    char* argv[])
{
    struct Platform2_Emscripten_SDL2* platform = Platform2_Emscripten_SDL2_New();
    // These dimensions define the internal 3D render resolution used by the game/viewport.
    const int graphics3d_width = 513;
    const int graphics3d_height = 335;

    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net_shared, graphics3d_width, graphics3d_height);
    set_game_ptr(game);

    struct GInput input = { 0 };
    struct ToriRSRenderCommandBuffer* render_command_buffer =
        LibToriRS_RenderCommandBufferNew(1024);
    platform->render_command_buffer = render_command_buffer;
    platform->current_game = game;
    platform->secret = 5;

    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    g_use_webgl1 = true;
    if( !Platform2_Emscripten_SDL2_InitForWebGL1(platform, 1024, 768) )
    {
        printf("Failed to initialize SDL window for WebGL1\n");
        return 1;
    }

    renderer_webgl1 = PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_New(1024, 768);
    printf("renderer_webgl1: %p\n", renderer_webgl1);
    if( !renderer_webgl1 ||
        !PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Init(renderer_webgl1, platform) )
    {
        printf("WebGL1 renderer init failed\n");
        return 1;
    }
    printf("WebGL1 renderer init succeeded\n");

    luajs_sidecar_set_callback(luajs_sidecar_callback, platform);

    const char* host = "127.0.0.1:80";
    LibToriRS_NetConnectLogin(game, host, "asdf2", "a");
    signal_browser_ready();

    emscripten_set_main_loop_arg(emscripten_main_loop, platform, 0, 1);

    return 0;
}
