#include "plugin/torirs_plugin_registry.h"

#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
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

#define PLUGIN_TABLE_COUNT (sizeof(PLUGIN_TABLE) / sizeof(PLUGIN_TABLE[0]))

/*
 * The plugin a selection env var names, or NULL when there is no such plugin.
 *
 * NULL is the answer to a lookup and not a swallowed error -- the one caller
 * turns it into the loud failure below.
 *
 * The widget probe is searched beside the table although it is not in it: it
 * is registered by TORIRS_WIDGET_DEMO rather than shipped, but "run only this
 * plugin" is the same question about it as about any other, and answering it
 * with silence is what made TORIRS_PLUGIN_ONLY=widget-demo boot a client with
 * nothing in it.
 */
static struct ToriRS_PluginDef const*
plugin_registry_find(char const* id)
{
    assert(id);

    if( strcmp(id, TORIRS_PLUGIN_WIDGET_DEMO.id) == 0 )
        return &TORIRS_PLUGIN_WIDGET_DEMO;
    for( size_t i = 0; i < PLUGIN_TABLE_COUNT; i++ )
        if( strcmp(id, PLUGIN_TABLE[i]->id) == 0 )
            return PLUGIN_TABLE[i];
    return NULL;
}

/*
 * A selection nobody can honour is a typo, and it stops the client here.
 *
 * It used to register nothing and return: the client booted, logged in, ran to
 * the frame limit and exited 0 with no plugin and no diagnostic, which is
 * indistinguishable from a host that has stopped working. The capture gate
 * catches it only when someone remembers to pass --plugin-id, and a person
 * running the binary by hand got no signal at all. Name the value, name what
 * would have been accepted, and stop -- a run whose whole subject is one
 * plugin has nothing left to measure once the plugin is not there.
 */
static void
plugin_registry_reject(char const* variable, char const* value, char const* accepted)
{
    assert(variable);
    assert(value);
    assert(accepted);

    fprintf(stderr, "plugin: %s=%s -- %s\n", variable, value, accepted);
    fprintf(stderr, "plugin: known plugin ids: %s", TORIRS_PLUGIN_WIDGET_DEMO.id);
    for( size_t i = 0; i < PLUGIN_TABLE_COUNT; i++ )
        fprintf(stderr, ", %s", PLUGIN_TABLE[i]->id);
    fprintf(stderr, "\n");
    fprintf(stderr,
        "plugin: a scripted plugin is selected with TORIRS_PLUGIN_ONLY=lua and a "
        "TORIRS_PLUGIN_MANIFEST naming it.\n");
    exit(2);
}

/*
 * What TORIRS_WIDGET_DEMO's VALUE asks for.
 *
 * It used to be a presence test with two special values, so
 * TORIRS_WIDGET_DEMO=0 -- the obvious way to write "off" -- turned the probe
 * on and dragged the entire shipped table in with it. Every value now says one
 * thing, and a value that says nothing recognisable is a typo rather than a
 * vote for the default.
 */
enum PluginWidgetDemoMode
{
    PLUGIN_WIDGET_DEMO_OFF,
    PLUGIN_WIDGET_DEMO_WITH_TABLE,
    PLUGIN_WIDGET_DEMO_ONLY,
    PLUGIN_WIDGET_DEMO_LUA,
    PLUGIN_WIDGET_DEMO_INVALID,
};

static enum PluginWidgetDemoMode
plugin_widget_demo_mode(char const* value)
{
    assert(value);

    /* Empty is unset written out longhand, and unset is off. */
    if( value[0] == '\0' )
        return PLUGIN_WIDGET_DEMO_OFF;
    if( strcmp(value, "0") == 0 || strcmp(value, "off") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "false") == 0 )
        return PLUGIN_WIDGET_DEMO_OFF;
    if( strcmp(value, "1") == 0 || strcmp(value, "on") == 0 ||
        strcmp(value, "yes") == 0 || strcmp(value, "true") == 0 )
        return PLUGIN_WIDGET_DEMO_WITH_TABLE;
    if( strcmp(value, "only") == 0 )
        return PLUGIN_WIDGET_DEMO_ONLY;
    if( strcmp(value, "lua") == 0 )
        return PLUGIN_WIDGET_DEMO_LUA;
    return PLUGIN_WIDGET_DEMO_INVALID;
}

void
PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host)
{
    assert(host);

    char const* only = getenv("TORIRS_PLUGIN_ONLY");
    if( only && *only )
    {
        struct ToriRS_PluginDef const* selected = plugin_registry_find(only);

        if( selected )
        {
            (void)PluginHost_Register(host, selected);
            return;
        }
        /* Does not return. */
        plugin_registry_reject(
            "TORIRS_PLUGIN_ONLY", only, "no plugin with that id is built in");
    }

    char const* demo = getenv("TORIRS_WIDGET_DEMO");
    enum PluginWidgetDemoMode const mode =
        demo ? plugin_widget_demo_mode(demo) : PLUGIN_WIDGET_DEMO_OFF;

    if( mode == PLUGIN_WIDGET_DEMO_INVALID )
        /* Does not return. */
        plugin_registry_reject(
            "TORIRS_WIDGET_DEMO",
            demo,
            "expected one of: 0/off/no/false, 1/on/yes/true (the probe beside the "
            "shipped table), only (the probe alone), lua (the Lua host alone)");

    /* The Lua host alone -- the probe's scripted twin lives in a script, and
     * the host is what runs it. */
    if( mode == PLUGIN_WIDGET_DEMO_LUA )
    {
        (void)PluginHost_Register(host, &TORIRS_PLUGIN_LUA);
        return;
    }
    if( mode == PLUGIN_WIDGET_DEMO_ONLY || mode == PLUGIN_WIDGET_DEMO_WITH_TABLE )
        (void)PluginHost_Register(host, &TORIRS_PLUGIN_WIDGET_DEMO);
    if( mode == PLUGIN_WIDGET_DEMO_ONLY )
        return;

    for( size_t i = 0; i < PLUGIN_TABLE_COUNT; i++ )
    {
        (void)PluginHost_Register(host, PLUGIN_TABLE[i]);
    }
}
