#ifndef LUAC_SIDECAR_H
#define LUAC_SIDECAR_H

struct LuaCSidecar;
struct GGame;

struct LuaCSidecar*
LuaCSidecar_New(void);

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar);

#define LUACSIDECAR_DONE 0
#define LUACSIDECAR_YIELDED 1

struct LuaCAsyncCall
{
    char command[32];
    int args[6];
    int nargs;
};

struct LuaCAsyncResult
{
    struct
    {
        int type; /* 0=int, 1=string, etc. */
        union
        {
            int int_val;
            void* ptr_val;
        };
    } args[6];
    int nargs;
};

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    const char* script_path,
    struct LuaCAsyncCall* out_async_call /* optional output parameter for async command */);

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaCAsyncCall* out_async_call,
    struct LuaCAsyncResult* in_async_result);

#endif