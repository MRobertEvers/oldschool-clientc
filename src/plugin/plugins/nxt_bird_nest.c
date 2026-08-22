#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

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

static struct ToriRS_PluginApi const* g_api;

static bool
nxt_is_bird_nest(int obj_id)
{
    for( size_t i = 0; i < sizeof(NXT_BIRD_NESTS) / sizeof(NXT_BIRD_NESTS[0]); i++ )
        if( NXT_BIRD_NESTS[i] == obj_id )
            return true;
    return false;
}

static enum ToriRS_PluginVerdict
nxt_bird_nest_spawn(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvObj* ev = (struct ToriRS_PluginEvObj*)event;
    struct ToriRS_PluginPlayerSnap me;

    assert(ctx);
    assert(ev);

    /* INVERTED (`param_1084` on struct_3737): the feature is on at 0. */
    if( !NXT_ON_INVERTED(g_api, ctx, NXT_VARBIT_BIRD_NEST) )
        return TORIRS_PLUGIN_PASS;
    if( !nxt_is_bird_nest(ev->obj.obj_id) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;

    /*
     * The player's TRUE tile, not the drawn one.
     *
     * A nest lands on the tile the server thinks you are on, and between
     * server ticks the draw position is somewhere between two tiles -- so
     * comparing against it would miss the drop for most of every step.
     */
    if( ev->obj.level != me.level || ev->obj.tile_x != me.true_x ||
        ev->obj.tile_z != me.true_z )
        return TORIRS_PLUGIN_PASS;

    g_api->notify(ctx, "A bird's nest falls out of the tree.");
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_bird_nest_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_OBJ_SPAWN, nxt_bird_nest_spawn, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST = {
    .name = "nxt-bird-nest",
    .title = "Bird nest notification (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_bird_nest_init,
    .shutdown = NULL,
};
