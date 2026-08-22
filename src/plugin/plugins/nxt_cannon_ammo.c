#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

/*
 * All Settings > Activities > Combat, the three cannon ammunition rows:
 *
 *   248  Cannon low on ammo notification
 *   249  Cannon low on ammo amount          (a slider, 0..310)
 *   250  Cannon out of ammo notification
 *
 * Nothing in the cache reads any of the three. What the cache DOES have is the
 * data, and its own names are what pin every number here:
 *
 *     varp 3          rockthrower                        the cannonball count
 *     varp 3551       ownedmcannon_temp                  where your cannon is
 *     varbit 14174    cannon_hud_disabled                setting 247
 *     varbit 14175    cannon_low_notification_enabled    setting 248
 *     varbit 14176    cannon_low_amount                  setting 249
 *     varbit 14177    cannon_no_ammo_notification_enabled  setting 250
 *
 * Clientscript 6676 -- the cannon HUD -- confirms varp 3 independently: it
 * prints `tostring(%var3)` and colours it red at 5 or fewer, amber at 15 or
 * fewer, green above that.
 *
 * ---- the threshold has no default, and that is correct ----
 *
 * Setting 249's slider runs 0..310 (enums 4601 and 4600) and starts at 0.
 * With it at 0 the LOW notification never fires, because there is no amount
 * the user has called low yet -- 250 still fires at empty. Picking a default
 * of ten here would be this client inventing a preference; the row exists
 * precisely so the number is the user's.
 */

static struct ToriRS_PluginApi const* g_api;

/**
 * The count as of the previous server tick.
 *
 * -1 means "nothing seen yet", which is not the same as zero: a cannon whose
 * count is already 0 when the plugin starts must not fire an out-of-ammo line
 * for a state it merely arrived in. Every notification below is an EDGE.
 */
static int g_last_ammo = -1;
static int g_had_cannon;

static enum ToriRS_PluginVerdict
nxt_cannon_tick(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    /* Absent on this cache reads as "no cannon", which is the state in which
     * this builtin does nothing at all -- the right answer for a revision that
     * has no cannon varps to read. */
    int const has_cannon = nxt_varp(g_api, ctx, NXT_VARP_CANNON_COORD, 0) != 0;
    int const ammo = nxt_varp(g_api, ctx, NXT_VARP_CANNON_AMMO, 0);
    int const threshold = nxt_varbit(g_api, ctx, NXT_VARBIT_CANNON_LOW_AMOUNT, 0);
    int const previous = g_last_ammo;

    assert(ctx);

    /*
     * No cannon: forget the count rather than remembering it.
     *
     * Picking the cannon up and putting it down again starts a new cannon with
     * a new load, and comparing the new count against the old one would
     * announce a "drop" that is really a different cannon.
     */
    if( !has_cannon )
    {
        g_last_ammo = -1;
        g_had_cannon = 0;
        return TORIRS_PLUGIN_PASS;
    }

    g_last_ammo = ammo;
    if( !g_had_cannon || previous < 0 )
    {
        /* First tick with this cannon. Whatever it is loaded with is a state,
         * not an event. */
        g_had_cannon = 1;
        return TORIRS_PLUGIN_PASS;
    }
    if( ammo >= previous )
        return TORIRS_PLUGIN_PASS; /* loading it is not news. */

    /*
     * Out of ammo wins over low on ammo.
     *
     * A cannon going from 3 to 0 crosses both, and saying "running low" and
     * "empty" in the same tick is two lines for one event -- the second is
     * the one that is true now.
     */
    if( ammo == 0 )
    {
        if( NXT_ON(g_api, ctx, NXT_VARBIT_CANNON_NO_AMMO_NOTIFY) )
            g_api->notify(ctx, "Your cannon has run out of cannonballs.");
        return TORIRS_PLUGIN_PASS;
    }

    /* Crossing the threshold, not merely being under it: firing every tick
     * below the line would bury the chatbox. */
    if( threshold > 0 && ammo <= threshold && previous > threshold &&
        NXT_ON(g_api, ctx, NXT_VARBIT_CANNON_LOW_NOTIFY) )
    {
        char line[96];
        snprintf(
            line,
            sizeof(line),
            "Your cannon is running low on cannonballs: %d left.",
            ammo);
        g_api->notify(ctx, line);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_cannon_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;
    g_last_ammo = -1;
    g_had_cannon = 0;
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_cannon_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, nxt_cannon_start, NULL);
    /* The SERVER tick, not the frame: the count only ever changes because the
     * server said so, and sampling it per frame would compare a value against
     * itself sixty times for every one time it moved. */
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SERVER_TICK, nxt_cannon_tick, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO = {
    .name = "nxt-cannon-ammo",
    .title = "Cannon ammo notifications (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_cannon_init,
    .shutdown = NULL,
};
