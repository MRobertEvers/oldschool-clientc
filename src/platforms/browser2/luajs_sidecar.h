#ifndef LUAJS_SIDECAR_H
#define LUAJS_SIDECAR_H

#include <emscripten.h>
#include <stdint.h>

void
luajs_sidecar_set_callback(
    void (*callback)(
        void*,
        int,
        int,
        uint64_t*),
    void* ctx);

#endif