#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>

/*
 * Draw what the CACHE asked to be marked.
 *
 * This is the other half of the HIGHLIGHT_* opcode family (7000..7044). The
 * engine records the groups the cache's scripts describe and resolves their
 * subjects against live world state (see src/game/rs_highlight.h); everything
 * that reaches here has already had every appearance decision made for it by a
 * clientscript that read the user's own setting and the user's own colour.
 *
 * So this plugin has NO opinions and no settings. It is the smallest possible
 * thing that can be called a renderer: turn each resolved item into the draw
 * calls its flags name. The moment it starts deciding what colour something
 * should be, or whether a group is worth drawing, the settings panel has
 * stopped being the place those questions are answered.
 *
 * Roughly thirty rows of All Settings > Activities come through here -- the
 * tile indicators, the tile markers, the npc highlight, Agility obstacles,
 * quest start points, fishing spots, poll booths, the Blast Furnace, the clue
 * scroll helper -- and several of them name subjects (which loc is an Agility
 * obstacle, which npc is a fishing spot) that this client has no table for and
 * would otherwise have had to build. See NXT_CLIENT_PLUGINS.md.
 *
 * ## It yields, and claims nothing
 *
 * This is the BASELINE renderer -- what the cache asked for -- so it holds no
 * entity claims of its own: a claim is for a plugin that wants an entity to
 * itself, and the cache's highlight groups are the thing a plugin like that
 * is overriding. The host's draw_hull gate does the yielding: an entity whose
 * APPEARANCE another plugin holds is silently skipped here, and comes back the
 * moment that claim goes. Nothing in this file has to know.
 */

/*
 * The flag bits, as read off clientscript 4624 and confirmed against 5198.
 * Restated here rather than included from rs_highlight.h: this plugin is
 * written against the contract like any other, and the contract's own
 * documentation of ToriRS_PluginHighlightItem::flags is where they are stated
 * for a plugin author.
 */
#define NXT_HL_MODEL_OUTLINE 1
#define NXT_HL_TILE_OUTLINE 2
#define NXT_HL_MODEL_FILL 4
#define NXT_HL_TILE_FILL 8

static struct ToriRS_PluginApi const* g_api;

/*
 * The reference's rules, not this file's guesses.
 *
 * An OUTLINE needs its flag AND a non-zero thickness; a FILL needs its flag
 * AND a non-zero opacity. Either one alone draws nothing, which is what makes
 * the cache's two odd-looking families work: the mouseover groups run at
 * opacity 0 (an outline has no wash) and the hovered tile at thickness 0 (a
 * wash with no border).
 *
 * Opacity is already 0..255 -- the opcode handler clamps it there. This used
 * to scale it by 255/100 on the belief that it was a percent, which made every
 * wash in the game 2.55x too opaque.
 */
static bool
nxt_hl_outline(struct ToriRS_PluginHighlightItem const* item, int flag)
{
    return (item->flags & flag) != 0 && item->outline_width != 0;
}

static bool
nxt_hl_fill(struct ToriRS_PluginHighlightItem const* item, int flag)
{
    return (item->flags & flag) != 0 && item->opacity != 0;
}

static enum ToriRS_PluginVerdict
nxt_highlight_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    int iter = -1;

    assert(ctx);
    assert(ev);

    for( ;; )
    {
        struct ToriRS_PluginHighlightItem item;
        bool model_outline;
        bool model_fill;
        bool tile_outline;
        bool tile_fill;

        iter = g_api->highlight_next(ctx, iter, &item);
        if( iter < 0 )
            break;

        model_outline = nxt_hl_outline(&item, NXT_HL_MODEL_OUTLINE);
        model_fill = nxt_hl_fill(&item, NXT_HL_MODEL_FILL);
        tile_outline = nxt_hl_outline(&item, NXT_HL_TILE_OUTLINE);
        tile_fill = nxt_hl_fill(&item, NXT_HL_TILE_FILL);

        /*
         * The model, when the group asked for one and the subject has one.
         *
         * A TILE item never does -- a marked tile is a place, not a thing --
         * and the engine reports that as element_id -1 rather than as a flag,
         * so the two tests are not the same and both are needed: a group can
         * carry the model bits and still resolve to a bare tile.
         */
        if( item.element_id >= 0 && (model_outline || model_fill) )
            g_api->draw_hull(
                ctx,
                ev->surface,
                item.element_id,
                item.rgb,
                model_fill ? item.opacity : 0,
                TORIRS_PLUGIN_HULL_MESH);

        if( tile_outline || tile_fill )
        {
            /* The whole footprint, not the anchor tile: true_x/true_z is the
             * SW corner, and a 2x2 npc marked on one tile looks misplaced
             * rather than partly drawn. Per tile, because draw_tile samples
             * the terrain per tile and that is what keeps a marker coplanar on
             * a slope. */
            for( int dz = 0; dz < item.size_z; dz++ )
                for( int dx = 0; dx < item.size_x; dx++ )
                    g_api->draw_tile(
                        ctx,
                        ev->surface,
                        item.tile_x + dx,
                        item.tile_z + dz,
                        item.level,
                        item.rgb,
                        item.rgb,
                        tile_fill ? item.opacity : 0);
        }
    }
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_highlight_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_highlight_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT = {
    .name = "nxt-highlight",
    .title = "Cache highlights (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_highlight_init,
    .shutdown = NULL,
};
