#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

#include "osrs/scripts/buildcache.lua.h"

#include <stdbool.h>
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
        int type; /* 0=int, 1=string, 2=ptr, 3=bool, 5=int_array */
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

/** For type 5 (int_array): _ptrarg points to this. Caller allocates; luac_sidecar
 * pushes table and frees both ids and the struct. */
struct LuaCIntArray
{
    int* ids;
    int count;
};

/** For type 6 (ptr_array): _ptrarg points to this. Caller allocates; luac_sidecar
 * pushes table of lightuserdata and frees ptrs array and struct (not the ptrs themselves). */
struct LuaCPtrArray
{
    void** ptrs;
    int count;
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

void
LuaCSidecar_ResultPushInt(
    struct LuaCAsyncResult* result,
    int val);

void
LuaCSidecar_ResultPushString(
    struct LuaCAsyncResult* result,
    const char* val);

void
LuaCSidecar_ResultPushPtr(
    struct LuaCAsyncResult* result,
    void* val);

void
LuaCSidecar_ResultPushBool(
    struct LuaCAsyncResult* result,
    bool val);

#endif