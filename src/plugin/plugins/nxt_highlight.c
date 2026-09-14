#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

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
 * APPEARANCE another plugin holds is skipped here, and comes back the moment
 * that claim goes.
 *
 * ## What Porcelain is here for, in a plugin that owns no control
 *
 * Nothing in this file describes anything, so there is no reconciler, no
 * fence and no commit. Two things brought the layer in anyway, and both are
 * about REFUSALS being readable:
 *
 *   - `Porcelain_Hull` and `Porcelain_Tile` answer. `(void)draw->world_hull`
 *     was the shipped spelling because there was nothing to read; what that
 *     cost is not an error message, it is half the outlines in a mass of
 *     tagged npcs, gone, with the renderer still reporting itself armed.
 *   - `Porcelain_Require` replaces a `core.log` line nothing machine-readable
 *     ever saw with a finding every capture carries. The capability it asks
 *     for is the RIGHT one now: `scripts.callbacks` meant "is this the CS2
 *     lane", which is only the same question as "are there highlight groups"
 *     while nothing else can record one.
 */

/*
 * The flag bits, as read off clientscript 4624 and confirmed against 5198.
 * Restated here rather than included from rs_highlight.h: this plugin is
 * written against the contract like any other, and the contract's own
 * documentation of ToriRS_HighlightItem::flags is where they are stated
 * for a plugin author.
 */
#define NXT_HL_MODEL_OUTLINE 1
#define NXT_HL_TILE_OUTLINE 2
#define NXT_HL_MODEL_FILL 4
#define NXT_HL_TILE_FILL 8

/** Named once, because `Porcelain_Require` and `Porcelain_ExpectUnsupported`
 *  must agree on the string: the declaration is matched against the finding's
 *  detail, and two spellings would be a declaration that never fires. */
#define NXT_HL_FEATURE "cache highlights"

/** Its own definition, named before on_start hands it to Porcelain_Open. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT;

struct NxtHighlightState
{
    struct Porcelain* porcelain;
};

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
nxt_hl_outline(struct ToriRS_HighlightItem const* item, int flag)
{
    return (item->flags & flag) != 0 && item->outline_width != 0;
}

static bool
nxt_hl_fill(struct ToriRS_HighlightItem const* item, int flag)
{
    return (item->flags & flag) != 0 && item->opacity != 0;
}

static void
nxt_highlight_draw(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_Graphics* draw)
{
    struct NxtHighlightState* state = state_ptr;
    int iter = -1;

    assert(api);
    assert(api->game);
    assert(state);
    assert(draw);

    for( ;; )
    {
        struct ToriRS_HighlightItem item;
        bool model_outline;
        bool model_fill;
        bool tile_outline;
        bool tile_fill;

        iter = api->game->highlight_next(api, iter, &item);
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
            (void)Porcelain_Hull(
                state->porcelain,
                draw,
                item.element_id,
                item.rgb,
                model_fill ? item.opacity : 0,
                TORIRS_HULL_MESH);

        if( tile_outline || tile_fill )
        {
            /* The whole footprint, not the anchor tile: true_x/true_z is the
             * SW corner, and a 2x2 npc marked on one tile looks misplaced
             * rather than partly drawn. Per tile, because draw_tile samples
             * the terrain per tile and that is what keeps a marker coplanar on
             * a slope.
             *
             * This nested loop is also why the frame's 512-item allotment is
             * reached here by arithmetic rather than by accident, and the
             * refusal it answers with is the whole reason this call goes
             * through the layer. Kept going rather than broken out of: the
             * budget is per FRAME and the finding is coalesced, so the cost of
             * carrying on is one refused call per tile and the benefit is that
             * a later item with a claim-free model still gets its hull. */
            for( int dz = 0; dz < item.size_z; dz++ )
                for( int dx = 0; dx < item.size_x; dx++ )
                    (void)Porcelain_Tile(
                        state->porcelain,
                        draw,
                        item.tile_x + dx,
                        item.tile_z + dz,
                        item.level,
                        item.rgb,
                        item.rgb,
                        tile_fill ? item.opacity : 0);
        }
    }
}

/*
 * The groups come from CS2 scripts, so a revision without them has none to
 * draw. Said once, as a FINDING and not a log line: a capture carries findings
 * and reads no log, and "unavailable" was the one thing about this plugin a
 * capture could never see.
 */
static void
nxt_highlight_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtHighlightState* state = state_ptr;

    assert(api);
    assert(state);
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_NXT_HIGHLIGHT, state);
    assert(state->porcelain);

    /*
     * `highlight_groups`, not `scripts.callbacks`.
     *
     * The old name asked "is this the CS2 lane" and meant "are there highlight
     * groups", which works exactly as long as nothing else can record one. The
     * capability is a single expression over engine facts -- CS2 ui logic AND
     * a profile that declares `[script:highlight_hover_tile]` -- and it lives
     * in the adapter, where the rule belongs, not in a lane test here.
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
    if( !Porcelain_Has(state->porcelain, "highlight_groups") )
        Porcelain_ExpectUnsupported(state->porcelain, NXT_HL_FEATURE,
            "this revision records no CS2 highlight groups, so there is nothing to draw");
    (void)Porcelain_Require(state->porcelain, "highlight_groups", NXT_HL_FEATURE);
    /*
     * The answer is NOT kept and the draw path is NOT gated on it.
     *
     * A capability that is false means nothing can record a group, so the walk
     * below finds none and the renderer is idle by arithmetic. A gate here
     * would be a second, weaker copy of that fact -- and the day a profile
     * records groups without declaring the script row, the gate would be the
     * thing hiding them. The declaration says the feature is off; the empty
     * list is what makes it off.
     */
}

static void
nxt_highlight_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtHighlightState* state = state_ptr;

    assert(api);
    assert(state);
    (void)api;
    /* Nothing was described and nothing was claimed, so this is the findings
     * read-out and the handle going back. */
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT = {
    .struct_size = sizeof(TORIRS_PLUGIN_NXT_HIGHLIGHT),
    .id = "nxt-highlight",
    .title = "Cache highlights (All Settings)",
    .version = "1.0.0",
    .state_size = sizeof(struct NxtHighlightState),
    .config = NULL,
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = nxt_highlight_start,
        .on_stop = nxt_highlight_stop,
        .on_draw_world = nxt_highlight_draw,
    },
};
