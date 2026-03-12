/*
 * main.c — Native C host for core.lua + platform.lua
 *
 * Drives the same coroutine yield protocol as the JavaScript driver in
 * main.js, so core.lua and platform.lua run unchanged on both platforms:
 *
 *   Browser  →  Fengari JS VM  +  main.js  (IndexedDB / WASM)
 *   Native   →  system Lua     +  main.c   (real disk I/O / POSIX sleep)
 *
 * Protocol (mirrors driveCoroutine() in main.js):
 *   yield("read_file", path)  →  reads path from disk, resumes with content
 *   yield("sleep",     ms)    →  sleeps ms milliseconds, resumes with nothing
 *
 * Build:  make host          (requires Lua 5.3+ headers/libs)
 * Run:    ./lua-host [instance-id]
 *         Run from lua-async-demo/ so relative paths (data.txt etc.) resolve.
 */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Lua version compatibility ───────────────────────────────────────────── */
/*
 * Lua 5.4 added a 4th parameter to lua_resume (int *nresults).
 * Wrap it so the call-sites are identical regardless of Lua version.
 */
#if LUA_VERSION_NUM >= 504
static int lua_resume_compat(lua_State *co, lua_State *from, int narg)
{
    int nres = 0;
    return lua_resume(co, from, narg, &nres);
}
#else
#  define lua_resume_compat lua_resume
#endif

/* ── Portable millisecond sleep ──────────────────────────────────────────── */

#ifdef _WIN32
#  include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}
#endif

/* ── _lua_log(instance_id, msg) ──────────────────────────────────────────── */
/*
 * Registered as a global C function so platform.lua can call it
 * synchronously without yielding (same contract as the JS bridge).
 */
static int c_lua_log(lua_State *L)
{
    const char *iid = luaL_optstring(L, 1, "?");
    const char *msg = luaL_optstring(L, 2, "");

    time_t     now = time(NULL);
    struct tm *tm  = localtime(&now);
    char ts[10];
    strftime(ts, sizeof ts, "%H:%M:%S", tm);

    printf("%s  [%s]  %s\n", ts, iid, msg);
    fflush(stdout);
    return 0;
}

/* ── Real disk read ──────────────────────────────────────────────────────── */
/*
 * Returns a malloc'd string (caller must free).
 * On the browser side this is the WASM hardcoded string or an IndexedDB
 * record; here we read an actual file from the filesystem.
 */
static char *read_file_from_disk(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        char *msg = malloc(256);
        snprintf(msg, 256, "(file not found: %s)", path);
        return msg;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

/* ── Module preloader ────────────────────────────────────────────────────── */
/*
 * Registers a .lua file in package.preload so require("name") works
 * from within any coroutine sharing this Lua state.
 *
 * Mirrors preloadModule() in main.js.
 */
static int file_module_loader(lua_State *L)
{
    const char *filepath = lua_tostring(L, lua_upvalueindex(1));

    if (luaL_loadfile(L, filepath) != LUA_OK)
        return lua_error(L);   /* compile error – propagates to require() */

    /* Execute the chunk; platform.lua returns its module table. */
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        return lua_error(L);   /* runtime error */

    return 1;                  /* one result: the module */
}

static void preload_module(lua_State *L, const char *name, const char *filepath)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushstring(L, filepath);                   /* upvalue 1: file path */
    lua_pushcclosure(L, file_module_loader, 1);
    lua_setfield(L, -2, name);                     /* package.preload[name] = loader */
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
static int drive_coroutine(lua_State *co, lua_State *from, const char *instance_id)
{
    int nresume = 0;   /* values on co's stack to pass as results of the yield */

    for (;;) {
        int status = lua_resume_compat(co, from, nresume);
        nresume = 0;

        if (status == LUA_OK)
            break;   /* coroutine returned normally (won't happen with infinite loop) */

        if (status != LUA_YIELD) {
            fprintf(stderr, "[%s] Lua error: %s\n",
                    instance_id,
                    lua_gettop(co) > 0 ? lua_tostring(co, -1) : "(no message)");
            return -1;
        }

        /* ── Coroutine yielded – read the command tuple ────────────────── */

        int         n   = lua_gettop(co);
        const char *cmd = (n >= 1) ? lua_tostring(co, 1) : NULL;
        const char *a2s = (n >= 2) ? lua_tostring(co, 2) : NULL;   /* string arg */
        int         a2n = (n >= 2) ? (int)lua_tonumber(co, 2) : 0; /* numeric arg */

        lua_settop(co, 0);   /* clear yielded values before pushing result */

        /* ── Dispatch ──────────────────────────────────────────────────── */

        if (cmd && strcmp(cmd, "read_file") == 0) {
            char *content = read_file_from_disk(a2s ? a2s : "data.txt");
            lua_pushstring(co, content ? content : "(read error)");
            free(content);
            nresume = 1;   /* pass the string as the return value of yield */

        } else if (cmd && strcmp(cmd, "sleep") == 0) {
            sleep_ms(a2n > 0 ? a2n : 1000);
            /* nresume stays 0 – platform.sleep() ignores the resume value */
        }
        /* Unknown commands: silently resume with nothing */
    }

    return 0;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *instance_id = (argc > 1) ? argv[1] : "native";

    /* ── Create Lua state ───────────────────────────────────────────────── */
    lua_State *L = luaL_newstate();
    if (!L) {
        fputs("failed to create Lua state\n", stderr);
        return 1;
    }
    luaL_openlibs(L);

    /* ── Register synchronous C bridge functions ────────────────────────── */

    /* _lua_log(id, msg) – called directly by platform.lua (no yield) */
    lua_pushcfunction(L, c_lua_log);
    lua_setglobal(L, "_lua_log");

    /* ── Set globals used by core.lua / platform.lua ────────────────────── */
    lua_pushstring(L, instance_id);
    lua_setglobal(L, "INSTANCE_ID");

    /* ── Preload platform module ─────────────────────────────────────────── */
    /*
     * Registers platform.lua in package.preload.  The coroutine thread shares
     * this global state, so require("platform") inside core.lua finds it.
     */
    preload_module(L, "platform", "platform.lua");

    /* ── Create coroutine thread ─────────────────────────────────────────── */
    /*
     * lua_newthread pushes the thread onto L's stack.  We leave it there so
     * the Lua GC cannot collect the thread while we still hold `co`.
     */
    lua_State *co = lua_newthread(L);

    /* ── Load core.lua directly into the coroutine's stack ──────────────── */
    /*
     * luaL_loadfile compiles core.lua and pushes the resulting function onto
     * co's stack.  The first lua_resume(co, L, 0) will call that function.
     */
    if (luaL_loadfile(co, "core.lua") != LUA_OK) {
        fprintf(stderr, "core.lua load error: %s\n", lua_tostring(co, -1));
        lua_close(L);
        return 1;
    }

    printf("=== Lua native host  |  instance: %s ===\n\n", instance_id);

    /* ── Drive the coroutine (blocks; core.lua loops forever) ───────────── */
    int rc = drive_coroutine(co, L, instance_id);

    lua_close(L);
    return rc == 0 ? 0 : 1;
}
