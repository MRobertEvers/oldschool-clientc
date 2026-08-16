#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include "osrs/lua_sidecar/lua_platform.h"

#include <stdbool.h>
#include <stdint.h>

struct LuaCSidecar;

typedef struct LuaGameType* (*LuaCSidecar_GameCallback)(
    void* ctx,
    struct LuaGameType* args);

struct LuaCSidecar*
LuaCSidecar_New(
    void* ctx,
    LuaCSidecar_GameCallback callback);

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar);

/** Full GC cycle on the sidecar's main Lua state (call after heavy scripts). */
void
LuaCSidecar_GC(struct LuaCSidecar* sidecar);

#define LUACSIDECAR_DONE 0
#define LUACSIDECAR_YIELDED 1

struct LuaCYield
{
    int command;
    struct LuaGameType* args;
};

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaGameScript* script,
    struct LuaCYield* yield);

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaGameType* args,
    struct LuaCYield* yield);

struct LuaConfigFile;
void
LuaCSidecar_TrackConfigFile(
    struct LuaCSidecar* sidecar,
    struct LuaConfigFile* file);

#endif