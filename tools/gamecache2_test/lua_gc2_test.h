#ifndef LUA_GC2_TEST_H
#define LUA_GC2_TEST_H

/*
 * lua_gc2_test — Lua bindings for the gamecache2_test tool.
 *
 * Exposes to Lua:
 *   GameCache2.IORequestBuffer  (wraps GameCache2IORequestBuffer)
 *   GameCache2.Dat1             (WorldBuild_BeginFetch, WorldBuild_BeginLoad)
 *   Platform.LoadArchives       (coroutine yield → C stub → CacheDat I/O)
 *
 * The host (main.cpp) calls gamecache2_test_run_worldbuild_script() after
 * creating a lua_State.  That function:
 *   1. Registers the globals above.
 *   2. Pushes gc2 as a global light-userdata "gc2" visible to the script.
 *   3. Loads + runs the script in a coroutine, handling the Platform.LoadArchives
 *      yield in a C loop (no SDL pump needed during worldbuild).
 */

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;
struct GameCache2;
struct CacheDat;

/**
 * Register GameCache2 and Platform globals on L, then load and run
 * script_path in a coroutine.  Returns 0 on success, -1 on error (with
 * a message already printed to stderr).
 */
int
gamecache2_test_run_worldbuild_script(
    struct lua_State* L,
    const char* script_path,
    struct GameCache2* gc2,
    struct CacheDat* cache_dat);

#ifdef __cplusplus
}
#endif

#endif /* LUA_GC2_TEST_H */
