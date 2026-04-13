#include "platforms/common/platform_memory.h"
#include "platforms/platform_impl2_emscripten_native.h"
#include "platforms/platform_impl2_emscripten_native_renderer_soft3d.h"
#include "tori_rs.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/lua_sidecar/lua_buildcachedat.h"
#include "osrs/lua_sidecar/lua_sidecar_misc.h"
#include "osrs/lua_sidecar/lua_dash.h"
#include "osrs/lua_sidecar/lua_game.h"
#include "osrs/lua_sidecar/lua_ui.h"
#include "platforms/browser2/luajs_sidecar.h"
}

#include <emscripten/bind.h>

using namespace emscripten;

// clang-format off
EMSCRIPTEN_BINDINGS(painters_module_native) {
    class_<PaintersElementCommand>("PaintersElementCommand")
        .constructor<>()

        .property("_packed", &PaintersElementCommand::_packed)

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

static struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer = NULL;

void
emscripten_main_loop_native(void* arg)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)arg;

    if( !platform->canvas_ready )
        return;

    LibToriRS_NetPump(platform->current_game);

    Platform2_Emscripten_Native_PollEvents(platform);

    Platform2_Emscripten_Native_RunLuaScripts(platform);

    LibToriRS_GameStep(platform->current_game, platform->input, platform->render_command_buffer);

    if( !renderer )
        return;
    PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Render(
        renderer, platform->current_game, platform->render_command_buffer);

    signal_browser_looped();
}

struct LuaGameType*
luajs_sidecar_callback_native(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)ctx;

    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_STRING);
    const char* command = LuaGameType_GetString(command_gametype);

    struct LuaGameType* result = NULL;
    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);

    if( LuaBuildCacheDat_CommandHasPrefix((char*)command) )
    {
        result = LuaBuildCacheDat_DispatchCommand(
            bcd, platform->current_game, (char*)command, args_view);
    }
    else if( LuaDash_CommandHasPrefix((char*)command) )
    {
        result = LuaDash_DispatchCommand(
            platform->current_game->sys_dash,
            bcd,
            platform->current_game,
            (char*)command,
            args_view);
    }
    else if( LuaGame_CommandHasPrefix((char*)command) )
    {
        result = LuaGame_DispatchCommand(platform->current_game, (char*)command, args_view);
    }
    else if( LuaUI_CommandHasPrefix((char*)command) )
    {
        result = LuaUI_DispatchCommand(platform->current_game, bcd, (char*)command, args_view);
    }
    else if( LuaSidecarMisc_CommandHasPrefix((char*)command) )
    {
        result = LuaSidecarMisc_DispatchCommand(
            platform->current_game, (char*)command, args_view);
    }

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
    (void)argc;
    (void)argv;

    struct PlatformMemoryInfo platform_memory_info = { 0 };

    printf("World size: %zu\n", sizeof(struct World));
    platform_get_memory_info(&platform_memory_info);

    printf(
        "Pre-Platform_Native_New: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    struct Platform2_Emscripten_Native* platform = Platform2_Emscripten_Native_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    const int graphics3d_width = 513;
    const int graphics3d_height = 335;
    const int game_width = 765;
    const int game_height = 503;
    const int render_max_width = game_width;
    const int render_max_height = game_height;

    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();

    struct GGame* game = LibToriRS_GameNew(net_shared, graphics3d_width, graphics3d_height);
    set_game_ptr(game);

    struct ToriRSRenderCommandBuffer* render_command_buffer = LibToriRS_RenderCommandBufferNew(10);
    platform->render_command_buffer = render_command_buffer;
    platform->current_game = game;
    platform->secret = 5;

    platform_get_memory_info(&platform_memory_info);
    printf(
        "Pre-InitForSoft3D: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    if( !Platform2_Emscripten_Native_InitForSoft3D(platform, game_width, game_height) )
    {
        printf("Failed to initialize native canvas for Soft3D\n");
        return 1;
    }

    platform_get_memory_info(&platform_memory_info);
    printf(
        "Pre-Renderer_Soft3D_New: Platform memory info: %zu / %zu / %zu\n",
        platform_memory_info.heap_used,
        platform_memory_info.heap_total,
        platform_memory_info.heap_peak);

    renderer = PlatformImpl2_Emscripten_Native_Renderer_Soft3D_New(
        game_width, game_height, render_max_width, render_max_height);
    if( !renderer || !PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Init(renderer, platform) )
    {
        printf("Soft3D native renderer init failed\n");
        return 1;
    }
    printf("Soft3D native renderer init succeeded\n");

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

    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;
    PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetDashOffset(renderer, 4, 4);
    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;
    renderer->clicked_tile_level = -1;

    luajs_sidecar_set_callback(luajs_sidecar_callback_native, platform);

    const char* host = "127.0.0.1:80";
    LibToriRS_NetConnectLogin(game, host, "asdf2", "a");
    signal_browser_ready();

    emscripten_set_main_loop_arg(emscripten_main_loop_native, platform, 0, 1);

    return 0;
}
