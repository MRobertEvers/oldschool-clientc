#include "luajs_configs.h"
#include "luajs_emscripten_compat.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void
write_int(
    uint8_t* p,
    int value)
{
    uint32_t v = (uint32_t)value;
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int
read_int(const uint8_t* p)
{
    uint32_t v = 0;
    v |= (uint32_t)p[0];
    v |= (uint32_t)p[1] << 8;
    v |= (uint32_t)p[2] << 16;
    v |= (uint32_t)p[3] << 24;
    return (int)v;
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaConfigFile_serialized_size(const struct LuaConfigFile* cf)
{
    if( !cf )
        return -1;
    int data_size = cf->size >= 0 ? cf->size : 0;
    return LUAJS_CONFIG_HEADER_SIZE + data_size;
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaConfigFile_serialize_to_buffer(
    const struct LuaConfigFile* cf,
    void* buffer,
    int size)
{
    if( !cf || !buffer || size < 0 )
        return -1;

    int need = luajs_LuaConfigFile_serialized_size(cf);
    if( need < 0 || size < need )
        return -1;

    uint8_t* p = (uint8_t*)buffer;
    int data_size = cf->size >= 0 ? cf->size : 0;

    write_int(p + 0, need);
    memset(p + 4, 0, 64);
    memcpy(p + 4, cf->name, strnlen(cf->name, 64));
    write_int(p + 68, data_size);

    if( data_size > 0 && cf->data )
        memcpy(p + LUAJS_CONFIG_HEADER_SIZE, cf->data, (size_t)data_size);

    return need;
}

EMSCRIPTEN_KEEPALIVE
struct LuaConfigFile*
luajs_LuaConfigFile_deserialize(
    const void* buffer,
    int size)
{
    if( !buffer || size < LUAJS_CONFIG_HEADER_SIZE )
        return NULL;

    const uint8_t* p = (const uint8_t*)buffer;
    int total_size = read_int(p + 0);
    int data_size = read_int(p + 68);

    if( total_size < LUAJS_CONFIG_HEADER_SIZE )
        return NULL;
    if( total_size != LUAJS_CONFIG_HEADER_SIZE + data_size )
        return NULL;
    if( size < total_size )
        return NULL;
    if( data_size < 0 )
        return NULL;

    struct LuaConfigFile* cf =
        (struct LuaConfigFile*)malloc(sizeof(struct LuaConfigFile));
    if( !cf )
        return NULL;
    memset(cf, 0, sizeof(*cf));

    memcpy(cf->name, p + 4, 64);
    cf->name[sizeof(cf->name) - 1] = '\0';

    cf->size = data_size;
    if( data_size > 0 )
    {
        cf->data = (uint8_t*)malloc((size_t)data_size);
        if( !cf->data )
        {
            free(cf);
            return NULL;
        }
        memcpy(cf->data, p + LUAJS_CONFIG_HEADER_SIZE, (size_t)data_size);
    }
    else
        cf->data = NULL;

    return cf;
}

EMSCRIPTEN_KEEPALIVE
void
luajs_LuaConfigFile_free(struct LuaConfigFile* cf)
{
    if( !cf )
        return;
    if( cf->data )
        free(cf->data);
    free(cf);
}

EMSCRIPTEN_KEEPALIVE
struct LuaConfigFile*
luajs_ConfigFile_deserialize(
    const void* buffer,
    int size)
{
    return luajs_LuaConfigFile_deserialize(buffer, size);
}

EMSCRIPTEN_KEEPALIVE
void
luajs_ConfigFile_free(struct LuaConfigFile* cf)
{
    luajs_LuaConfigFile_free(cf);
}
