#ifndef GAME_RS_DESIGN_PANEL_H
#define GAME_RS_DESIGN_PANEL_H

/*
 * The rev-239 character-design panel's arrows, answered CLIENT-SIDE.
 *
 * ------------------------------------------------------------------
 * What this is for
 * ------------------------------------------------------------------
 *
 * Interface 679 (`OSRS-Content/.../interfaces/player_design.if`) is
 * server-driven end to end: every arrow is an ordinary component with `op1=*`,
 * a click is an IF_BUTTON like any other, and the state behind it is the
 * server's — see the header of `player/scripts/player_design.rs2`, which
 * argues at length for keeping it that way.
 *
 * That is still where the design LIVES. What it costs is that the figure
 * cannot move until the server's next tick: measured on the embedded lane, an
 * arrow click is answered in 63 ms when it lands just before a tick boundary
 * and 615 ms when it lands just after, with nothing slow anywhere in between.
 * A 600 ms wait on a repeated, exploratory click is the panel's whole feel.
 *
 * So the client steps the SAME arrow itself, immediately, and lets the
 * server's appearance land on top a tick later. The prediction is not a second
 * authority:
 *
 *   - it is derived from the same facts. The kit lists come from the cache's
 *     own idk table filtered exactly as `tools/gen_player_design.py` filters
 *     it (bodypart + notselectable), which is where the server's `design_kit`
 *     enum came from; the colour palettes are `k_recol1d`, which the CLIENT
 *     already owns and the server keeps a checked copy of
 *     (`tools/check_design_colours.py`);
 *   - it is self-correcting. PLAYER_INFO overwrites the appearance wholesale
 *     within one tick, so a prediction that is wrong is wrong for one tick and
 *     then is not. Nothing is saved from it and nothing is sent from it — the
 *     IF_BUTTON the server acts on is the same packet it always was.
 *
 * ------------------------------------------------------------------
 * Why stepping the KIT is the same walk as stepping the INDEX
 * ------------------------------------------------------------------
 *
 * The server holds each row as an INDEX into that body part's list
 * (`%design_kit_*`), because an index is what survives a gender flip. The
 * client cannot read those varps (`transmit=no`) and does not need to: it
 * knows which kit the row is currently ON, because that is what it is drawing.
 * Walking kit ids upward and skipping the ones that are not this part's is the
 * same sequence as advancing an index through the filtered list — same order,
 * same wrap. @see RS_IdkDesign_StepKitId, which is the dat1 design screen's
 * own cycling rule, shared rather than re-derived.
 *
 * A row standing on a kit that is not in the list (nothing on this panel puts
 * it there) cannot be located, and then this module declines to predict and
 * the arrow simply costs a tick again.
 */

struct CacheProvider;

enum RS_DesignArrowKind
{
    /* `child` is not one of the panel's arrows. */
    RS_DESIGN_ARROW_NONE = 0,
    /* A body-part row on the left: `row` is the design part, 0..6. */
    RS_DESIGN_ARROW_KIT,
    /* A palette row on the right: `row` is the colour slot, 0..4. */
    RS_DESIGN_ARROW_COLOUR
};

struct RS_DesignArrow
{
    enum RS_DesignArrowKind kind;
    int row;
    /** -1 for the left arrow, +1 for the right one. */
    int step;
};

/**
 * Which arrow (if any) child `child` of the design interface is.
 *
 * Returns nonzero and fills `out` for an arrow, zero for every other child of
 * the panel — its frame, its labels, the gender buttons, Confirm. Those keep
 * the server round trip, which for a once-per-session button is nothing to
 * pay and one fewer rule to keep in step.
 */
int
RS_DesignPanel_Arrow(
    int child,
    struct RS_DesignArrow* out);

/**
 * Apply `arrow` to a PLAYER_INFO appearance in place, exactly as the server's
 * `~design_kit_step` / `~design_colour_step` will.
 *
 * `slots` is the drawn layer and `identkit` the body underneath (the two
 * arrays rev 239's appearance block carries). A kit lands in both, except in a
 * wear position covered by a worn obj — there only `identkit` moves, because
 * that is what the server's own encoder does with it.
 *
 * Returns nonzero when something changed, zero when the arrow could not be
 * resolved (no kit to step from, an empty palette) and the caller should just
 * wait for the server.
 */
int
RS_DesignPanel_Apply(
    struct CacheProvider* provider,
    struct RS_DesignArrow const* arrow,
    int gender,
    int slots[12],
    int identkit[12],
    int colors[5]);

#endif
