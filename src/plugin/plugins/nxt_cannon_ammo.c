#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

/**
 * The count as of the previous server tick.
 *
 * -1 means "nothing seen yet", which is not the same as zero: a cannon whose
 * count is already 0 when the plugin starts must not fire an out-of-ammo line
 * for a state it merely arrived in. Every notification below is an EDGE.
 */
struct NxtCannonState
{
    int last_ammo;
    int last_coord;
};

static int
nxt_cannon_named(
    struct ToriRS_Api* api,
    char const* kind,
    char const* name,
    int absent)
{
    int id = -1;
    if( !api->cache.named_id(api, kind, name, &id) )
        return absent;
    return strcmp(kind, "varbit") == 0 ? api->cache.varbit(api, id)
                                        : api->cache.varp(api, id);
}

static void
nxt_cannon_tick(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct NxtCannonState* state = state_ptr;

    /* Absent on this cache reads as "no cannon", which is the state in which
     * this builtin does nothing at all -- the right answer for a revision that
     * has no cannon varps to read. */
    int const coord = nxt_cannon_named(api, "varp", NXT_VARP_CANNON_COORD, 0);
    int const ammo = nxt_cannon_named(api, "varp", NXT_VARP_CANNON_AMMO, 0);
    int const threshold =
        nxt_cannon_named(api, "varbit", NXT_VARBIT_CANNON_LOW_AMOUNT, 0);
    int const previous = state->last_ammo;

    (void)event;
    assert(api);
    assert(state);

    /*
     * No cannon: forget the count rather than remembering it.
     *
     * Picking the cannon up and putting it down again starts a new cannon with
     * a new load, and comparing the new count against the old one would
     * announce a "drop" that is really a different cannon.
     */
    if( coord <= 0 )
    {
        state->last_ammo = -1;
        state->last_coord = 0;
        return;
    }

    state->last_ammo = ammo;
    if( state->last_coord != coord || previous < 0 )
    {
        /* First tick with this coordinate, including direct replacement
         * between ticks. Whatever it is loaded with is a state,
         * not an event. */
        state->last_coord = coord;
        return;
    }
    if( ammo >= previous )
        return; /* loading it is not news. */

    /*
     * Out of ammo wins over low on ammo -- WHEN IT SPEAKS.
     *
     * A cannon going from 3 to 0 crosses both, and saying "running low" and
     * "empty" in the same tick is two lines for one event -- the second is
     * the one that is true now, so it takes the tick.
     *
     * It only takes the tick when it is going to say something. Setting 250
     * unticked means the empty line never speaks, and swallowing the low line
     * for a line nobody will hear turns setting 248 off for the one drop that
     * matters most: 15 -> 0 in a single server tick is a downward crossing of
     * the user's own threshold, and with 250 off it was the only warning
     * there was ever going to be. So the branch returns on the notify, not on
     * the value.
     */
    if( ammo == 0 &&
        nxt_cannon_named(api, "varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY, 0) !=
            0 )
    {
        api->core.notify(api, "Your cannon has run out of cannonballs.");
        return;
    }

    /* Crossing the threshold, not merely being under it: firing every tick
     * below the line would bury the chatbox. Zero is under the line like any
     * other count -- reached here only when the out-of-ammo row is off. */
    if( threshold > 0 && ammo <= threshold && previous > threshold &&
        nxt_cannon_named(api, "varbit", NXT_VARBIT_CANNON_LOW_NOTIFY, 0) != 0 )
    {
        char line[96];
        snprintf(
            line,
            sizeof(line),
            "Your cannon is running low on cannonballs: %d left.",
            ammo);
        api->core.notify(api, line);
    }
}

/*
 * Say what this revision can do, once -- the same line its two siblings print.
 *
 * Both varps are the boot profile's, by name, and on a revision that has
 * neither (rs289lc has no cannon at all) every tick below reads the absent
 * answer and this builtin does nothing for the whole session. Silence there
 * is indistinguishable from a builtin that failed to start, which is exactly
 * the failure mode this family is written against: a support log has to be
 * able to tell "this revision has no cannon" from "the plugin is broken".
 *
 * The line says what resolved, not what the settings say. on_start runs at
 * boot, before a single VARP has arrived, so any value read here would be the
 * unloaded zero rather than the user's choice.
 */
static void
nxt_cannon_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtCannonState* state = state_ptr;
    int ammo_id = -1;
    int coord_id = -1;
    bool have_ammo;
    bool have_coord;

    assert(api);
    assert(state);
    state->last_ammo = -1;
    state->last_coord = 0;

    have_ammo = api->cache.named_id(api, "varp", NXT_VARP_CANNON_AMMO, &ammo_id);
    have_coord =
        api->cache.named_id(api, "varp", NXT_VARP_CANNON_COORD, &coord_id);
    if( have_ammo && have_coord )
        api->core.log(
            api,
            "cannon ammo notifications: watching varp %d (cannonballs) and "
            "varp %d (your cannon)",
            ammo_id,
            coord_id);
    else
        api->core.log(
            api,
            "cannon ammo notifications: unavailable, this revision has no %s "
            "varp",
            have_ammo ? NXT_VARP_CANNON_COORD : NXT_VARP_CANNON_AMMO);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO = {
    .struct_size = sizeof(TORIRS_PLUGIN_NXT_CANNON_AMMO),
    .id = "nxt-cannon-ammo",
    .title = "Cannon ammo notifications (All Settings)",
    .version = "1.0.0",
    .state_size = sizeof(struct NxtCannonState),
    .config = NULL,
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = nxt_cannon_start,
        /* Server tick, not render frame: cannon ammo is server state. */
        .on_server_tick = nxt_cannon_tick,
    },
};
