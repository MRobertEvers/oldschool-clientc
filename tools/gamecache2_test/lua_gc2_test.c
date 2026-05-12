#include "lua_gc2_test.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "osrs/gamecache2/dat1/gamecache2_dat1.h"
#include "osrs/gamecache2/gamecache2.h"
#include "osrs/gamecache2/gamecache2_iorequest.h"
#include "osrs/rscache/cache_dat.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Metatable / registry key names ────────────────────────────────────── */
#define MT_IOREQUEST_BUFFER "GameCache2.IORequestBuffer"
#define MT_ARCHIVES "GameCache2.Archives"

/* Registry keys used to share state between the Lua side and the host driver */
#define REG_CACHE_DAT "gc2_test.cache_dat"
#define REG_PENDING_BUF "gc2_test.pending_buf"

/* ── lua_resume compat (Lua 5.4+ adds an nresults out-param) ───────────── */
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
static int
lua_resume_compat(
    lua_State* co,
    lua_State* from,
    int narg)
{
    return lua_resume(co, from, narg);
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * GameCache2.IORequestBuffer userdata
 * ═══════════════════════════════════════════════════════════════════════ */

static struct GameCache2IORequestBuffer*
check_iorequest_buffer(
    lua_State* L,
    int idx)
{
    struct GameCache2IORequestBuffer** ud = luaL_checkudata(L, idx, MT_IOREQUEST_BUFFER);
    luaL_argcheck(L, *ud != NULL, idx, "IORequestBuffer is null");
    return *ud;
}

static int
iorequest_buffer_new(lua_State* L)
{
    struct GameCache2IORequestBuffer** ud =
        lua_newuserdata(L, sizeof(struct GameCache2IORequestBuffer*));
    *ud = GameCache2IORequestBuffer_New();
    if( !*ud )
        return luaL_error(L, "GameCache2IORequestBuffer_New failed");
    luaL_setmetatable(L, MT_IOREQUEST_BUFFER);
    return 1;
}

static int
iorequest_buffer_gc(lua_State* L)
{
    struct GameCache2IORequestBuffer** ud = luaL_checkudata(L, 1, MT_IOREQUEST_BUFFER);
    if( *ud )
    {
        GameCache2IORequestBuffer_Free(*ud);
        *ud = NULL;
    }
    return 0;
}

static int
iorequest_buffer_count(lua_State* L)
{
    struct GameCache2IORequestBuffer* buf = check_iorequest_buffer(L, 1);
    lua_pushinteger(L, GameCache2IORequestBuffer_Count(buf));
    return 1;
}

/* Returns a table { table_id, archive_id, flags } for request at 1-based index. */
static int
iorequest_buffer_get_request(lua_State* L)
{
    struct GameCache2IORequestBuffer* buf = check_iorequest_buffer(L, 1);
    int idx = (int)luaL_checkinteger(L, 2) - 1; /* convert 1-based Lua index */
    luaL_argcheck(
        L, idx >= 0 && idx < GameCache2IORequestBuffer_Count(buf), 2, "index out of range");
    struct GameCache2IORequest* req = GameCache2IORequestBuffer_GetRequest(buf, idx);
    lua_newtable(L);
    lua_pushinteger(L, req->table_id);
    lua_setfield(L, -2, "table_id");
    lua_pushinteger(L, req->archive_id);
    lua_setfield(L, -2, "archive_id");
    lua_pushinteger(L, req->flags);
    lua_setfield(L, -2, "flags");
    return 1;
}

static const luaL_Reg kIORequestBufferIndex[] = {
    { "Count",      iorequest_buffer_count       },
    { "GetRequest", iorequest_buffer_get_request },
    { NULL,         NULL                         }
};

/* ═══════════════════════════════════════════════════════════════════════
 * GameCache2.Archives userdata (opaque linked-list head)
 * ═══════════════════════════════════════════════════════════════════════ */

static int
archives_gc(lua_State* L)
{
    /* Archives data is consumed (and owned) by GameCache2Dat1_WorldBuild_BeginLoad.
     * We do not free here to avoid a double-free — same as the original main.cpp. */
    (void)L;
    return 0;
}

static void
push_archives(
    lua_State* L,
    struct GameCache2Archive* head)
{
    struct GameCache2Archive** ud = lua_newuserdata(L, sizeof(struct GameCache2Archive*));
    *ud = head;
    luaL_setmetatable(L, MT_ARCHIVES);
}

/* ═══════════════════════════════════════════════════════════════════════
 * stub_load_archives — blocking CacheDat I/O (runs between yield + resume)
 * Mirrors the loop that was inlined in main.cpp, now extracted here.
 * ═══════════════════════════════════════════════════════════════════════ */

static struct GameCache2Archive*
convert_cachedat_archive(struct CacheDatArchive* src)
{
    struct GameCache2Archive* dst = malloc(sizeof(struct GameCache2Archive));
    if( !dst )
        return NULL;
    dst->data = src->data;
    dst->data_size = src->data_size;
    dst->table_id = src->table_id;
    dst->archive_id = src->archive_id;
    dst->flags = 0;
    dst->next = NULL;
    /* Transfer data ownership so cache_dat_archive_free won't double-free. */
    src->data = NULL;
    src->data_size = 0;
    return dst;
}

/* Returns head of a GameCache2Archive linked list, or NULL on error. */
static struct GameCache2Archive*
stub_load_archives(
    struct CacheDat* cache_dat,
    struct GameCache2IORequestBuffer* buf)
{
    struct GameCache2Archive* list = NULL;
    int count = GameCache2IORequestBuffer_Count(buf);

    for( int i = 0; i < count; i++ )
    {
        struct GameCache2IORequest* req = GameCache2IORequestBuffer_GetRequest(buf, i);

        switch( req->table_id )
        {
        case CACHE_DAT_CONFIGS:
        {
            struct CacheDatArchive* cda =
                cache_dat_archive_new_load(cache_dat, req->table_id, req->archive_id);
            if( !cda )
            {
                fprintf(
                    stderr,
                    "[gc2_test] cache_dat_archive_new_load failed"
                    " (table=%d archive=%d)\n",
                    req->table_id,
                    req->archive_id);
                return NULL;
            }
            struct GameCache2Archive* gc2a = convert_cachedat_archive(cda);
            cache_dat_archive_free(cda);
            if( !gc2a )
            {
                fprintf(stderr, "[gc2_test] archive alloc failed\n");
                return NULL;
            }
            GameCache2Archive_Push(gc2a, &list);
            break;
        }
        default:
            fprintf(
                stderr,
                "[gc2_test] unknown table_id %d in request buffer, skipping\n",
                req->table_id);
            break;
        }
    }
    return list;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Platform.LoadArchives — lua_yieldk implementation
 *
 * Called from Lua:  local archives = Platform.LoadArchives(request_buffer)
 *
 * Execution order:
 *   1. Stash the IORequestBuffer pointer in the registry (host reads it after yield).
 *   2. lua_yieldk → suspends the coroutine; host runs stub_load_archives.
 *   3. Host pushes the archives userdata and calls lua_resume(co, L, 1, &nres).
 *   4. Continuation load_archives_k is invoked; it returns 1 (the archives ud).
 * ═══════════════════════════════════════════════════════════════════════ */

static int
load_archives_k(
    lua_State* L,
    int status,
    lua_KContext ctx)
{
    (void)status;
    (void)ctx;
    /* The host pushed one archives userdata before resuming; it is on the stack. */
    return 1;
}

static int
platform_load_archives(lua_State* L)
{
    struct GameCache2IORequestBuffer* buf = check_iorequest_buffer(L, 1);

    /* Stash buf in the registry so the host driver can retrieve it after yield. */
    lua_pushlightuserdata(L, buf);
    lua_setfield(L, LUA_REGISTRYINDEX, REG_PENDING_BUF);

    return lua_yieldk(L, 0, 0, load_archives_k);
}

/* ═══════════════════════════════════════════════════════════════════════
 * GameCache2.Dat1 bindings
 * ═══════════════════════════════════════════════════════════════════════ */

static int
dat1_worldbuild_beginfetch(lua_State* L)
{
    luaL_argcheck(L, lua_islightuserdata(L, 1), 1, "expected gc2 lightuserdata");
    struct GameCache2* gc2 = lua_touserdata(L, 1);
    struct GameCache2IORequestBuffer* buf = check_iorequest_buffer(L, 2);
    GameCache2Dat1_WorldBuild_BeginFetch(gc2, buf);
    return 0;
}

static int
dat1_worldbuild_beginload(lua_State* L)
{
    luaL_argcheck(L, lua_islightuserdata(L, 1), 1, "expected gc2 lightuserdata");
    struct GameCache2* gc2 = lua_touserdata(L, 1);
    struct GameCache2Archive** ud = luaL_checkudata(L, 2, MT_ARCHIVES);
    luaL_argcheck(L, *ud != NULL, 2, "archives userdata is null");
    GameCache2Dat1_WorldBuild_BeginLoad(gc2, *ud);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Library registration helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static void
register_metatables(lua_State* L)
{
    /* IORequestBuffer: __gc + __index table of methods */
    luaL_newmetatable(L, MT_IOREQUEST_BUFFER);
    lua_pushcfunction(L, iorequest_buffer_gc);
    lua_setfield(L, -2, "__gc");
    lua_newtable(L);
    luaL_setfuncs(L, kIORequestBufferIndex, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    /* Archives: __gc only (opaque to Lua) */
    luaL_newmetatable(L, MT_ARCHIVES);
    lua_pushcfunction(L, archives_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
}

static void
register_gamecache2_global(lua_State* L)
{
    /* GameCache2.IORequestBuffer = { New = ... } */
    lua_newtable(L);
    lua_pushcfunction(L, iorequest_buffer_new);
    lua_setfield(L, -2, "New");
    /* IORequestBuffer table is at index -1 */

    /* GameCache2.Dat1 = { WorldBuild_BeginFetch = ..., WorldBuild_BeginLoad = ... } */
    lua_newtable(L);
    lua_pushcfunction(L, dat1_worldbuild_beginfetch);
    lua_setfield(L, -2, "WorldBuild_BeginFetch");
    lua_pushcfunction(L, dat1_worldbuild_beginload);
    lua_setfield(L, -2, "WorldBuild_BeginLoad");
    /* Dat1 table is at index -1, IORequestBuffer at -2 */

    /* GameCache2 = { IORequestBuffer = ..., Dat1 = ... } */
    lua_newtable(L);                        /* [ior_buf][dat1][gc2] */
    lua_insert(L, -3);                      /* [gc2][ior_buf][dat1] */
    lua_setfield(L, -3, "Dat1");            /* [gc2][ior_buf]       */
    lua_setfield(L, -2, "IORequestBuffer"); /* [gc2]           */
    lua_setglobal(L, "GameCache2");
}

static void
register_platform_global(lua_State* L)
{
    lua_newtable(L);
    lua_pushcfunction(L, platform_load_archives);
    lua_setfield(L, -2, "LoadArchives");
    lua_setglobal(L, "Platform");
}

/* ═══════════════════════════════════════════════════════════════════════
 * Public entry point
 * ═══════════════════════════════════════════════════════════════════════ */

int
gamecache2_test_run_worldbuild_script(
    struct lua_State* L,
    const char* script_path,
    struct GameCache2* gc2,
    struct CacheDat* cache_dat)
{
    register_metatables(L);
    register_gamecache2_global(L);
    register_platform_global(L);

    /* gc2 is a global light-userdata; threads share globals so one set suffices. */
    lua_pushlightuserdata(L, gc2);
    lua_setglobal(L, "gc2");

    /* Store cache_dat in the registry for use by the yield handler. */
    lua_pushlightuserdata(L, cache_dat);
    lua_setfield(L, LUA_REGISTRYINDEX, REG_CACHE_DAT);

    /* ── Create coroutine + load the script ── */
    lua_State* co = lua_newthread(L);
    /* Anchor the thread in the registry so GC doesn't collect it. */
    int thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if( luaL_loadfile(co, script_path) != LUA_OK )
    {
        fprintf(stderr, "[gc2_test] Lua load error: %s\n", lua_tostring(co, -1));
        luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
        return -1;
    }

    /* ── Drive the coroutine ── */
    int rc;
    int narg = 0; /* first resume has no args — script receives nothing */

    while( (rc = lua_resume_compat(co, L, narg)) == LUA_YIELD )
    {
        /* Only Platform.LoadArchives is expected to yield. */
        lua_getfield(L, LUA_REGISTRYINDEX, REG_PENDING_BUF);
        struct GameCache2IORequestBuffer* buf = lua_touserdata(L, -1);
        lua_pop(L, 1);

        if( !buf )
        {
            fprintf(stderr, "[gc2_test] unexpected yield: no pending buffer\n");
            luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
            return -1;
        }

        /* Clear so a second unexpected yield is caught. */
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, REG_PENDING_BUF);

        lua_getfield(L, LUA_REGISTRYINDEX, REG_CACHE_DAT);
        struct CacheDat* cd = lua_touserdata(L, -1);
        lua_pop(L, 1);

        struct GameCache2Archive* archives = stub_load_archives(cd, buf);
        if( !archives )
        {
            fprintf(stderr, "[gc2_test] stub_load_archives failed\n");
            luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
            return -1;
        }

        /* Push the archives userdata onto the coroutine stack and resume. */
        push_archives(co, archives);
        narg = 1;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);

    if( rc != LUA_OK )
    {
        fprintf(
            stderr,
            "[gc2_test] Lua runtime error: %s\n",
            lua_gettop(co) > 0 ? lua_tostring(co, -1) : "(no message)");
        return -1;
    }

    return 0;
}
