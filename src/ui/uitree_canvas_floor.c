#include "ui/uitree_canvas_floor.h"

#include "ui/uitree_canvas_measure.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include "perf/torirs_perf.h"

#include <assert.h>
#include <stdint.h>

#if defined(TORIRS_CANVAS_CAPTURE)
#include "../tools/perf/canvas_chain_capture.u.h"
#endif
int
UITree_MeasureRightChromeStripWidth(struct UITree const* tree)
{
#if defined(TORIRS_CANVAS_CAPTURE)
    canvas_chain_capture(tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    if( tree && UITree_CanvasQueryCompactEnabled() )
        return UITree_CanvasMeasureCompact(tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H)
            .strip;
    /*
     * Memo, keyed on the tree publication this answer was read from.
     *
     * The scan below is over every component -- 7,142 in a logged-in frame --
     * and App_SyncFixedChromeInset asks for it once per frame from the main
     * loop, so it ran in full on every frame whether or not anything had moved.
     * An EIP profile of an in-world frame put it at 4.1% of non-raster work.
     *
     * The key terms are the ones the scan actually reads: `dirty_gen` covers
     * the hide/free flags, `layout_resolve_seq` covers the resolved boxes (a
     * re-layout moves them without touching dirty_gen -- the same reason the
     * emit retain gate needs both), `component_count` covers a tree that grew,
     * and `generation` covers one that was REBUILT. That last one is not
     * redundant: UITree_Clear reclaims every slot without shrinking the array
     * or bumping dirty_gen, so on the other three terms a cleared tree looks
     * exactly like the tree it used to be, and the whole strip of a logged-in
     * frame stays in the canvas floor of a login screen that has no chrome on
     * it at all. Keyed on the tree pointer too, so two trees cannot read each
     * other's answer.
     */
    static struct UITree const* memo_tree = NULL;
    static uint32_t memo_dirty_gen;
    static uint32_t memo_layout_seq;
    static uint32_t memo_count;
    static uint32_t memo_generation;
    static int memo_value;

    int canvas_w;
    int canvas_h;
    int best;
    uint32_t i;

    if( !tree || tree->component_count == 0 )
        return 0;

    if( memo_tree == tree && memo_dirty_gen == tree->dirty_gen &&
        memo_layout_seq == tree->layout_resolve_seq &&
        memo_count == tree->component_count && memo_generation == tree->generation )
        return memo_value;

    canvas_w = UITREE_LAYOUT_ROOT_W;
    canvas_h = UITREE_LAYOUT_ROOT_H;
    best = 0;

    /* Script 5355 docks the popout strip on the canvas right edge at full
     * height. Measure that geometry rather than naming interface 728 or the
     * 42/312 widths the CS2 embeds — those are content, and the strip width
     * changes when a panel opens. The mode checks matter: mounted interface
     * roots also commonly fill from a positive X to the right edge. Treating
     * one of those fill-width roots as chrome makes the canvas feed back into
     * its own next width and grow every frame. The strip itself is fixed-width,
     * parent-height, and right-anchored. */
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int right;
        int w;

        /* Geometry first, visibility last. Every test here is an independent
         * reject, so the order is free to choose — and all of these read the
         * component record already in cache, while the visibility test below
         * chases `parent` to the root, a fresh cache line per hop. Almost
         * nothing in the tree is a full-height right-docked fixed-width box, so
         * paying for the ancestor walk on every component — 7,142 of them in a
         * logged-in frame — was most of this function. */
        w = c->position.abs_w;
        if( w <= 0 || c->position.abs_x <= 0 || c->position.width_mode != 0 ||
            c->position.height_mode != 1 || c->position.x_mode != 2 )
            continue;
        /* Near full canvas height: the strip, not a minimap orb or tab icon. */
        if( c->position.abs_h < canvas_h - 2 )
            continue;
        right = c->position.abs_x + w;
        if( right != canvas_w )
            continue;
        /* A wider candidate cannot lose to the ancestor walk, so only ask the
         * question when the answer can change the result. */
        if( w <= best )
            continue;
        /* Ancestors too: a speculatively baked panel (the CS2 runtime bakes a
         * pack the moment a script touches it) is hidden at its group root,
         * while the right-docked column inside it keeps a clear flag and a
         * full-height box — the exact signature this loop looks for. Measuring
         * that column grew the fixed canvas by a panel that was never open. */
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CHROME_STRIP_VISCHECK, 1);
        if( UITree_ComponentHiddenOrOrphaned(tree, (int32_t)i) )
            continue;
        best = w;
    }

    memo_tree = tree;
    memo_dirty_gen = tree->dirty_gen;
    memo_layout_seq = tree->layout_resolve_seq;
    memo_count = tree->component_count;
    memo_generation = tree->generation;
    memo_value = best;
    return best;
}

