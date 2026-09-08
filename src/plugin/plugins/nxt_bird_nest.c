#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * All Settings > Activities > Skills, setting 189:
 *
 *   "Bird nest notification -- When enabled, a notification will be displayed
 *    if you obtain a bird nest drop while cutting down trees."
 *
 * A bird nest from woodcutting does not go into your inventory: it drops on
 * the ground under you, silently, and is easy to walk away from. That is the
 * whole reason the setting exists, and it is also what makes it implementable
 * from here -- the nest arriving IS a ground-item event, which the plugin
 * layer already reports. Both item arrivals and increases in a previously
 * observed stack can represent a new drop, so both are listened for below.
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

static bool
nxt_is_bird_nest(int obj_id)
{
    for( size_t i = 0; i < sizeof(NXT_BIRD_NESTS) / sizeof(NXT_BIRD_NESTS[0]); i++ )
        if( NXT_BIRD_NESTS[i] == obj_id )
            return true;
    return false;
}

static int
nxt_bird_nest_varbit(
    struct ToriRS_Api* api,
    char const* name,
    int absent)
{
    int id = -1;
    return !api->cache.named_id(api, "varbit", name, &id)
               ? absent
               : api->cache.varbit(api, id);
}

/**
 * The nest stacks this client has been told about, and how many were in each.
 *
 * OBJ_ADD is an arrival, including a second unstackable nest whose packet
 * still carries count1. It is announced by on_item_spawn even if the scene
 * coalesces the picture into an existing same-id slot. An actual OBJ_COUNT
 * update instead reports a new absolute quantity through on_item_changed.
 *
 * The count alone cannot tell that growth from a shrink: somebody taking one
 * of a pair is the same callback with the same fields. So the previous count
 * is kept per stack, and only an increase is an event.
 *
 * A stack whose baseline was never seen -- the builtin was switched on after
 * it landed, or every slot was live -- is recorded silently rather than
 * announced. An unknown baseline is a state, not an event; the same rule the
 * cannon builtin states as `last_ammo = -1`.
 */
#define NXT_BIRD_NEST_TRACKED 16

struct NxtBirdNestStack
{
    /** 0 when the slot is free. No nest above has id 0, so a free slot never
     *  matches a lookup. */
    int obj_id;
    int tile_x;
    int tile_z;
    int level;
    int count;
};

struct NxtBirdNestState
{
    struct NxtBirdNestStack seen[NXT_BIRD_NEST_TRACKED];
    /** Where the next eviction lands when every slot is live. */
    int evict;
};

static struct NxtBirdNestStack*
nxt_bird_nest_find(
    struct NxtBirdNestState* state,
    struct ToriRS_GroundItemSnapshot const* item)
{
    assert(state);
    assert(item);
    for( int i = 0; i < NXT_BIRD_NEST_TRACKED; i++ )
    {
        struct NxtBirdNestStack* slot = &state->seen[i];
        if( slot->obj_id == item->obj_id && slot->tile_x == item->tile_x &&
            slot->tile_z == item->tile_z && slot->level == item->level )
            return slot;
    }
    return NULL;
}

static void
nxt_bird_nest_remember(
    struct NxtBirdNestState* state,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct NxtBirdNestStack* slot;

    assert(state);
    assert(item);

    slot = nxt_bird_nest_find(state, item);
    for( int i = 0; !slot && i < NXT_BIRD_NEST_TRACKED; i++ )
        if( state->seen[i].obj_id == 0 )
            slot = &state->seen[i];
    if( !slot )
    {
        /* Every slot live: take the oldest claim rather than stop tracking.
         * Dropping a baseline costs one missed notice; keeping a stale one
         * costs a wrong one, and a wrong one is the worse of the two. */
        slot = &state->seen[state->evict];
        state->evict = (state->evict + 1) % NXT_BIRD_NEST_TRACKED;
    }
    slot->obj_id = item->obj_id;
    slot->tile_x = item->tile_x;
    slot->tile_z = item->tile_z;
    slot->level = item->level;
    slot->count = item->count;
}

/*
 * The player's TRUE tile, not the drawn one.
 *
 * A nest lands on the tile the server thinks you are on, and between server
 * ticks the draw position is somewhere between two tiles -- so comparing
 * against it would miss the drop for most of every step.
 */
