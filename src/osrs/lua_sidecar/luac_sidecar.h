#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include <stdbool.h>
#include <stdint.h>

struct LuaCSidecar;
struct LuaGameType;

/** Callback: (ctx, args) -> result. args is VarTypeArray [func_name_string, arg1, arg2, ...].
 * Returns malloc'd LuaGameType* or NULL for void. Caller frees result via LuacGameType_Free. */
typedef struct LuaGameType* (*LuaCSidecar_GameCallback)(void* ctx, struct LuaGameType* args);

struct LuaCSidecar*
LuaCSidecar_New(void* ctx, LuaCSidecar_GameCallback callback);

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar);

#define LUACSIDECAR_DONE 0
#define LUACSIDECAR_YIELDED 1

struct LuaCArg
{
    int type;
    union
    {
        int _iarg;
        char* _strarg;
    };
};

struct LuaCScriptCall
{
    char name[64];
    struct LuaCArg args[10];

    int argno;
};

struct LuaCYield
{
    int command;
    int argno;
    uint64_t args[10];
};

struct LuaCArchive
{
    void* ptr;
};

struct LuaCArchiveArray
{
    void* archives;
    int count;
};

struct LuaCYieldResult
{
    int type;
    union
    {
        struct LuaCArchive _archive;
    };
};

void
LuaCSidecar_YieldResultPushArchive(
    struct LuaCYieldResult* result,
    void* archive);

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaCScriptCall* script_call,
    struct LuaCYield* yield);

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaCYield* yield,
    struct LuaCYieldResult* yield_result);

#endif