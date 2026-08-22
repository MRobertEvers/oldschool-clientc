#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>

/*
 * All Settings > Activities > General, the four rows that put a NAME over an
 * npc:
 *
 *   258  NPC highlight - Display name             off / normal / bold
 *   263  NPC highlight - Text colour
 *   264  Display all NPC names above their body   off / normal / bold
 *   266  NPC names text colour
 *
 * Everything else in the NPC highlight family is the cache's now -- it installs
 * the "Tag" and "Tag-All" client ops, keeps the tagged npcs in highlight npc
 * group 6, and encodes the outline (setting 260) and the tile (259) as that
 * group's flags. `nxt-highlight` draws all of it.
 *
 * The NAME is the one part that does not come through a highlight flag,
 * because in the reference it is not drawn as one: clientscript 6695 builds a
 * WORLD-ANCHORED interface component for it, through the `_7200..7211` family
 * that this client does not implement. So the two name rows are drawn here
 * instead, off the same group membership, and this file goes away the day that
 * family lands.
 *
 * ---- what "tagged" means here ----
 *
 * Setting 258 names the npcs the user tagged, and that list lives in the
 * cache's highlight state -- not in a list of this plugin's own. Reading it
 * back through api->highlight_next is what keeps the two in step: tagging is
 * done by a cache script through a client op, and a plugin holding its own
 * copy would drift the first time the cache cleared the group.
 */

/** Pixels above the overhead anchor. The anchor is where a health bar's top
 *  edge goes; a name exactly there overlaps the bar of an npc in combat. */
#define NXT_NAME_LIFT 8

static struct ToriRS_PluginApi const* g_api;

/**
 * Bold, with one font.
 *
 * The overlay layer draws in the hitsplat font and has no second face, so bold
 * is the same glyphs struck twice a pixel apart. It is a faux bold and looks
 * like one up close; the row's job is to make a name stand out in a crowd, a
 * second pass does that, and the alternative is a row that visibly does
 * nothing.
 */
static void
nxt_name_draw(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int fine_x,
    int fine_z,
    int height,
    char const* name,
    int mode,
    uint32_t rgb)
{
    int screen_x;
    int screen_y;

    assert(ctx);

    if( mode == NXT_NAME_OFF || !name || name[0] == '\0' )
        return;
    if( !g_api->project(ctx, fine_x, fine_z, height + NXT_NAME_LIFT, &screen_x, &screen_y) )
        return;

    g_api->draw_text(ctx, surface, screen_x, screen_y, name, rgb);
    if( mode == NXT_NAME_BOLD )
        g_api->draw_text(ctx, surface, screen_x + 1, screen_y, name, rgb);
}

static enum ToriRS_PluginVerdict
nxt_npc_names_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    int const tagged_mode = g_api->varbit(ctx, NXT_VARBIT_NPC_NAME);
    int const all_mode = g_api->varbit(ctx, NXT_VARBIT_NPC_NAMES_ALL);
    uint32_t tagged_rgb;
    uint32_t all_rgb;
    int iter;

    assert(ctx);
    assert(ev);

    if( tagged_mode == NXT_NAME_OFF && all_mode == NXT_NAME_OFF )
        return TORIRS_PLUGIN_PASS;

    tagged_rgb = g_api->setting_color(ctx, NXT_VARP_NPC_TEXT_COLOR, NXT_COL_NPC_HIGHLIGHT);
    all_rgb = g_api->setting_color(ctx, NXT_VARP_NPC_NAMES_COLOR, NXT_COL_NPC_HIGHLIGHT);

    /*
     * 264 first, over every npc, then 258 over the tagged ones.
     *
     * That order is what stops a tagged npc being named twice in two colours:
     * 258's pass draws second and simply covers 264's, which is also what the
     * two rows mean read together -- the tagged colour is the more specific
     * statement.
     */
    if( all_mode != NXT_NAME_OFF )
    {
        struct ToriRS_PluginNpcSnap npc;
        iter = -1;
        for( ;; )
        {
            iter = g_api->npc_next(ctx, iter, &npc);
            if( iter < 0 )
                break;
            nxt_name_draw(
                ctx,
                ev->surface,
                npc.fine_x,
                npc.fine_z,
                g_api->element_height(ctx, npc.element_id),
                npc.name,
                all_mode,
                all_rgb);
        }
    }

    if( tagged_mode != NXT_NAME_OFF )
    {
        struct ToriRS_PluginHighlightItem item;
        iter = -1;
        for( ;; )
        {
            iter = g_api->highlight_next(ctx, iter, &item);
            if( iter < 0 )
                break;
            if( item.kind != TORIRS_PLUGIN_HL_NPC )
                continue;
            nxt_name_draw(
                ctx,
                ev->surface,
                item.fine_x,
                item.fine_z,
                item.overhead_height,
                item.name,
                tagged_mode,
                tagged_rgb);
        }
    }
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_npc_names_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_npc_names_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_NPC_NAMES = {
    .name = "nxt-npc-names",
    .title = "NPC names (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_npc_names_init,
    .shutdown = NULL,
};
