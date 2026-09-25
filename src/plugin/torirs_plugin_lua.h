#ifndef TORIRS_PLUGIN_LUA_H
#define TORIRS_PLUGIN_LUA_H

/*
 * The Lua runtime host's engine-facing surface.
 *
 * Scripts arrive as BYTES, never as paths: the boot task reads them through
 * the IO queue (PlatformX_IO / TORIRS_IOK_SCRIPT) exactly like every other
 * asset, which is what lets the same code serve the native lanes and the two
 * web lanes, where a synchronous file read does not exist at all.
 */

struct ToriRS_PluginHost;
struct lua_State;

/*
 * One entry in a module's function table.
 *
 * `int (*)(struct lua_State*)` IS `lua_CFunction`; spelling it with the struct
 * tag is what lets this header stay free of lua.h, which matters because
 * app_internal.h includes it and the client's include path has no Lua on it.
 * The one TU that talks to Lua -- and now the quest driver's six -- get
 * -I$(LUA_DIR) from the makefile.
 */
struct LuaFn
{
    char const* name;
    int (*fn)(struct lua_State*);
};

/*
 * Test-only module installer (the quest driver, src/plugin/torirs_plugin_drive.h).
 *
 * NULL in every ordinary build and in every plugin-host unit test, which never
 * link the driver -- which is the whole reason this is a function pointer the
 * driver sets rather than a call into it. Installed before any script is
 * compiled; called with the fresh `api` table on top of the stack, and expected
 * to push its own module tables and setfield them onto it.
 */
typedef void (*PluginLua_TestModulesFn)(struct lua_State* L, void* script);
void PluginLua_SetTestModules(PluginLua_TestModulesFn installer);

/** Push a NEW table of `fns`, each bound to `script` as its upvalue. */
void PluginLua_PushModule(struct lua_State* L, void* script, struct LuaFn const* fns);
/** Add `fns` to the table already on top of the stack. Several arrays make one
 *  flat module this way, which is how `api.drive` is assembled from six files
 *  without any of them editing another's. */
void PluginLua_AppendModule(struct lua_State* L, void* script, struct LuaFn const* fns);

/* ------------------------------------------------ test-only coroutine seam */

/*
 * The quest driver's scheduler. Here rather than in the driver because all
 * three need `struct LuaScript`: the instruction budget, the fault routing and
 * the api scope are the runtime's business, not a test module's.
 *
 * The rule that makes or breaks this: lua_newthread copies the parent's hook
 * only at CREATION (3rd/lua/lstate.c:280-284), so PluginLua_ThreadResume
 * re-arms the step-budget hook on the coroutine's own lua_State every time.
 * A busy-loop chunk with no await must still be killed.
 */
struct lua_State* PluginLua_ThreadCreate(
    char const* plugin_name,
    char const* chunk_name,
    char const* source,
    int source_len,
    int* out_registry_ref);

/** LUA_OK when the chunk returned, LUA_YIELD when it called drive.await, any
 *  other status is an error already routed through the script's fault path
 *  (the plugin is disabled and `error` carries the message). */
int PluginLua_ThreadResume(
    struct lua_State* thread,
    int argument_count,
    int* out_result_count,
    char* error,
    int error_cap);

void PluginLua_ThreadDestroy(int registry_ref);


/** Compile `source` and register it with the host as a plugin in its own
 *  right. Returns the host plugin index, or -1 when the script would not load
 *  (the reason is logged). `name` is the manifest identity and must match the
 *  returned V2 table's `id`. The source bytes are retained for reload. */
int PluginLua_AddScript(
    struct ToriRS_PluginHost* host,
    char const* name,
    char const* source,
    int source_len);

/** Closes every script state. Called from the runtime host's own shutdown. */
void PluginLua_Shutdown(void);

#include "plugin/torirs_plugin_api.h"

/** The scripting runtime host is an ordinary native V2 plugin. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LUA;

/** Resolve a key name ("shift", "ctrl", ...) to a code, or -1. */
int PluginLua_KeyCodeFromName(char const* name);

#if defined(TORIRS_PLUGIN_LUA_TESTING)
/** Test-only source replacement used to exercise the host reload seam. */
bool PluginLua_TestReplaceSource(int plugin_index, char const* source, int source_len);
#endif

#endif /* TORIRS_PLUGIN_LUA_H */
