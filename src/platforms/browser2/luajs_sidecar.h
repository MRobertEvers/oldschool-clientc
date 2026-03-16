#ifndef LUAJS_SIDECAR_H
#define LUAJS_SIDECAR_H

#include "src/osrs/lua_sidecar/lua_gametypes.h"

#include <emscripten.h>
#include <stdint.h>

void
luajs_sidecar_set_callback(
    struct LuaGameType* (*callback)(
        void*,
        int,
        int,
        uint64_t*),
    void* ctx);

#endif