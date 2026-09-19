#include "plugin/plugins/nxt_activities.h"
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stddef.h>

/*
 * All Settings > Activities > Skills, setting 189:
 *
 *   "Bird nest notification -- When enabled, a notification will be displayed
 *    if you obtain a bird nest drop while cutting down trees."
 *
 * A bird nest from woodcutting does not go into your inventory: it drops on
 * the ground under you, silently, and is easy to walk away from. That is the
 * whole reason the setting exists, and it is also what makes it implementable
 * from here -- the nest arriving IS a ground-item spawn, which the plugin
 * layer already reports.
 *
 * No cache script reads this varbit. It is one of twenty-nine rows in the
 * category that nothing in the cache acts on, so the client owns it whole.
 *
 * ---- "while cutting down trees" ----
 *
 * Not enforced, deliberately. The client cannot see why a nest appeared: there
 * is no woodcutting state on this side, and a nest that fell from a tree and
 * one dropped by another player look identical on the ground. The reference
 * has the same problem and solves it the same way -- the notification is about
 * the ITEM, and a nest appearing under you is worth a line either way.
 *
 * What IS enforced is that it appeared under YOU. A nest on someone else's
 * tile across the clearing is not your drop, and a notification for it would
 * be noise every time a wintertodt crowd chopped.
 *
 * ---- what the layer took away ----
 *
 * Three things, and none of them is a control:
 *
 *   - The setting. `Porcelain_Setting(PORCELAIN_SETTING_INVERTED)` is the
 *     named read, the display inversion and the absent-is-OFF answer in one
 *     call, and it resolves the id ONCE instead of once per ground item. In a
 *     busy drop zone that was two host calls per obj that could never answer
 *     differently.
 *   - The availability line. A `core.log` at start is something no capture
 *     reads and no test can assert on; `Porcelain_Require` makes it a finding,
 *     which is the channel every other builtin's unavailability now uses.
 *   - The announcement. `Porcelain_Notify` coalesces on (kind, subject) within
 *     a fence, so two nests of one kind landing on one tick are one line.
 *
 * ---- and the fence a plugin with no UI still needs ----
 *
 * `Porcelain_Notify`'s coalescing window is the layer's frame counter, and only
 * `Porcelain_Fence` moves it. A plugin that never fences has that counter
 * frozen at zero, which turns "one line per nest per fence" into "one line per
 * nest EVER" -- the second nest of a session would be silently swallowed. So
 * this fences, on the SERVER TICK and not the render frame: a ground item
 * arrives on a tick, the spawns for a tick all precede its end, and a tick is
 * 600 ms of fence instead of sixty a second for a plugin that describes
 * nothing.
 */

/**
 * The nests, by id.
 *
 * Read out of the cache rather than guessed at:
 *
 *     grep -E "^[0-9]+=bird_nest" OSRS-Content/osrs239-content/configs/all.obj.compack
 *
 * Ten of them, and the list is stable in a way a NAME match would not be: all
 * ten are called "Bird nest" and so is the empty one you get from a Wintertodt
 * crate, but the seed and ring nests are separate records because their
 * contents differ. Matching on the name would also catch any future record
 * that happens to share it.
 *
 * STILL IN C, and the ledger calls that a gap: the family's own header says
 * rows are named and not numbered, and these ten are numbered. The proposal is
 * a revconfig list row (`[objs:bird_nests]`) reached the same way the setting
 * is. It is not done here because a list-valued revconfig row does not exist --
 * `cache.named_id` answers ONE id per name -- so writing it would mean adding a
 * row kind to the profile format for one plugin, and that is the owner's call.
 * Declared rather than left implied: @see nxt_bird_nest_start.
 */
static int const NXT_BIRD_NESTS[] = {
    5070,  /* bird_nest_egg_red */
    5071,  /* bird_nest_egg_green */
    5072,  /* bird_nest_egg_blue */
    5073,  /* bird_nest_seeds */
    5074,  /* bird_nest_ring */
    5075,  /* bird_nest_empty */
    7413,  /* bird_nest_cheapseeds */
    13653, /* bird_nest_decentseeds */
    22798, /* bird_nest_seeds_jan2019 */
    22800, /* bird_nest_decentseeds_jan2019 */
};

/** The row this builtin's switch is, as a capability name. One spelling for
 *  the requirement and for the read: `Porcelain_Setting` takes the bare name
 *  and `Porcelain_Require` the `varbit:` form of the same row. */
#define NXT_NEST_CAPABILITY "varbit:" NXT_VARBIT_BIRD_NEST
#define NXT_NEST_FEATURE "bird nest notification"

/** Its own definition, named before on_start hands it to Porcelain_Open. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST;

struct NxtBirdNestState
{
    struct Porcelain* porcelain;
    /**
     * Does this profile declare the row at all.
     *
     * A BOOT fact: `varbit:<name>` is answered out of the profile's refs, which
     * are loaded with the profile and not filled in later -- it is not one of
     * the two capabilities (`item_bonuses`, `native_orbs`) that are facts about
     * state that ARRIVES. Held so the spawn path can cost nothing at all on a
     * lane with no All Settings, instead of recording an absence per obj.
     */
    bool available;
};

