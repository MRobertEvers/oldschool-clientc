#include "luac_sidecar.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/game.h"
#include "osrs/lua_scripts.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_platform.h"
#include "osrs/lua_sidecar/luac_gametypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

static int
c_wasm_dispatcher(lua_State* L)
{
    const char* func_name = lua_tostring(L, lua_upvalueindex(1));
    void* ctx = lua_touserdata(L, lua_upvalueindex(2));
    LuaCSidecar_GameCallback callback =
        (LuaCSidecar_GameCallback)lua_touserdata(L, lua_upvalueindex(3));

    if( !callback )
    {
        printf("C Dispatch: Calling '%s' on context %p (no callback)\n", func_name, ctx);
        return 1;
    }

    /* Build args as VarTypeArray [func_name_string, arg1, arg2, ...] */
    int nargs = lua_gettop(L);
    struct LuaGameType* args = LuaGameType_NewVarTypeArray(nargs + 1);
    if( !args )
        return lua_error(L);

    char* func_name_copy = func_name ? strdup(func_name) : strdup("");
    if( !func_name_copy )
    {
        LuaGameType_Free(args);
        return lua_error(L);
    }
    LuaGameType_VarTypeArrayPush(
        args, LuaGameType_NewString(func_name_copy, (int)strlen(func_name_copy)));

    for( int i = 1; i <= nargs; i++ )
    {
        struct LuaGameType* elem = LuacGameType_FromLua(L, i);
        if( elem )
            LuaGameType_VarTypeArrayPush(args, elem);
    }

    struct LuaGameType* result = callback(ctx, args);
    LuacGameType_Free(args);

    if( result )
    {
        LuacGameType_PushToLua(L, result);
        LuacGameType_Free(result);
        return 1;
    }
    return 0;
}
static int
mt_index(lua_State* L)
{
    // Access the pointers baked into THIS specific mt_index instance
    void* ctx = lua_touserdata(L, lua_upvalueindex(1));
    void* callback = lua_touserdata(L, lua_upvalueindex(2));

    // 1. Get the cache from the metatable
    if( !lua_getmetatable(L, 1) )
        return 0;
    lua_pushstring(L, "__cache");
    lua_rawget(L, -2);
    int cache_idx = lua_gettop(L);

    // 2. Check cache
    lua_pushvalue(L, 2);
    lua_rawget(L, cache_idx);
    if( !lua_isnil(L, -1) )
        return 1;
    lua_pop(L, 1);

    // 3. Create dispatcher with 3 upvalues
    lua_pushvalue(L, 2);                // UV 1: func_name
    lua_pushlightuserdata(L, ctx);      // UV 2: ctx
    lua_pushlightuserdata(L, callback); // UV 3: callback
    lua_pushcclosure(L, c_wasm_dispatcher, 3);

    // 4. Store in cache
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_rawset(L, cache_idx);

    return 1;
}
static int
create_wasm_object(
    lua_State* L,
    void* wasm_ctx,
    LuaCSidecar_GameCallback callback)
{
    lua_newtable(L); // The Object "Game"

    // 1. Create the metatable
    lua_newtable(L);

    // 2. Create and attach the cache table to the metatable
    lua_pushstring(L, "__cache");
    lua_newtable(L);
    lua_newtable(L); // Cache metatable for weak values
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_rawset(L, -3); // mt.__cache = {}

    // 3. Bind mt_index with 2 UPVALUES (ctx and callback)
    // This is the "Magic" part: the pointers live in the C-closure of mt_index
    lua_pushlightuserdata(L, wasm_ctx);
    lua_pushlightuserdata(L, (void*)callback);
    lua_pushcclosure(L, mt_index, 2);
    lua_setfield(L, -2, "__index");

    // 4. Set metatable and Global
    lua_setmetatable(L, -2);
    lua_setglobal(L, "Game");

    return 1;
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
    void* ctx;
    LuaCSidecar_GameCallback callback;
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
    struct LuaGameType* args,
    struct LuaCYield* yield)
{
    int nresume = 0; /* values on co's stack to pass as results of the yield */

    if( args )
    {
        nresume = LuacGameType_PushToLua(co, args);
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
    int cmd = (n >= 1) ? (int)lua_tonumber(co, 1) : 0;
    yield->command = cmd;

    /* Build yield args as a VarTypeArray from Lua stack indices 2..n */
    int narg = (n > 1) ? n - 1 : 0;
    yield->args = NULL;
    if( narg > 0 )
    {
        struct LuaGameType* arr = LuaGameType_NewVarTypeArray(narg);
        if( arr )
        {
            for( int i = 2; i <= n; i++ )
            {
                struct LuaGameType* elem = LuacGameType_FromLua(co, i);
                if( elem )
                    LuaGameType_VarTypeArrayPush(arr, elem);
            }
            yield->args = arr;
        }
    }

    return LUACSIDECAR_YIELDED;
}

struct LuaCSidecar*
LuaCSidecar_New(
    void* ctx,
    LuaCSidecar_GameCallback callback)
{
    struct LuaCSidecar* sidecar = malloc(sizeof(struct LuaCSidecar));
    if( !sidecar )
        return NULL;
    memset(sidecar, 0, sizeof(*sidecar));
    sidecar->ctx = ctx;
    sidecar->callback = callback;

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

    create_wasm_object(sidecar->L, sidecar->ctx, sidecar->callback);

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

int
LuaCSidecar_RunScript(
    struct LuaCSidecar* sidecar,
    struct LuaGameScript* script,
    struct LuaCYield* yield)
{
    lua_State* co = lua_newthread(sidecar->L);
    sidecar->L_coro = co;

    char filepath[256] = { 0 };
    snprintf(filepath, sizeof filepath, "%s/%s", LUA_SCRIPTS_DIR, script->name);

    if( luaL_loadfile(co, filepath) != LUA_OK )
    {
        fprintf(stderr, "core.lua load error: %s\n", lua_tostring(co, -1));
        lua_close(sidecar->L);
        return -1;
    }

    int rc = step_coroutine(co, sidecar->L, "native", script->args, yield);
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
    struct LuaGameType* args,
    struct LuaCYield* yield)
{
    lua_State* co = sidecar->L_coro;
    assert(co != NULL && "No coroutine to resume");

    int rc = step_coroutine(co, sidecar->L, "native", args, yield);
    if( rc == LUACSIDECAR_YIELDED )
        return rc;

    // pop the thread
    lua_pop(sidecar->L, 1);
    sidecar->L_coro = NULL;
    return LUACSIDECAR_DONE;
}
