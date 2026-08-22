#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * All Settings > Activities > General, setting 453:
 *
 *   "Highlight poll booths -- When enabled, poll booths will be highlighted
 *    when there is an active poll you have not voted in."
 *
 * The varbit is INVERTED: `param_1084` is set on this row, so the checkbox
 * shows `1 - varbit` and the feature is ON when `%varbit9538` reads 0.
 * Clientscript 8319 agrees, and lights the booths on exactly that test.
 *
 * The second half of the sentence is the SERVER's. Whether a poll is open and
 * whether this player has voted are facts only it holds -- 8319 reads
 * `%varbit4337` for the first and this server never writes it -- so the
 * highlight here is unconditional while the setting is on, and the "you have
 * not voted" gate is a server feature that does not exist yet. Written down
 * rather than faked: a booth that lights up forever teaches the user to
 * ignore it.
 *
 * ---- why this is not the cache's job, yet ----
 *
 * The cache would do it: 8319 sets up loc highlight group 16 and 8320 puts
 * each booth in it with `highlight_loc_on(_6802, _6801, 16, 1)`. But nothing
 * in the cache CALLS 8320 -- it is invoked by the reference client when a loc
 * comes into view, a per-loc hook this client does not raise -- so group 16 is
 * set up and stays empty. Until that hook exists, this is the implementation.
 *
 * ---- found by NAME, not by an id list ----
 *
 * This cache holds dozens of separate poll booth locs -- `pollbooth_blue`,
 * `clanwars_tournament_pollbooth_blue` and a long tail of per-city copies, all
 * called "Poll booth" -- and the list is not stable between revisions. A
 * hardcoded id table would be right for one cache and quietly incomplete for
 * every other, and "quietly incomplete" for a highlighter means the booth in
 * front of you is the one it does not know about.
 *
 * The name is what the game itself shows on the right-click, so matching it is
 * matching what the player would call the thing. It costs one strcmp per loc
 * per frame, which for a busy scene is a few thousand comparisons that almost
 * all fail on the first character.
 */

#define NXT_POLL_BOOTH_NAME "Poll booth"

/**
 * The outline colour.
 *
 * Setting 453 has no colour row of its own, so this is the client's to pick.
 * It is the same cyan the NPC highlight rows default to (`param_1230` on
 * struct_318), because the two are the same kind of mark -- "the client is
 * pointing at this for you" -- and a client that answered that with a
 * different colour per feature would read as a decorated screen rather than a
 * legible one.
 */
#define NXT_POLL_BOOTH_RGB NXT_COL_NPC_HIGHLIGHT

/** A wash as well as an outline. A booth is a box among other boxes, and the
 *  outline alone reads as part of the scenery at a distance. */
#define NXT_POLL_BOOTH_FILL_ALPHA 48

static struct ToriRS_PluginApi const* g_api;

static enum ToriRS_PluginVerdict
nxt_poll_booths_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    int iter = -1;

    assert(ctx);
    assert(ev);

    /* INVERTED -- see nxt_activities.h. Read the plain way this highlighted
     * every booth in the game for anyone who had switched the setting OFF. */
    if( !NXT_ON_INVERTED(g_api, ctx, NXT_VARBIT_POLL_BOOTHS) )
        return TORIRS_PLUGIN_PASS;

    for( ;; )
    {
        struct ToriRS_PluginLocSnap loc;

        iter = g_api->loc_next(ctx, iter, &loc);
        if( iter < 0 )
            break;

        /* A non-interactive loc is scenery nothing can click -- a wall, floor
         * decor. Marking one would point at something that cannot be used. */
        if( !loc.interactive || loc.element_id < 0 )
            continue;
        if( strcmp(loc.name, NXT_POLL_BOOTH_NAME) != 0 )
            continue;

        g_api->draw_hull(
            ctx,
            ev->surface,
            loc.element_id,
            NXT_POLL_BOOTH_RGB,
            NXT_POLL_BOOTH_FILL_ALPHA,
            TORIRS_PLUGIN_HULL_MESH);
    }
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_poll_booths_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_poll_booths_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_POLL_BOOTHS = {
    .name = "nxt-poll-booths",
    .title = "Highlight poll booths (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_poll_booths_init,
    .shutdown = NULL,
};
