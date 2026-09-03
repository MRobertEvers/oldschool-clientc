#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin_v2.h"

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
    struct ToriRS_ApiV2* api,
    char const* name,
    int absent)
{
    int id = -1;
    return !api->cache.named_id(api, "varbit", name, &id)
               ? absent
               : api->cache.varbit(api, id);
}

static void
nxt_bird_nest_spawn(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_GroundItemSnapshot const* item)
{
    struct ToriRS_PlayerSnapshot me;

    (void)state;
    assert(api);
    assert(item);

    /* INVERTED (`param_1084` on struct_3737): the feature is on at 0. */
    if( nxt_bird_nest_varbit(api, NXT_VARBIT_BIRD_NEST, 1) != 0 )
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

    api->core.notify(api, "A bird's nest falls out of the tree.");
}

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_NXT_BIRD_NEST = {
    .struct_size = sizeof(TORIRS_PLUGIN_NXT_BIRD_NEST),
    .id = "nxt-bird-nest",
    .title = "Bird nest notification (All Settings)",
    .version = "1.0.0",
    .state_size = 0,
    .config = NULL,
    .flags = TORIRS_PLUGIN_V2_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_item_spawn = nxt_bird_nest_spawn,
    },
};
