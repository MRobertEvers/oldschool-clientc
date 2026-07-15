#ifndef LUAC_SIDECAR_UI_H
#define LUAC_SIDECAR_UI_H

#include "osrs/lua_sidecar/luac_sidecar.h"

struct LuaGameType*
LuaCSidecar_Config_LoadConfig(struct LuaGameType* args);

struct LuaGameType*
LuaCSidecar_Config_LoadConfigs(struct LuaGameType* args);

#endif