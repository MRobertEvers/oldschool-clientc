#ifndef LUA_SIDECAR_MISC_H
#define LUA_SIDECAR_MISC_H

#include "lua_gametypes.h"
#include "osrs/game.h"

#include <stdbool.h>

/** Args: [LuaConfigFile* userdata, int radius]. Installs cullmap on game->world. */
struct LuaGameType*
LuaSidecarMisc_read_cullmap_from_blob(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaSidecarMisc_save_camera(
    struct GGame* game,
    struct LuaGameType* args);

/** Returns VarTypeArray [x, y, z, pitch, yaw] (world ints + angles). */
struct LuaGameType*
LuaSidecarMisc_load_camera(
    struct GGame* game,
    struct LuaGameType* args);

#endif
