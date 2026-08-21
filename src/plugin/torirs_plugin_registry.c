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
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LUA;

static struct ToriRS_PluginDef const* const PLUGIN_TABLE[] = {
    &TORIRS_PLUGIN_TILEIND,
    &TORIRS_PLUGIN_LUA,
};

void
PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host)
{
    assert(host);

    for( size_t i = 0; i < sizeof(PLUGIN_TABLE) / sizeof(PLUGIN_TABLE[0]); i++ )
        PluginHost_Register(host, PLUGIN_TABLE[i]);
}
