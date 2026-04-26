#include "platforms/common/platform_memory.h"
#include "platforms/platform_impl2_sdl2.h"
#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"
#include "tori_rs.h"

#include <cassert>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/lua_sidecar/lua_api.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "platforms/browser2/luajs_sidecar.h"
}

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

static Platform2_SDL2_Renderer_WebGL1* renderer = nullptr;

void
emscripten_main_loop(void* arg)
{
    struct Platform2_SDL2* platform = (struct Platform2_SDL2*)arg;

    if( !platform->window )
        return;

    LibToriRS_NetPump(platform->current_game);

    Platform2_SDL2_PollEvents(platform, platform->input, 0);

    Platform2_SDL2_RunLuaScripts(platform, platform->current_game);

    LibToriRS_GameStep(platform->current_game, platform->input, platform->render_command_buffer);

    if( !renderer )
        return;
    PlatformImpl2_SDL2_Renderer_WebGL1_Render(
        renderer, platform->current_game, platform->render_command_buffer);

    signal_browser_looped();
}

struct LuaGameType*
luajs_sidecar_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_SDL2* platform = (struct Platform2_SDL2*)ctx;

    // Dispatch by string name.
    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_INT);
    LuaApiId api_id = (LuaApiId)LuaGameType_GetInt(command_gametype);

    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);
    struct LuaGameType* result = lua_api_dispatch(
        platform->current_game,
        bcd,
        platform->current_game->sys_dash,
        api_id,
        args_view);
    LuaGameType_Free(args_view);

    if( result )
        return result;
    return LuaGameType_NewVoid();
}

int
main(
    int argc,
    char* argv[])
{
    struct PlatformMemoryInfo platform_memory_info = { 0 };

    printf("World size: %d\n", sizeof(struct World));
    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-SDL2_New: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    struct Platform2_SDL2* platform = Platform2_SDL2_New();
    // These dimensions define the internal 3D render resolution used by the game/viewport.
    const int graphics3d_width = 513;
    const int graphics3d_height = 335;
    /* Fixed game output size — iface_view_port and game_screen are locked to this.
     * All renderers letterbox this to the actual canvas/window size. */
    const int game_width = 765;
    const int game_height = 503;
    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-NetNewBuffer: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();

    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-GameNew: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    struct GGame* game = LibToriRS_GameNew(net_shared, graphics3d_width, graphics3d_height);
    set_game_ptr(game);

    struct GInput input = { 0 };
    struct ToriRSRenderCommandBuffer* render_command_buffer = LibToriRS_RenderCommandBufferNew(10);
    platform->render_command_buffer = render_command_buffer;
    platform->current_game = game;
    platform->secret = 5;

    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-InitForWebGL1: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);
    if( !Platform2_SDL2_InitForWebGL1(platform, game_width, game_height) )
    {
        printf("Failed to initialize SDL window for WebGL1\n");
        return 1;
    }

    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-Renderer_WebGL1_New: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    renderer = PlatformImpl2_SDL2_Renderer_WebGL1_New(game_width, game_height);
    renderer->width = game_width;
    renderer->height = game_height;
    if( !PlatformImpl2_SDL2_Renderer_WebGL1_Init(renderer, platform) )
    {
        printf("WebGL1 renderer init failed\n");
        return 1;
    }
    printf("WebGL1 renderer init succeeded\n");

    game->iface_view_port->width = game_width;
    game->iface_view_port->height = game_height;
    game->iface_view_port->x_center = game_width / 2;
    game->iface_view_port->y_center = game_height / 2;
    game->iface_view_port->stride = game_width;
    game->iface_view_port->clip_left = 0;
    game->iface_view_port->clip_top = 0;
    game->iface_view_port->clip_right = game_width;
    game->iface_view_port->clip_bottom = game_height;

    platform_get_memory_info(&platform_memory_info);

    printf(
        "Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);
    /* Keep viewport config identical across renderer backends (same as sdl2.cpp). */
    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;

    luajs_sidecar_set_callback(luajs_sidecar_callback, platform);

    const char* host = "127.0.0.1:80";
    LibToriRS_NetConnectLogin(game, host, "asdf2", "a");
    signal_browser_ready();

    emscripten_set_main_loop_arg(emscripten_main_loop, platform, 0, 1);

    return 0;
}
