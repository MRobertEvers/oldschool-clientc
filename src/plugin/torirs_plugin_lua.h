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

/*
 * Key codes a script may name as a string, mirroring enum LibToriRS_KeyCode.
 *
 * Restated here rather than including the input header so the adapter -- and
 * anything else built on the plugin contract -- stays free of engine headers.
 * The static asserts in torirs_plugin_bridge.u.c are what keep the two in
 * step: if the enum ever moves, the client stops compiling rather than
 * silently gating on the wrong key.
 */
#define TORIRS_PLUGIN_KEY_ESCAPE 37
#define TORIRS_PLUGIN_KEY_SHIFT 42
#define TORIRS_PLUGIN_KEY_CTRL 43
#define TORIRS_PLUGIN_KEY_TAB 44
#define TORIRS_PLUGIN_KEY_SPACE 45

/** Resolve a key name ("shift", "ctrl", ...) to a code, or -1. */
int PluginLua_KeyCodeFromName(char const* name);

#endif /* TORIRS_PLUGIN_LUA_H */
