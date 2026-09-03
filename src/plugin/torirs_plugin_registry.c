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
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_MINIMAP_ORBS;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_ORBS;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_GAMEFRAME;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_MOBILE_GAMEFRAME;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_ITEM_STATS;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LOOT_TRACKER;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LUA;

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

static struct ToriRS_PluginDefV2 const* const PLUGIN_TABLE[] = {
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
    &TORIRS_PLUGIN_CLIENT_SETTINGS,
    &TORIRS_PLUGIN_FEATURE_FLAGS,
    &TORIRS_PLUGIN_TILEIND,
    &TORIRS_PLUGIN_MINIMAP_ORBS,
    &TORIRS_PLUGIN_XP_ORBS,
    &TORIRS_PLUGIN_GAMEFRAME,
    /* Beside the desktop provider for presentation only. Both publish their
     * offers before startup; the one saved canonical id selects is the only
     * provider the host runs, independent of this table order. */
    &TORIRS_PLUGIN_MOBILE_GAMEFRAME,
    &TORIRS_PLUGIN_ITEM_STATS,
    /* The two trackers, beside the other readouts. Both are PAGES and neither
     * draws on the canvas, which is why they are on by default where
     * xp-drop-orbs is not: nothing appears on a fresh install until the player
     * opens the panel and asks. */
    &TORIRS_PLUGIN_XP_TRACKER,
    &TORIRS_PLUGIN_LOOT_TRACKER,
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
    {
        (void)PluginHost_RegisterV2(host, PLUGIN_TABLE[i]);
    }
}
