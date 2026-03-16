#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include <stdbool.h>
#include <stdint.h>

struct LuaCSidecar;
struct LuaGameType;

/** Callback: (ctx, args) -> result. args is VarTypeArray [func_name_string, arg1, arg2, ...].
 * Returns malloc'd LuaGameType* or NULL for void. Caller frees result via LuacGameType_Free. */
typedef struct LuaGameType* (*LuaCSidecar_GameCallback)(
    void* ctx,
    struct LuaGameType* args);

struct LuaCSidecar*
LuaCSidecar_New(
    void* ctx,
    LuaCSidecar_GameCallback callback);

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar);

#define LUACSIDECAR_DONE 0
#define LUACSIDECAR_YIELDED 1

struct LuaCScriptCall
{
    char name[64];
    struct LuaGameType* args[10];
    int argno;
};

struct LuaCYield
{
    int command;
    struct LuaGameType* args;
};

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaCScriptCall* script_call,
    struct LuaGameType* args,
    struct LuaCYield* yield);

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaGameType* args,
    struct LuaCYield* yield);

#endif