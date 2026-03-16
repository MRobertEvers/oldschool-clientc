#ifndef LUAJS_SIDECAR_H
#define LUAJS_SIDECAR_H

#include "osrs/lua_sidecar/lua_gametypes.h"

#include <emscripten.h>
#include <stdint.h>

void
luajs_sidecar_set_callback(
    struct LuaGameType* (*callback)(
        void*,
        struct LuaGameType*),
    void* ctx);

#endif