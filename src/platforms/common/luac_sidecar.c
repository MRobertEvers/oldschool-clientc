#include "luac_sidecar.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

/* ── Lua version compatibility ───────────────────────────────────────────── */
/*
 * Lua 5.4 added a 4th parameter to lua_resume (int *nresults).
 * Wrap it so the call-sites are identical regardless of Lua version.
 */
#if LUA_VERSION_NUM >= 504
static int
lua_resume_compat(
    lua_State* co,
    lua_State* from,
    int narg)
{
    int nres = 0;
    return lua_resume(co, from, narg, &nres);
}
#else
#define lua_resume_compat lua_resume
#endif

struct LuaCSidecar
{
    lua_State* L;
    lua_State* L_coro;
};

/* ── _lua_log(instance_id, msg) ──────────────────────────────────────────── */
/*
 * Registered as a global C function so platform.lua can call it
 * synchronously without yielding (same contract as the JS bridge).
 */
static int
c_lua_log(lua_State* L)
{
    const char* iid = luaL_optstring(L, 1, "?");
    const char* msg = luaL_optstring(L, 2, "");

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char ts[10];
    strftime(ts, sizeof ts, "%H:%M:%S", tm);

    printf("%s  [%s]  %s\n", ts, iid, msg);
    fflush(stdout);
    return 0;
}

/* ── Module preloader ────────────────────────────────────────────────────── */
/*
 * Registers a .lua file in package.preload so require("name") works
 * from within any coroutine sharing this Lua state.
 *
 * Mirrors preloadModule() in main.js.
 */
static int
file_module_loader(lua_State* L)
{
    const char* filepath = lua_tostring(L, lua_upvalueindex(1));

    if( luaL_loadfile(L, filepath) != LUA_OK )
        return lua_error(L); /* compile error – propagates to require() */

    /* Execute the chunk; platform.lua returns its module table. */
    if( lua_pcall(L, 0, 1, 0) != LUA_OK )
        return lua_error(L); /* runtime error */

    return 1; /* one result: the module */
}

static void
preload_module(
    lua_State* L,
    const char* name,
    const char* filepath)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushstring(L, filepath); /* upvalue 1: file path */
    lua_pushcclosure(L, file_module_loader, 1);
    lua_setfield(L, -2, name); /* package.preload[name] = loader */
    lua_pop(L, 2);
}

/* ── Coroutine driver ────────────────────────────────────────────────────── */
/*
 * Drives the coroutine in a blocking loop.  On every yield it reads the
 * command from the stack, performs the I/O, then resumes with the result.
 *
 * This is the C equivalent of async driveCoroutine() in main.js.
 * The only difference: the browser version can interleave two instances via
 * the JS event loop; here we run one instance sequentially.
 *
 * Returns 0 on clean finish, -1 on Lua error.
 */
static int
step_coroutine(
    lua_State* co,
    lua_State* from,
    const char* instance_id,
    struct LuaCAsyncCall* out_async_call,
    struct LuaCAsyncResult* in_async_result)
{
    int nresume = 0; /* values on co's stack to pass as results of the yield */
    if( in_async_result )
    {
        for( int i = 0; i < in_async_result->argno && i < 6; i++ )
        {
            if( in_async_result->args[i].type == 0 ) /* int */
                lua_pushnumber(co, in_async_result->args[i]._iarg);
            else if( in_async_result->args[i].type == 1 ) /* string */
                lua_pushstring(co, in_async_result->args[i]._strarg);
            else if( in_async_result->args[i].type == 2 ) /* lightuserdata */
                lua_pushlightuserdata(co, in_async_result->args[i]._ptrarg);

            nresume++;
        }
    }

    int status = lua_resume_compat(co, from, nresume);
    nresume = 0;

    if( status == LUA_OK )
        return LUACSIDECAR_DONE; /* coroutine returned normally (won't happen with infinite loop) */

    assert(out_async_call != NULL);

    if( status != LUA_YIELD )
    {
        fprintf(
            stderr,
            "[%s] Lua error: %s\n",
            instance_id,
            lua_gettop(co) > 0 ? lua_tostring(co, -1) : "(no message)");
        return -1;
    }

    /* ── Coroutine yielded – read the command tuple ────────────────── */
    int n = lua_gettop(co);
    int cmd = (n >= 1) ? lua_tonumber(co, 1) : 0;
    out_async_call->command = cmd;
    out_async_call->argno = 0;