static bool
nxt_is_bird_nest(int obj_id)
{
    for( size_t i = 0; i < sizeof(NXT_BIRD_NESTS) / sizeof(NXT_BIRD_NESTS[0]); i++ )
        if( NXT_BIRD_NESTS[i] == obj_id )
            return true;
    return false;
}

static void
nxt_bird_nest_spawn(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct NxtBirdNestState* state = state_ptr;
    struct ToriRS_PlayerSnapshot me;

    assert(api);
    assert(state);
    assert(item);

    /* The profile has no such row: the feature is off, and asking again per
     * obj would be an absence finding per drop for an answer settled at boot.
     * @see NxtBirdNestState::available. */
    if( !state->available )
        return;
    /* INVERTED (`param_1084` on struct_3737): the feature is on at 0, and the
     * layer's INVERTED flag is where that display convention now lives. */
    if( !Porcelain_Setting(state->porcelain, NXT_VARBIT_BIRD_NEST, PORCELAIN_SETTING_INVERTED) )
        return;
    if( !nxt_is_bird_nest(item->obj_id) )
        return;
    if( !api->world.local_player(api, &me) )
        return;

    /*
     * The player's TRUE tile, not the drawn one.
     *
     * A nest lands on the tile the server thinks you are on, and between
     * server ticks the draw position is somewhere between two tiles -- so
     * comparing against it would miss the drop for most of every step.
     */
    if( item->level != me.level || item->tile_x != me.true_x ||
        item->tile_z != me.true_z )
        return;

    /* Subject is the obj id, so two DIFFERENT nests landing together are two
     * lines and the same nest reported twice in one fence is one. */
    Porcelain_Notify(state->porcelain, "bird_nest", item->obj_id,
        "A bird's nest falls out of the tree.");
}

/*
 * The fence, and nothing else.
 *
 * This plugin has no description to reconcile, so the tick is here purely to
 * move the coalescing window `Porcelain_Notify` keys on. @see the header
 * comment: without it the second nest of a session is swallowed.
 */
static void
nxt_bird_nest_tick(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct NxtBirdNestState* state = state_ptr;

    assert(api);
    assert(state);
    (void)event;
    if( !state->available )
        return;
    Porcelain_Fence(state->porcelain);
    /* A fence without a commit is a finding on the NEXT fence, and it is the
     * right one: a handle that wrote nothing still owes the epoch its turn. */
    Porcelain_Commit(api);
}

/*
 * Say what this revision can do, once, in the channel a capture reads.
 *
 * The setting is a named varbit of the boot profile; a cache without it
 * (rs289lc has no All Settings) makes the feature explicitly unavailable
 * rather than silently on or off -- and "silently off" is the failure mode an
 * inverted row makes worse, because an unset var reads as a feature that
 * switched itself ON for a user who never asked.
 */
static void
nxt_bird_nest_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtBirdNestState* state = state_ptr;

    assert(api);
    assert(state);
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_NXT_BIRD_NEST, state);
    assert(state->porcelain);

    /*
     * The declaration comes BEFORE the requirement, and the order is not
     * cosmetic.
     *
     * A finding is labelled expected-or-not at the moment it is RECORDED, and
     * its trace line -- the one every capture reads -- is written at birth.
     * `Porcelain_ExpectUnsupported` marks the table afterwards, which fixes
     * what `Porcelain_Findings` answers and does nothing at all for the line
     * already in the log. So the lane is asked plainly first and the
     * limitation stated before the requirement records its refusal. Two
     * capability calls at boot; @see the port's report.
     */
    if( !Porcelain_Has(state->porcelain, NXT_NEST_CAPABILITY) )
        Porcelain_ExpectUnsupported(state->porcelain, NXT_NEST_FEATURE,
            "this revision declares no bird_nest setting, so there is no switch to obey");
    state->available =
        Porcelain_Require(state->porcelain, NXT_NEST_CAPABILITY, NXT_NEST_FEATURE);

    /*
     * The ten ids, declared as the limitation they are.
     *
     * The list is right and it is also the wrong SHAPE: it is ten numbers in C
     * in a family whose own header says rows are named. A profile that
     * renumbered the objs would leave this silently matching nothing -- which
     * looks exactly like a setting somebody switched off -- and no test in this
     * tree can tell those apart. @see NXT_BIRD_NESTS for the proposed row.
     */
    Porcelain_ExpectUnsupported(state->porcelain, "bird nest ids from the profile",
        "no revconfig row kind holds a LIST of ids, so the ten are literals here");
}

static void
nxt_bird_nest_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtBirdNestState* state = state_ptr;

    assert(api);
    assert(state);
    (void)api;
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST = {
    .struct_size = sizeof(TORIRS_PLUGIN_NXT_BIRD_NEST),
    .id = "nxt-bird-nest",
    .title = "Bird nest notification (All Settings)",
    .version = "1.0.0",
    .state_size = sizeof(struct NxtBirdNestState),
    .config = NULL,
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = nxt_bird_nest_start,
        .on_stop = nxt_bird_nest_stop,
        /* Server tick, not render frame: the fence exists to bound an
         * announcement about a server event. @see nxt_bird_nest_tick. */
        .on_server_tick = nxt_bird_nest_tick,
        .on_item_spawn = nxt_bird_nest_spawn,
    },
};
