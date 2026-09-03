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

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_CLIENT_SETTINGS;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_FEATURE_FLAGS;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_TILEIND;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_ORBS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_ITEM_STATS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER;
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
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_NXT_HIGHLIGHT;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_NXT_BIRD_NEST;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_NXT_CANNON_AMMO;

enum PluginRegistryApi
{
    PLUGIN_REGISTRY_V1 = 1,
    PLUGIN_REGISTRY_V2 = 2,
};

struct PluginRegistryEntry
{
    enum PluginRegistryApi api;
    struct ToriRS_PluginDef const* v1;
    struct ToriRS_PluginDefV2 const* v2;
};

static struct PluginRegistryEntry const PLUGIN_TABLE[] = {
    /*
     * FIRST, and their position is load-bearing: the roster lists plugins in
     * registration order and these two are where the CLIENT's own knobs live,
     * so they belong at the top of the list rather than sorted in among the
     * extras. Both are `.essential`, so neither has a switch -- see
     * ToriRS_PluginDef::essential.
     *
     * Display settings first because that is the order a person reads them in:
     * how the client LOOKS, then how it BEHAVES. The order of START is settled
     * by priority and not by this table, and there Feature Flags goes first --
     * a flag has to be in force before anything that reads one has run, and
     * nothing reads a display setting at START at all.
     */
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_CLIENT_SETTINGS },
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_FEATURE_FLAGS },
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_TILEIND },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_MINIMAP_ORBS, NULL },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_XP_ORBS, NULL },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_GAMEFRAME, NULL },
    /* Beside the desktop provider for presentation only. Both publish their
     * offers before startup; the one saved canonical id selects is the only
     * provider the host runs, independent of this table order. */
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_MOBILE_GAMEFRAME, NULL },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_ITEM_STATS, NULL },
    /* The two trackers, beside the other readouts. Both are PAGES and neither
     * draws on the canvas, which is why they are on by default where
     * xp-drop-orbs is not: nothing appears on a fresh install until the player
     * opens the panel and asks. */
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_XP_TRACKER, NULL },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_LOOT_TRACKER, NULL },
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_NXT_HIGHLIGHT },
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_NXT_BIRD_NEST },
    { PLUGIN_REGISTRY_V2, NULL, &TORIRS_PLUGIN_NXT_CANNON_AMMO },
    { PLUGIN_REGISTRY_V1, &TORIRS_PLUGIN_LUA, NULL },
};

void
PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host)
{
    assert(host);

    for( size_t i = 0; i < sizeof(PLUGIN_TABLE) / sizeof(PLUGIN_TABLE[0]); i++ )
    {
        struct PluginRegistryEntry const* entry = &PLUGIN_TABLE[i];
        if( entry->api == PLUGIN_REGISTRY_V2 )
            (void)PluginHost_RegisterV2(host, entry->v2);
        else
            (void)PluginHost_Register(host, entry->v1);
    }
}
