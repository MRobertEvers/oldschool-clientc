// Shared config-file userdata for Lua sidecar (native + browser/WASM).
// Intentionally minimal POD; ownership is by the creator; consumers must not free `data`.
#ifndef LUA_CONFIGFILE_H
#define LUA_CONFIGFILE_H

#include <stdint.h>

struct LuaConfigFile
{
    char name[64];
    uint8_t* data;
    int size;
};

#endif
