#include "luajs_sidecar.h"

#include <assert.h>
#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
dispatch_lua_command(struct LuaGameType* args)
{
    // printf("dispatch_lua_command\n");
    // clang-format off
    int fn = EM_ASM_INT({ return window.LuaSidecarCallback || 0; });
    int ctx = EM_ASM_INT({ return window.LuaSidecarCallbackArg || 0; });
    // clang-format on

    assert(fn != 0 && ctx != 0);

    struct LuaGameType* (*callback)(void*, struct LuaGameType*) =
        (struct LuaGameType * (*)(void*, struct LuaGameType*)) fn;
    struct LuaGameType* result = callback((void*)ctx, args);

    return result;
}

// TODO: This should be tied to the wasm module.
void
luajs_sidecar_set_callback(
    struct LuaGameType* (*callback)(
        void*,
        struct LuaGameType*),
    void* ctx)
{
    // clang-format off
    EM_ASM({
        window.LuaSidecarCallback = $0;
        window.LuaSidecarCallbackArg = $1;
    }, callback, ctx);
    // clang-format on
}
