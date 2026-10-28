#include "plugin/torirs_plugin_registry.h"

#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Statically linked plugins.
 *
 * There is no dynamic loading: the web build has no dlopen, and nothing else
 * in this tree loads code at runtime either. The C ABI is versioned so a
 * dlopen lane can be added on native later without any plugin changing, but
 * until something needs it, a table is the whole mechanism.
 *
 * Scripted plugins do not appear here. The Lua runtime host is one entry below
 * and registers a further V2 plugin per script it loads.
 */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_WIDGET_DEMO;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS;
extern struct ToriRS_PluginDef const TORIRS_FEATURE_FLAGS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND;
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
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_COX_HELPER;

static struct ToriRS_PluginDef const* const PLUGIN_TABLE[] = {
    /*
     * FIRST, and their position is load-bearing: the roster lists plugins in
     * registration order and these two are where the CLIENT's own knobs live,
     * so they belong at the top of the list rather than sorted in among the
     * extras. Both carry TORIRS_PLUGIN_ESSENTIAL, so neither has a switch.
     *
     * Display settings first because that is the order a person reads them in:
     * how the client LOOKS, then how it BEHAVES. The order of START is settled
     * by priority and not by this table, and there Feature Flags goes first --
     * a flag has to be in force before anything that reads one has run, and
     * nothing reads a display setting at START at all.
     */
    &TORIRS_PLUGIN_CLIENT_SETTINGS,
    &TORIRS_FEATURE_FLAGS,
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
     * draws on the canvas. */
    &TORIRS_PLUGIN_XP_TRACKER,
    &TORIRS_PLUGIN_LOOT_TRACKER,
    &TORIRS_PLUGIN_NXT_HIGHLIGHT,
    &TORIRS_PLUGIN_NXT_BIRD_NEST,
    &TORIRS_PLUGIN_NXT_CANNON_AMMO,
    &TORIRS_PLUGIN_NXT_COX_HELPER,
    &TORIRS_PLUGIN_LUA,
};

/* The builtins above, on their own: what a host with the user plugins
 * switched off still registers. */
static struct ToriRS_PluginDef const* const BUILTIN_TABLE[] = {
    &TORIRS_PLUGIN_NXT_HIGHLIGHT,
    &TORIRS_PLUGIN_NXT_BIRD_NEST,
    &TORIRS_PLUGIN_NXT_CANNON_AMMO,
    &TORIRS_PLUGIN_NXT_COX_HELPER,
};

void
PluginRegistry_RegisterBuiltins(struct ToriRS_PluginHost* host)
{
    assert(host);
    for( size_t i = 0; i < sizeof(BUILTIN_TABLE) / sizeof(BUILTIN_TABLE[0]); i++ )
        (void)PluginHost_Register(host, BUILTIN_TABLE[i]);
}

void
PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host)
{
    assert(host);

    char const* only=getenv("TORIRS_PLUGIN_ONLY");
    if( only && *only )
    {
        /* A comma list, registered in table order: "lua,nxt-cox-helper" is
         * the quest driver plus the one builtin a visual check is about. */
        for( size_t i=0; i<sizeof(PLUGIN_TABLE)/sizeof(PLUGIN_TABLE[0]); ++i )
        {
            size_t const len=strlen(PLUGIN_TABLE[i]->id);
            for( char const* at=only; *at; )
            {
                char const* end=strchr(at,',');
                size_t const n=end ? (size_t)(end-at) : strlen(at);
                if( n==len && strncmp(at,PLUGIN_TABLE[i]->id,len)==0 )
                {
                    (void)PluginHost_Register(host,PLUGIN_TABLE[i]);
                    break;
                }
                at+=n;
                if( *at==',' )
                    at++;
            }
        }
        return; /* The capture gate rejects a missing selected plugin. */
    }

    if( getenv("TORIRS_WIDGET_DEMO") && strcmp(getenv("TORIRS_WIDGET_DEMO"), "lua") == 0 )
    {
        (void)PluginHost_Register(host, &TORIRS_PLUGIN_LUA);
        return;
    }
    if( getenv("TORIRS_WIDGET_DEMO") )
    {
        (void)PluginHost_Register(host, &TORIRS_PLUGIN_WIDGET_DEMO);
        if( strcmp(getenv("TORIRS_WIDGET_DEMO"), "only") == 0 ) return;
    }

    for( size_t i = 0; i < sizeof(PLUGIN_TABLE) / sizeof(PLUGIN_TABLE[0]); i++ )
    {
        (void)PluginHost_Register(host, PLUGIN_TABLE[i]);
    }
}
