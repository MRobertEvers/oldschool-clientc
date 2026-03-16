#include "luac_sidecar.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/game.h"
#include "osrs/lua_scripts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

static int
c_wasm_dispatcher(lua_State* L)
{
    // Upvalue 1: The method name
    const char* func_name = lua_tostring(L, lua_upvalueindex(1));
    // Upvalue 2: The Context Pointer (e.g., your WASM environment or C struct)
    void* ctx = lua_touserdata(L, lua_upvalueindex(2));

    // Logic: Look up func_name in your WASM exports and execute it.
    // This part depends on your specific WASM runtime (e.g., Wasmtime, Wasmer, or a custom engine)
    printf("C Dispatch: Calling '%s' on context %p\n", func_name, ctx);

    // Pull arguments from the Lua stack (1, 2, ...) and pass to WASM
    // Push results back to L
    return 1;
}

static int
mt_index(lua_State* L)
{
    // Stack: [1] table, [2] key (name)

    // 1. Get the cache from the metatable
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__cache"); // Stack: [3] metatable, [4] cache_table

    // 2. Check cache[key]
    lua_pushvalue(L, 2);
    lua_gettable(L, 4);
    if( !lua_isnil(L, -1) )
    {
        return 1; // Hit! Return existing closure
    }
    lua_pop(L, 1); // Pop nil

    // 3. Cache Miss: Create closure
    lua_pushvalue(L, 2);         // Upvalue 1: Name
    lua_getfield(L, 1, "__ptr"); // Upvalue 2: Context Pointer
    lua_pushcclosure(L, c_wasm_dispatcher, 2);

    // 4. Update Cache: cache[name] = closure
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_settable(L, 4);

    return 1;
}

int
create_wasm_object(
    lua_State* L,
    void* wasm_ctx)
{
    // 1. Create the Object Table
    lua_newtable(L);
    lua_pushlightuserdata(L, wasm_ctx);
    lua_setfield(L, -2, "__ptr");

    // 2. Create the Metatable
    lua_newtable(L);

    // 3. Create the Cache Table (with weak values)
    lua_newtable(L); // The cache table
    lua_newtable(L); // Metatable for the cache
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "__cache");

    // 4. Bind the indexer
    lua_pushcfunction(L, mt_index);
    lua_setfield(L, -2, "__index");

    // 5. Attach metatable to Object
    lua_setmetatable(L, -2);

    // 2. Pop it from the stack and assign it to the global name 'wasmObj'
    lua_setglobal(L, "Game");

    return 1; // Returns the object to Lua
}

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
    struct LuaCYield* out_yield,
    struct LuaCYieldResult* in_yield_result)
{
    int nresume = 0; /* values on co's stack to pass as results of the yield */
    if( in_yield_result )
    {
        if( in_yield_result->type == 0 )
        {
            lua_pushlightuserdata(co, in_yield_result->_archive.ptr);
            nresume++;
        }
        else
        {
            fprintf(stderr, "Unknown yield result type: %d\n", in_yield_result->type);
            return -1;
        }
    }

    int status = lua_resume_compat(co, from, nresume);
    nresume = 0;

    if( status == LUA_OK )
        return LUACSIDECAR_DONE; /* coroutine returned normally (won't happen with infinite loop) */

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
    out_yield->command = cmd;
    out_yield->argno = 0;

    for( int i = 1; i < n; i++ )
    {
        if( i >= 10 )
            break; /* too many args, ignore the rest */
        int argno = out_yield->argno;
        if( lua_isnumber(co, i + 1) )
            out_yield->args[argno] = (int)lua_tonumber(co, i + 1);
        else if( lua_isstring(co, i + 1) )
            out_yield->args[argno] = (uint64_t)lua_tostring(co, i + 1);

        out_yield->argno += 1;
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
    snprintf(platform_path, sizeof(platform_path), "%s/%s", LUA_SCRIPTS_DIR, "cachedat.lua");

    /* Pass the FULL path to the preloader */
    preload_module(sidecar->L, "cachedat", platform_path);

    create_wasm_object(sidecar->L, NULL);

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
LuaCSidecar_YieldResultPushArchive(
    struct LuaCYieldResult* result,
    void* archive)
{
    result->type = 0;
    result->_archive.ptr = archive;
}

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaCScriptCall* script_call,
    struct LuaCYield* out_yield)
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

    struct LuaCYieldResult in_yield_result = { 0 };
    for( int i = 0; i < script_call->argno && i < 10; i++ )
    {
    }

    int rc = step_coroutine(co, sidecar->L, "native", out_yield, &in_yield_result);
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