static bool
nxt_bird_nest_under_player(
    struct ToriRS_Api* api,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct ToriRS_PlayerSnapshot me;

    assert(api);
    assert(item);
    if( !api->world.local_player(api, &me) )
        return false;
    return item->level == me.level && item->tile_x == me.true_x &&
           item->tile_z == me.true_z;
}

static void
nxt_bird_nest_announce(struct ToriRS_Api* api)
{
    assert(api);

    /* INVERTED (`param_1084` on struct_3737): the feature is on at 0. */
    if( nxt_bird_nest_varbit(api, NXT_VARBIT_BIRD_NEST, 1) != 0 )
    {
        /*
         * The one line that tells "switched off" from "broken".
         *
         * on_start cannot say it. The host runs on_start at boot, before a
         * single VARP has arrived, and an unloaded varbit reads 0 -- which for
         * this inverted row is ON, so a start line quoting the value says
         * "currently on" in every session including the ones where the user
         * has it off. Said HERE it is the value the decision was actually made
         * on, at the one moment the answer matters.
         */
        api->core.log(
            api,
            "bird nest under you: %s says off, staying quiet",
            NXT_VARBIT_BIRD_NEST);
        return;
    }

    api->core.notify(api, "A bird's nest falls out of the tree.");
}

static void
nxt_bird_nest_spawn(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct NxtBirdNestState* state = state_ptr;

    assert(api);
    assert(state);
    assert(item);

    if( !nxt_is_bird_nest(item->obj_id) )
        return;
    nxt_bird_nest_remember(state, item);
    if( !nxt_bird_nest_under_player(api, item) )
        return;
    nxt_bird_nest_announce(api);
}

/*
 * An increase carried by an actual stack-count update.
 *
 * Only an increase over a baseline this plugin saw for itself: the same
 * callback carries the shrink when you pick one up, and the count that arrives
 * with no baseline is whatever was already lying there.
 */
static void
nxt_bird_nest_changed(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct NxtBirdNestState* state = state_ptr;
    struct NxtBirdNestStack* known;
    bool grew;

    assert(api);
    assert(state);
    assert(item);

    if( !nxt_is_bird_nest(item->obj_id) )
        return;
    known = nxt_bird_nest_find(state, item);
    grew = known != NULL && item->count > known->count;
    nxt_bird_nest_remember(state, item);
    if( !grew )
        return;
    if( !nxt_bird_nest_under_player(api, item) )
        return;
    nxt_bird_nest_announce(api);
}

/*
 * The stack is gone: forget its count.
 *
 * Keeping it would make the next nest to land there look like a stack that
 * had merely been re-sent, and the drop after you pick one up is the most
 * ordinary one there is.
 */
static void
nxt_bird_nest_despawn(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct NxtBirdNestState* state = state_ptr;
    struct NxtBirdNestStack* known;

    (void)api;
    assert(api);
    assert(state);
    assert(item);

    if( !nxt_is_bird_nest(item->obj_id) )
        return;
    known = nxt_bird_nest_find(state, item);
    if( known )
        known->obj_id = 0;
}

/*
 * Say what this revision can do, once. The setting is a named varbit of the
 * boot profile; a cache without it (rs289lc has no All Settings) makes the
 * feature explicitly unavailable rather than silently on or off.
 *
 * What it does NOT say is whether the row is ticked. This runs at boot, before
 * login, and an unloaded varbit answers 0 -- ON for this inverted row -- so
 * the value here is not the user's. The line that reports the user's answer is
 * in nxt_bird_nest_announce, where the value has arrived.
 */
static void
nxt_bird_nest_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtBirdNestState* state = state_ptr;
    int id = -1;

    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    if( api->cache.named_id(api, "varbit", NXT_VARBIT_BIRD_NEST, &id) )
        api->core.log(api,
            "bird nest notification: setting varbit %d, read at each drop",
            id);
    else
        api->core.log(api, "bird nest notification: unavailable, this revision has no %s setting",
            NXT_VARBIT_BIRD_NEST);
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
        .on_item_spawn = nxt_bird_nest_spawn,
        /* Quantity updates are distinct from OBJ_ADD arrivals. */
        .on_item_changed = nxt_bird_nest_changed,
        .on_item_despawn = nxt_bird_nest_despawn,
    },
};
