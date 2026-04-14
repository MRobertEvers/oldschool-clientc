#include "luac_sidecar.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/game.h"
#include "osrs/lua_scripts.h"
#include "osrs/lua_sidecar/lua_api.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_platform.h"
#include "osrs/lua_sidecar/luac_gametypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LUA_SCRIPTS_DIR
#define LUA_SCRIPTS_DIR "../src/osrs/scripts"
#endif

static int
c_wasm_dispatcher_by_id(lua_State* L)
{
    int api_id = (int)lua_tointeger(L, lua_upvalueindex(1));
    void* ctx = lua_touserdata(L, lua_upvalueindex(2));
    LuaCSidecar_GameCallback callback =
        (LuaCSidecar_GameCallback)lua_touserdata(L, lua_upvalueindex(3));

    if( !callback )
        return 0;

    int nargs = lua_gettop(L);
    struct LuaGameType* args = LuaGameType_NewVarTypeArray(nargs + 1);
    if( !args )
        return lua_error(L);

    LuaGameType_VarTypeArrayPush(args, LuaGameType_NewInt(api_id));

    for( int i = 1; i <= nargs; i++ )
    {
        struct LuaGameType* elem = LuacGameType_FromLua(L, i);
        if( elem )
            LuaGameType_VarTypeArrayPush(args, elem);
    }

    struct LuaGameType* result = callback(ctx, args);
    LuaGameType_Free(args);

    if( result )
    {
        int nres = LuacGameType_PushToLua(L, result);
        LuaGameType_Free(result);
        return nres;
    }
    return 0;
}

/** __index for Game.<Domain> proxy tables; UV1=ctx, UV2=callback, UV3=LuaApiDomain (integer). */
static int
domain_mt_index(lua_State* L)
{
    void* ctx = lua_touserdata(L, lua_upvalueindex(1));
    void* callback = lua_touserdata(L, lua_upvalueindex(2));
    enum LuaApiDomain domain = (enum LuaApiDomain)lua_tointeger(L, lua_upvalueindex(3));

    if( !lua_getmetatable(L, 1) )
        return 0;
    lua_pushstring(L, "__cache");
    lua_rawget(L, -2);
    int cache_idx = lua_gettop(L);

    lua_pushvalue(L, 2);
    lua_rawget(L, cache_idx);
    if( !lua_isnil(L, -1) )
        return 1;
    lua_pop(L, 1);

    const char* key = lua_tostring(L, 2);
    if( !key )
        return 0;

    LuaApiId id = lua_api_domain_lookup(domain, key);
    if( id == LUA_API_INVALID )
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, (lua_Integer)id);
    lua_pushlightuserdata(L, ctx);
    lua_pushlightuserdata(L, callback);
    lua_pushcclosure(L, c_wasm_dispatcher_by_id, 3);

    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_rawset(L, cache_idx);

    return 1;
}

/** Push Game.<fieldName> as a proxy table with weak-valued __cache. */
static void
push_domain_proxy(
    lua_State* L,
    void* wasm_ctx,
    LuaCSidecar_GameCallback callback,
    enum LuaApiDomain domain,
    const char* lua_field_name)
{
    lua_newtable(L);

    lua_newtable(L);
    lua_pushstring(L, "__cache");
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_rawset(L, -3);

    lua_pushlightuserdata(L, wasm_ctx);
    lua_pushlightuserdata(L, (void*)callback);
    lua_pushinteger(L, (lua_Integer)domain);
    lua_pushcclosure(L, domain_mt_index, 3);
    lua_setfield(L, -2, "__index");

    lua_setmetatable(L, -2);
    lua_setfield(L, -2, lua_field_name);
}

static int
create_wasm_object(
    lua_State* L,
    void* wasm_ctx,
    LuaCSidecar_GameCallback callback)
{
    lua_newtable(L);

    push_domain_proxy(L, wasm_ctx, callback, LUA_DOMAIN_BUILDCACHEDAT, "BuildCacheDat");
    push_domain_proxy(L, wasm_ctx, callback, LUA_DOMAIN_GAME, "Game");
    push_domain_proxy(L, wasm_ctx, callback, LUA_DOMAIN_DASH, "Dash");
    push_domain_proxy(L, wasm_ctx, callback, LUA_DOMAIN_UI, "UI");
    push_domain_proxy(L, wasm_ctx, callback, LUA_DOMAIN_MISC, "Misc");

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

#define LUACSIDECAR_MAX_CONFIG_FILES 16

struct LuaCSidecar
{
    lua_State* L;
    lua_State* L_coro;
    void* ctx;
    LuaCSidecar_GameCallback callback;
    struct LuaConfigFile* config_files[LUACSIDECAR_MAX_CONFIG_FILES];
    int config_files_count;
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
    /* Base, package, coroutine, math, string, table only (no io/os/debug/utf8). */
    luaL_openselectedlibs(
        sidecar->L,
        LUA_GLIBK | LUA_LOADLIBK | LUA_COLIBK | LUA_MATHLIBK | LUA_STRLIBK | LUA_TABLIBK,
        0);

    lua_api_init();

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
        for( int i = 0; i < sidecar->config_files_count; i++ )
        {
            free(sidecar->config_files[i]->data);
            free(sidecar->config_files[i]);
        }
        if( sidecar->L )
            lua_close(sidecar->L);
        free(sidecar);
    }
}

void
LuaCSidecar_GC(struct LuaCSidecar* sidecar)
{
    if( sidecar && sidecar->L )
        lua_gc(sidecar->L, LUA_GCCOLLECT, 0);
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

void
LuaCSidecar_TrackConfigFile(
    struct LuaCSidecar* sidecar,
    struct LuaConfigFile* file)
{
    if( !sidecar || !file )
        return;
    if( sidecar->config_files_count < LUACSIDECAR_MAX_CONFIG_FILES )
        sidecar->config_files[sidecar->config_files_count++] = file;
}
