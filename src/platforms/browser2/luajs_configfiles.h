#ifndef LUAJS_CONFIGFILES_H
#define LUAJS_CONFIGFILES_H

#include "osrs/lua_sidecar/lua_configfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deserialize a config file from raw bytes (copies into newly allocated buffer).
 * Caller must free with luajs_ConfigFile_free().
 */
struct LuaConfigFile*
luajs_ConfigFile_deserialize(const void* buffer, int size);

/**
 * Free a LuaConfigFile allocated by luajs_ConfigFile_deserialize.
 * Safe to call with NULL.
 */
void
luajs_ConfigFile_free(struct LuaConfigFile* cf);

#ifdef __cplusplus
}
#endif

#endif

