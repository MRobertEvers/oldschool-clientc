#include "rs_design_panel.h"

#include "engine/entity_model_build.h"
#include "game/rs_idk_design.h"
/* The appearance slot vocabulary — what a slot int means, and how a kit is
 * tagged into one. Header-only. */
#include "net/rev/packets/pkt_player_appearance.h"

#include <assert.h>

/*
 * The panel's own layout, from `interfaces/player_design.compack`.
 *
 * Twelve rows, each four components in the same order — the label's backing,
 * the label, the left arrow, the right arrow — so the arrows are a stride and
 * not a table:
 *
 *   13..40  the seven body-part rows, `head` first: left = 15 + 4*part
 *   44..63  the five colour rows, `hair` first:     left = 46 + 4*slot
 *
 * The rows' ORDER is the panel's, not the appearance's: body parts run hair,
 * jaw, torso, arms, hands, legs, feet (the idk table's own bodypart numbering,
 * which is also `^design_part_*`), and colours run hair, torso, legs, feet,
 * skin (the order the appearance block carries them, which is also what
 * `setidkcolour` indexes). Both match what this client already calls a design
 * part and a design colour, so neither needs a remap here.
 *
 * `tools/check_player_design_panel.py` reads the compack and fails on a
 * mismatch: the panel is content, these are ids, and an arrow that silently
 * stepped the wrong row would look like a prediction bug rather than a moved
 * component.
 */
enum
{
    DESIGN_KIT_ROW_FIRST = 15,
    DESIGN_KIT_ROW_COUNT = 7,
    DESIGN_COLOUR_ROW_FIRST = 46,
    DESIGN_COLOUR_ROW_COUNT = 5,
    DESIGN_ROW_STRIDE = 4
};

/* Both halves of a row: `first + stride*n` is the left arrow and the next
 * component is the right one. Returns 0 when `child` is neither. */
static int
design_row_arrow(
    int child,
    int first,
    int count,
    int* out_row,
    int* out_step)
{
    int offset = child - first;
    int within;

    if( offset < 0 || offset >= count * DESIGN_ROW_STRIDE )
        return 0;
    within = offset % DESIGN_ROW_STRIDE;
    if( within > 1 )
        return 0;
    *out_row = offset / DESIGN_ROW_STRIDE;
    *out_step = within == 0 ? -1 : 1;
    return 1;
}

int
RS_DesignPanel_Arrow(
    int child,
    struct RS_DesignArrow* out)
{
    assert(out);

    out->kind = RS_DESIGN_ARROW_NONE;
    out->row = -1;
    out->step = 0;

    if( design_row_arrow(
            child, DESIGN_KIT_ROW_FIRST, DESIGN_KIT_ROW_COUNT, &out->row, &out->step) )
    {
        out->kind = RS_DESIGN_ARROW_KIT;
        return 1;
    }
    if( design_row_arrow(
            child, DESIGN_COLOUR_ROW_FIRST, DESIGN_COLOUR_ROW_COUNT, &out->row, &out->step) )
    {
        out->kind = RS_DESIGN_ARROW_COLOUR;
        return 1;
    }
    out->row = -1;
    out->step = 0;
    return 0;
}

/* `~design_step`: wrap an index within [0, count). */
static int
design_step_index(
    int index,
    int step,
    int count)
{
    int next;

    if( count < 1 )
        return 0;
    next = index + step;
    if( next < 0 )
        return count - 1;
    if( next >= count )
        return 0;
    return next;
}

static int
design_apply_kit(
    struct CacheProvider* provider,
    int part,
    int step,
    int gender,
    int slots[12],
    int identkit[12])
{
    /* The female half of the idk table is the same seven parts at +7, which is
     * `~design_part` on the other side. */
    int const body_part = part + (gender == 1 ? RS_IDK_DESIGN_PARTS : 0);
    int const wearpos = PlayerModel_DesignPartWearpos(part);
    int const current = Appearance_SlotKit(identkit[wearpos]);
    int next;

    assert(wearpos >= 0);
    if( current < 0 )
        return 0; /* no kit on this row to step from — let the server answer */

    next = RS_IdkDesign_StepKitId(
        provider, RS_IdkDesign_KitTableCount(provider), body_part, current, step);
    if( next < 0 || next == current )
        return 0;

    /* What the server's encoder does with the same write: the kit always lands
     * in the body-underneath array, and in the drawn one only where no worn obj
     * is covering it. @see appearance_identkit_slots / appearance_slots. */
    identkit[wearpos] = Appearance_PackKit(next);
    if( Appearance_SlotKind(slots[wearpos]) != APPEARANCE_SLOT_OBJ )
        slots[wearpos] = Appearance_PackKit(next);
    return 1;
}

static int
design_apply_colour(
    int slot,
    int step,
    int colors[5])
{
    /* Every one of the five palettes has entries; the caller has already
     * checked that the row is one of them. */
    int const count = PlayerModel_DesignColourCount(slot);
    int next;

    assert(count > 0);
    next = design_step_index(colors[slot], step, count);
    if( next == colors[slot] )
        return 0; /* a one-entry palette: the arrow has nowhere to go */
    colors[slot] = next;
    return 1;
}

int
RS_DesignPanel_Apply(
    struct CacheProvider* provider,
    struct RS_DesignArrow const* arrow,
    int gender,
    int slots[12],
    int identkit[12],
    int colors[5])
{
    assert(provider);
    assert(arrow);
    assert(slots);
    assert(identkit);
    assert(colors);
    assert(arrow->step == 1 || arrow->step == -1);

    if( arrow->kind == RS_DESIGN_ARROW_KIT )
    {
        assert(arrow->row >= 0 && arrow->row < DESIGN_KIT_ROW_COUNT);
        return design_apply_kit(provider, arrow->row, arrow->step, gender, slots, identkit);
    }
    if( arrow->kind == RS_DESIGN_ARROW_COLOUR )
    {
        assert(arrow->row >= 0 && arrow->row < DESIGN_COLOUR_ROW_COUNT);
        return design_apply_colour(arrow->row, arrow->step, colors);
    }
    return 0;
}
