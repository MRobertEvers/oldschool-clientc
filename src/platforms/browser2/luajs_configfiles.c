#include "luajs_configfiles.h"

#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

EMSCRIPTEN_KEEPALIVE
struct LuaConfigFile*
luajs_ConfigFile_deserialize(
    const void* buffer,
    int size)
{
    if( !buffer || size < 0 )
        return NULL;

    struct LuaConfigFile* cf = (struct LuaConfigFile*)malloc(sizeof(struct LuaConfigFile));
    if( !cf )
        return NULL;
    memset(cf, 0, sizeof(*cf));

    cf->size = size;
    if( size == 0 )
    {
        cf->data = NULL;
        return cf;
    }

    cf->data = (uint8_t*)malloc((size_t)size);
    if( !cf->data )
    {
        free(cf);
        return NULL;
    }
    memcpy(cf->data, buffer, (size_t)size);
    return cf;
}

EMSCRIPTEN_KEEPALIVE
void
luajs_ConfigFile_free(struct LuaConfigFile* cf)
{
    if( !cf )
        return;
    if( cf->data )
        free(cf->data);
    free(cf);
}

