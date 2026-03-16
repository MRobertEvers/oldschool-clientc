#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include <stdbool.h>
#include <stdint.h>

struct LuaCSidecar;

struct LuaCSidecar*
LuaCSidecar_New(void);

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

#endif