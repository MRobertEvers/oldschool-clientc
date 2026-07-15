#ifndef LUAJS_CONFIGS_H
#define LUAJS_CONFIGS_H

#include "osrs/lua_sidecar/lua_configfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header: total_size(4) + name[64] + data_size(4) */
#define LUAJS_CONFIG_HEADER_SIZE 72

int
luajs_LuaConfigFile_serialized_size(const struct LuaConfigFile* cf);

int
luajs_LuaConfigFile_serialize_to_buffer(
    const struct LuaConfigFile* cf,
    void* buffer,
    int size);

struct LuaConfigFile*
luajs_LuaConfigFile_deserialize(const void* buffer, int size);

void
luajs_LuaConfigFile_free(struct LuaConfigFile* cf);

/**
 * WASM-stable names; same as luajs_LuaConfigFile_deserialize / _free.
 */
struct LuaConfigFile*
luajs_ConfigFile_deserialize(const void* buffer, int size);

void
luajs_ConfigFile_free(struct LuaConfigFile* cf);

#ifdef __cplusplus
}
#endif

#endif
