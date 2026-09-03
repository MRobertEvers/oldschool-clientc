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

#include "plugin/torirs_plugin_v2.h"

/** The scripting runtime host is an ordinary native V2 plugin. */
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LUA;

/** Resolve a key name ("shift", "ctrl", ...) to a code, or -1. */
int PluginLua_KeyCodeFromName(char const* name);

#if defined(TORIRS_PLUGIN_LUA_TESTING)
/** Test-only source replacement used to exercise the host reload seam. */
bool PluginLua_TestReplaceSource(int plugin_index, char const* source, int source_len);
#endif

#endif /* TORIRS_PLUGIN_LUA_H */
