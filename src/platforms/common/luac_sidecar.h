#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include "osrs/scripts/buildcache.lua.h"

#include <stdint.h>

struct LuaCSidecar;
struct GGame;

struct LuaCSidecar*
LuaCSidecar_New(void);

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar);

#define LUACSIDECAR_DONE 0
#define LUACSIDECAR_YIELDED 1

struct LuaCScriptCall
{
    char name[64];

    struct
    {
        int type;
        union
        {
            int _iarg;
            char* _strarg;
            int _iarg;
            void* _ptrarg;
        };
    } args[10];

    int argno;
};

struct LuaCAsyncCall
{
    int command;
    int argno;
    uint64_t args[10];
};

struct LuaCAsyncResult
{
    struct
    {
        int type; /* 0=int, 1=string, etc. */
        union
        {
            int _iarg;
            char* _strarg;
            void* _ptrarg;
            bool _barg;
        };
    } args[6];
    int argno;
};

void
LuaCSidecar_ResultPushInt(
    struct LuaCAsyncResult* result,
    int val);

void
LuaCSidecar_ResultLightUserData(
    struct LuaCAsyncResult* result,
    void* val);

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaCScriptCall* script_call,
    struct LuaCAsyncCall* out_async_call /* optional output parameter for async command */);

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaCAsyncCall* out_async_call,
    struct LuaCAsyncResult* in_async_result);

#endif