    for( int i = 1; i < n; i++ )
    {
        int argno = out_async_call->argno;
        if( i < 6 && lua_isnumber(co, i + 1) )
            out_async_call->args[argno] = (int)lua_tonumber(co, i + 1);
        else
            out_async_call->args[argno] = 0;
        out_async_call->argno += 1;
    }

    return LUACSIDECAR_YIELDED;
}

struct LuaCSidecar*
LuaCSidecar_New(void)
{
    struct LuaCSidecar* sidecar = malloc(sizeof(struct LuaCSidecar));
    if( !sidecar )
        return NULL;
    memset(sidecar, 0, sizeof(*sidecar));

    sidecar->L = luaL_newstate();
    if( !sidecar->L )
    {
        free(sidecar);
        return NULL;
    }
    luaL_openlibs(sidecar->L);

    /* _lua_log(id, msg) – called directly by platform.lua (no yield) */
    lua_pushcfunction(sidecar->L, c_lua_log);
    lua_setglobal(sidecar->L, "_lua_log");

    /* ── Preload platform module ─────────────────────────────────────────── */
    /*
     * Registers platform.lua in package.preload.  The coroutine thread shares
     * this global state, so require("platform") inside core.lua finds it.
     */
    char platform_path[512];
    snprintf(platform_path, sizeof(platform_path), "%s/%s", LUA_SCRIPTS_DIR, "buildcache.lua");

    /* Pass the FULL path to the preloader */
    preload_module(sidecar->L, "buildcache", platform_path);

    return sidecar;
}

void
LuaCSidecar_Free(struct LuaCSidecar* sidecar)
{
    if( sidecar )
    {
        if( sidecar->L )
            lua_close(sidecar->L);
        free(sidecar);
    }
}

void
LuaCSidecar_ResultPushInt(
    struct LuaCAsyncResult* result,
    int val)
{
    result->args[result->argno].type = 0;
    result->args[result->argno]._iarg = val;
    result->argno += 1;
}

void
LuaCSidecar_ResultLightUserData(
    struct LuaCAsyncResult* result,
    void* val)
{
    result->args[result->argno].type = 1;
    result->args[result->argno]._strarg = val;
    result->argno += 1;
}

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaCScriptCall* script_call,
    struct LuaCAsyncCall* out_async_call)
{
    lua_State* co = lua_newthread(sidecar->L);
    sidecar->L_coro = co;

    char filepath[256] = { 0 };
    snprintf(filepath, sizeof filepath, "%s/%s", LUA_SCRIPTS_DIR, script_call->name);

    if( luaL_loadfile(co, filepath) != LUA_OK )
    {
        fprintf(stderr, "core.lua load error: %s\n", lua_tostring(co, -1));
        lua_close(sidecar->L);
        return -1;
    }

    printf("=== Lua native host ===\n\n");

    struct LuaCAsyncResult in_args = { 0 };
    for( int i = 0; i < script_call->argno && i < 10; i++ )
    {
        in_args.args[i].type = script_call->args[i].type;
        if( script_call->args[i].type == 1 )
            in_args.args[i]._strarg = script_call->args[i]._strarg;
        else
            in_args.args[i]._iarg = script_call->args[i]._iarg;
        in_args.argno += 1;
    }

    int rc = step_coroutine(co, sidecar->L, "native", out_async_call, &in_args);
    if( rc == LUACSIDECAR_YIELDED )
        return rc;
    else if( rc != LUACSIDECAR_DONE )
    {
        fprintf(stderr, "core.lua runtime error: %s\n", lua_tostring(co, -1));
        assert(false && "Lua error");
        return -1;
    }

    // pop the thread
    lua_pop(sidecar->L, 1);
    sidecar->L_coro = NULL;
    return LUACSIDECAR_DONE;
}

int
LuaCSidecar_ResumeScript(
    struct LuaCSidecar* sidecar,
    struct LuaCAsyncCall* out_async_call,
    struct LuaCAsyncResult* in_async_result)
{
    lua_State* co = sidecar->L_coro;
    assert(co != NULL && "No coroutine to resume");

    int rc = step_coroutine(co, sidecar->L, "native", out_async_call, in_async_result);
    if( rc == LUACSIDECAR_YIELDED )
        return rc;
    else if( rc != LUACSIDECAR_DONE )
    {
        fprintf(stderr, "core.lua runtime error: %s\n", lua_tostring(co, -1));
        assert(false && "Lua error");
        return -1;
    }

    // pop the thread
    lua_pop(sidecar->L, 1);
    sidecar->L_coro = NULL;
    return LUACSIDECAR_DONE;
}
