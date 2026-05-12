#ifndef LUAJS_CACHEIO_H
#define LUAJS_CACHEIO_H

#include <stdint.h>

#include "osrs/lua_sidecar/lua_cacheio.h"
#include "platforms/browser2/luajs_emscripten_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Flatten DAT1 requests to int32 triplets for the datserver JSON path. Caller must `_free` the pointer. */
EMSCRIPTEN_KEEPALIVE
int32_t*
luajs_LuaCacheIO_flatten_dat1_triplets(
    struct LuaCacheIORequestList* list,
    int* out_triplet_count);

#ifdef __cplusplus
}
#endif

#endif
