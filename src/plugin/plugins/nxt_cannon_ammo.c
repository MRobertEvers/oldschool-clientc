#include "plugin/plugins/nxt_activities.h"
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
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
 *
 * ---- what the layer took away, and what it had to grow ----
 *
 * This builtin is the reason `Porcelain_SettingValue` exists. The layer had
 * one named-var verb and it answered a BOOLEAN over a varbit, which covers the
 * two notification switches and none of the three numbers this file actually
 * reads: the cannon's coordinate and its ball count are varps, and the
 * threshold is a 0..310 slider. Written against the boolean verb alone, this
 * plugin would have kept `cache.named_id` + `cache.varp` by hand for three of
 * its five reads -- so "a plugin can stop reading varps and varbits by hand"
 * would have been false here, for a builtin, which is the claim the family was
 * ported to test.
 *
 * With it: five named reads a tick become five reads and ZERO name lookups
 * after the first tick, the absent answer is stated per call rather than
 * re-derived, and a revision with no cannon varps says so once as a finding
 * instead of being indistinguishable from a cannon nobody is firing.
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
    struct Porcelain* porcelain;
    int last_ammo;
    int last_coord;
    /** Does this profile declare the cannon varps. A BOOT fact, read out of
     *  the profile's own refs; @see the identical field on the bird-nest
     *  builtin for why it is held rather than re-asked. */
    bool available;
};

/** One spelling per row, used for the requirement and for the read. */
#define NXT_CANNON_CAPABILITY "varp:" NXT_VARP_CANNON_COORD
#define NXT_CANNON_FEATURE "cannon notifications"

/** Its own definition, named before on_start hands it to Porcelain_Open. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO;

static void
nxt_cannon_sample(struct ToriRS_Api* api, void* state_ptr, uint64_t elapsed_ms)
{
    struct NxtCannonState* state = state_ptr;
    int coord;
    int ammo;
    int threshold;
    int previous;

    assert(api);
    assert(state);
    (void)api;
    /* Zero on a cadence timer: the interval IS the tick, and a plugin that
     * read this would be measuring the server's pace, not its own. */
    (void)elapsed_ms;

    /* Absent reads as "no cannon" and as "the switch is off", which is the
     * state in which this builtin does nothing at all -- the right answer for
     * a revision that has no cannon varps to read. Stated per call, because
     * the OFF answer is the caller's fact and not the layer's. */
    coord = Porcelain_SettingValue(state->porcelain, "varp:" NXT_VARP_CANNON_COORD, 0);
    ammo = Porcelain_SettingValue(state->porcelain, "varp:" NXT_VARP_CANNON_AMMO, 0);
    threshold =
        Porcelain_SettingValue(state->porcelain, NXT_VARBIT_CANNON_LOW_AMOUNT, 0);
    previous = state->last_ammo;

    /*
     * No cannon: forget the count rather than remembering it.
     *
     * Picking the cannon up and putting it down again starts a new cannon with
     * a new load, and comparing the new count against the old one would
     * announce a "drop" that is really a different cannon. `coord <= 0` covers
     * both zero and the cache's native null.
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
     * Out of ammo wins over low on ammo.
     *
     * A cannon going from 3 to 0 crosses both, and saying "running low" and
     * "empty" in the same tick is two lines for one event -- the second is
     * the one that is true now.
     */
    if( ammo == 0 )
    {
        if( Porcelain_Setting(state->porcelain, NXT_VARBIT_CANNON_NO_AMMO_NOTIFY, 0) )
            Porcelain_Notify(state->porcelain, "cannon_empty", coord,
                "Your cannon has run out of cannonballs.");
        return;
    }

    /* Crossing the threshold, not merely being under it: firing every tick
     * below the line would bury the chatbox. */
    if( threshold > 0 && ammo <= threshold && previous > threshold &&
        Porcelain_Setting(state->porcelain, NXT_VARBIT_CANNON_LOW_NOTIFY, 0) )
    {
        char line[96];
        snprintf(
            line,
            sizeof(line),
            "Your cannon is running low on cannonballs: %d left.",
            ammo);
        /* Subject is the coordinate, so the same cannon crossing the line
         * twice inside one fence is one line and two cannons are two. */
        Porcelain_Notify(state->porcelain, "cannon_low", coord, line);
    }
}

/*
 * The plugin forwarding its own tick.
 *
 * Porcelain installs no callbacks: the definition below is the host's
 * registration, and `Porcelain_Tick` is what turns it into the cadence
 * `Porcelain_EveryServerTick` registered against. The indirection buys one
 * thing that matters -- the fence-less-lane rule is stated once, in the layer,
 * instead of once per plugin: the tick fires on EVERY lane now, after
 * SERVER_TICK_END where the wire has it and after PLAYER_INFO where it does
 * not, never both.
 *
 * The fence comes FIRST, so a notification raised by the sample below lands in
 * the new coalescing window rather than sharing the previous tick's.
 */
static void
nxt_cannon_tick(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct NxtCannonState* state = state_ptr;

    assert(api);
    assert(state);
    (void)event;
    if( !state->available )
        return;
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
    Porcelain_Tick(state->porcelain, PORCELAIN_SERVER_TICK);
}

static void
nxt_cannon_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtCannonState* state = state_ptr;

    assert(api);
    assert(state);
    state->last_ammo = -1;
    state->last_coord = 0;
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_NXT_CANNON_AMMO, state);
    assert(state->porcelain);

    /*
     * The availability line this builtin never had.
     *
     * Its two siblings said something at start and this one said nothing, so a
     * lane without the cannon varps was indistinguishable from a lane with a
     * cannon nobody is firing. Every builtin reports unavailability the same
     * way now, and it is a finding rather than a log line because a log line
     * is something no capture reads.
     *
     * The declaration comes BEFORE the requirement, and the order is not
     * cosmetic: a finding is labelled expected-or-not at the moment it is
     * RECORDED, and its trace line -- the one every capture reads -- is
     * written at birth.
     * `Porcelain_ExpectUnsupported` marks the table afterwards, which fixes
     * what `Porcelain_Findings` answers and does nothing at all for the line
     * already in the log. So the lane is asked plainly first and the
     * limitation stated before the requirement records its refusal. Two
     * capability calls at boot; @see the port's report.
     */
    if( !Porcelain_Has(state->porcelain, NXT_CANNON_CAPABILITY) )
        Porcelain_ExpectUnsupported(state->porcelain, NXT_CANNON_FEATURE,
            "this revision declares no cannon varps, so there is no cannon to watch");
    state->available =
        Porcelain_Require(state->porcelain, NXT_CANNON_CAPABILITY, NXT_CANNON_FEATURE);
    if( !state->available )
        return;
    Porcelain_EveryServerTick(state->porcelain, nxt_cannon_sample, state);
}

static void
nxt_cannon_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtCannonState* state = state_ptr;

    assert(api);
    assert(state);
    (void)api;
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
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
        .on_stop = nxt_cannon_stop,
        /* Server tick, not render frame: cannon ammo is server state. */
        .on_server_tick = nxt_cannon_tick,
    },
};
