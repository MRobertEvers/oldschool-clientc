#ifndef TORIRS_PLUGIN_LUA_H
#define TORIRS_PLUGIN_LUA_H

/*
 * The Lua adapter's engine-facing surface.
 *
 * Scripts arrive as BYTES, never as paths: the boot task reads them through
 * the IO queue (PlatformX_IO / TORIRS_IOK_SCRIPT) exactly like every other
 * asset, which is what lets the same code serve the native lanes and the two
 * web lanes, where a synchronous file read does not exist at all.
 */

struct ToriRS_PluginHost;

/** Hand the adapter the host before any script is added. */
void PluginLua_Bind(struct ToriRS_PluginHost* host);

/** Compile `source` and register it with the host as a plugin in its own
 *  right. Returns the host plugin index, or -1 when the script would not load
 *  (the reason is logged). `name` is the fallback identity; a script may name
 *  itself in its returned table. The bytes are not retained. */
int PluginLua_AddScript(
    struct ToriRS_PluginHost* host,
    char const* name,
    char const* source,
    int source_len);

/** Closes every script state. Called from the adapter's own shutdown. */
void PluginLua_Shutdown(void);

/* The key codes moved to torirs_plugin.h (TORIRS_PLUGIN_KEY_*) when the first
 * C plugin needed one. They are part of the contract, not of this adapter --
 * the comment they carried always said so -- and a C plugin including the Lua
 * adapter's header to ask what shift is would have been the wrong shape. */
#include "plugin/torirs_plugin.h"

/** Resolve a key name ("shift", "ctrl", ...) to a code, or -1. */
int PluginLua_KeyCodeFromName(char const* name);

#endif /* TORIRS_PLUGIN_LUA_H */
