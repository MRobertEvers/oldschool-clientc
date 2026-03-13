#include "luajs_sidecar.h"

#include <assert.h>
#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>

EMSCRIPTEN_KEEPALIVE
void
dispatch_lua_command(
    int cmd,
    int argc,
    uint64_t* args)
{
    printf("dispatch_lua_command: cmd=%d, argc=%d\n", cmd, argc);
    // clang-format off
    int fn = EM_ASM_INT({ return window.LuaSidecarCallback || 0; });
    int ctx = EM_ASM_INT({ return window.LuaSidecarCallbackArg || 0; });
    // clang-format on

    assert(fn != 0 && ctx != 0);

    void (*callback)(void*, int, int, uint64_t*) = (void (*)(void*, int, int, uint64_t*))fn;
    callback((void*)ctx, cmd, argc, args);
}

// TODO: This should be tied to the wasm module.
void
luajs_sidecar_set_callback(
    void (*callback)(
        void*,
        int,
        int,
        uint64_t*),
    void* ctx)
{
    // clang-format off
    EM_ASM({
        window.LuaSidecarCallback = $0;
        window.LuaSidecarCallbackArg = $1;
    }, callback, ctx);
    // clang-format on
}
