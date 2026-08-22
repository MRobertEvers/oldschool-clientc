#include "plugin/torirs_plugin_registry.h"

#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <stddef.h>

/*
 * Statically linked plugins.
 *
 * There is no dynamic loading: the web build has no dlopen, and nothing else
 * in this tree loads code at runtime either. The C ABI is versioned so a
 * dlopen lane can be added on native later without any plugin changing, but
 * until something needs it, a table is the whole mechanism.
 *
 * Scripted plugins do not appear here. The Lua adapter is one entry below and
 * registers a further plugin per script it loads.
 */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LUA;

/*
 * The BUILTINS: the Activities category of the cache's All Settings panel,
 * one plugin per feature. Every one of them is `.hidden = true`, so none has a
 * row in the plugin roster -- their switch is the cache's, where the user
 * already expects to find it. See NXT_CLIENT_PLUGINS.md.
 *
 * They are registered here beside the ordinary plugins, and that is the point:
 * a builtin written against the same contract is proof the contract is wide
 * enough to write the client's own features in, rather than only the extras.
 */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO;

static struct ToriRS_PluginDef const* const PLUGIN_TABLE[] = {
    &TORIRS_PLUGIN_TILEIND,
    &TORIRS_PLUGIN_MINIMAP_ORBS,
    &TORIRS_PLUGIN_NXT_HIGHLIGHT,
    &TORIRS_PLUGIN_NXT_BIRD_NEST,
    &TORIRS_PLUGIN_NXT_CANNON_AMMO,
    &TORIRS_PLUGIN_LUA,
};

void
PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host)
{
    assert(host);

    for( size_t i = 0; i < sizeof(PLUGIN_TABLE) / sizeof(PLUGIN_TABLE[0]); i++ )
        PluginHost_Register(host, PLUGIN_TABLE[i]);
}