/*
 * The width the LANE's own frame needs, when the canvas it was given is too
 * narrow for it.
 *
 * The resizable OldSchool toplevels are not fluid all the way down: each one
 * carves the popout strip off the canvas (a full-height layer authored as
 * "parent width - <strip>") and then lays a single fixed-size block inside
 * what is left, clamped up to 765x503 by its own resize script. Handed less
 * than that, the script writes the floor anyway and the layout CENTRES the
 * block, so it hangs off both sides -- and every lane widget anchored to its
 * right edge (the XP counter, the tracker overlays) lands to the RIGHT of the
 * area a plugin frame was handed, under that frame's own right-hand furniture.
 *
 * Nobody notices this natively because APP_CANVAS_MIN_W is that same 765: the
 * canvas floor already holds the block. A plugin frame that declares
 * TORIRS_FRAME_CANVAS_WINDOW replaces that floor with its OWN minimum, and a
 * frame written for a phone declares a smaller one -- legitimately, for its
 * own furniture, which is all a frame offer can speak for. It does not replace
 * the lane's toplevel; it arranges over it. So the lane's floor still applies,
 * and this is where it is read: not from a constant (the mobile toplevel is
 * fluid and must stay able to lay out at phone widths) but from the lane's own
 * layout, at the one moment it states the number -- when the block it lays in
 * the carved area comes out WIDER than the carved area.
 *
 * 0 when everything fits, which is also the answer when there is no such
 * block. Only the overflow case is measurable: a block that fits tells us
 * nothing about how small it could have been, and needs nothing.
 */
int
UITree_MeasureLaneFrameCoreWidth(struct UITree const* tree)
{
#if defined(TORIRS_CANVAS_CAPTURE)
    canvas_chain_capture(tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    if( tree && UITree_CanvasQueryCompactEnabled() )
        return UITree_CanvasMeasureCompact(tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H)
            .core;
    /* Memoised on the same terms as the strip scan above, and for the
     * same reason: this is a full pass over every component, asked once per
     * frame from App_CanvasFloorWidth. @see UITree_MeasureRightChromeStripWidth. */
    static struct UITree const* memo_tree = NULL;
    static uint32_t memo_dirty_gen;
    static uint32_t memo_layout_seq;
    static uint32_t memo_count;
    static uint32_t memo_generation;
    static int memo_value;

    int canvas_w;
    int canvas_h;
    int strip;
    int best;
    uint32_t i;

    if( !tree || tree->component_count == 0 )
        return 0;

    if( memo_tree == tree && memo_dirty_gen == tree->dirty_gen &&
        memo_layout_seq == tree->layout_resolve_seq &&
        memo_count == tree->component_count && memo_generation == tree->generation )
        return memo_value;

    canvas_w = UITREE_LAYOUT_ROOT_W;
    canvas_h = UITREE_LAYOUT_ROOT_H;
    strip = UITree_MeasureRightChromeStripWidth(tree);
    best = 0;

    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        struct UITreeComponent const* p;

        /* A block with a size of its own -- not one that follows whatever it
         * is put in, which by construction can never overflow. */
        if( c->freed || c->position.width_mode != 0 || c->position.abs_w <= 0 )
            continue;
        if( c->parent < 0 || (uint32_t)c->parent >= tree->component_count )
            continue;
        p = &tree->components[c->parent];
        if( c->position.abs_w <= p->position.abs_w )
            continue;
        /*
         * The parent must be the CARVED AREA: full-canvas height, docked on
         * the left edge, and exactly the strip narrower than the canvas. That
         * signature is what separates the lane's frame from every other fixed
         * box that is bigger than the container it hangs in -- a 512x334 modal
         * parked inside the 42-column popout strip is the shape this would
         * otherwise measure, and it is not short of anything.
         */
        if( p->position.width_mode != 1 || p->position.height_mode != 1 )
            continue;
        if( p->position.abs_x != 0 || p->position.abs_w != canvas_w - strip )
            continue;
        if( p->position.abs_h < canvas_h - 2 )
            continue;
        if( c->position.abs_w <= best )
            continue;
        /* Ancestors too, for the reason the strip scan gives: a speculatively
         * baked panel keeps live-looking boxes under a hidden root. */
        if( UITree_ComponentHiddenOrOrphaned(tree, (int32_t)i) )
            continue;
        best = c->position.abs_w;
    }

    memo_tree = tree;
    memo_dirty_gen = tree->dirty_gen;
    memo_layout_seq = tree->layout_resolve_seq;
    memo_count = tree->component_count;
    memo_generation = tree->generation;
    memo_value = best;
    return best;
}
