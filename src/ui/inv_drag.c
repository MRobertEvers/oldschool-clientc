#include "ui/inv_drag.h"

#include <assert.h>
#include <string.h>

void
UIInvDrag_Reset(struct UIInvDrag* drag)
{
    assert(drag);
    memset(drag, 0, sizeof(*drag));
    drag->component_id = -1;
    drag->node_index = -1;
    drag->source_id = -1;
    drag->obj_id = -1;
}

void
UIInvDrag_Arm(struct UIInvDrag* drag, struct UIInvDragPress const* press, int mouse_x, int mouse_y)
{
    assert(drag);
    assert(press);

    drag->component_id = press->component_id;
    drag->node_index = press->node_index;
    drag->node_incarnation = press->node_incarnation;
    drag->can_drag = press->can_drag;
    drag->from_slot = press->slot;
    drag->source_id = press->inv_source_id;
    drag->obj_id = press->obj_id;
    drag->cycles = 0;
    drag->grab_x = mouse_x;
    drag->grab_y = mouse_y;
    drag->threshold = false;
    /* Normalised once, here, rather than re-defaulted on every frame of the
     * hold: a widget that declares no gate gets the reference's, and from then
     * on both gates are plain numbers. */
    drag->dead_zone = press->dead_zone ? press->dead_zone : UI_INV_DRAG_DEAD_ZONE_DEFAULT;
    drag->dead_time = press->dead_time ? press->dead_time : UI_INV_DRAG_DEAD_TIME_DEFAULT;
    drag->dx = 0;
    drag->dy = 0;
}

bool
UIInvDrag_Promoted(struct UIInvDrag const* drag)
{
    assert(drag);
    /* No can_drag term: a fixed cell never gets past the guard in Hold, so it
     * arrives here with neither gate touched. Testing it twice would leave one
     * of the two tests unreachable, and an unreachable rule is one nothing can
     * tell you has stopped being true. */
    return drag->threshold && drag->cycles >= drag->dead_time;
}

bool
UIInvDrag_Ghosting(struct UIInvDrag const* drag, enum UIInvDragLane lane)
{
    assert(drag);
    if( !UIInvDrag_Armed(drag) )
        return false;
    /* On the per-cell lane a cell that cannot be dragged never fades: the
     * press is only ever going to be a click on it, and fading it would
     * promise a drag the release is not going to deliver. The uniform lane has
     * no such flag to consult, so it fades whatever it armed. */
    return lane == UI_INV_DRAG_LANE_UNIFORM ? true : drag->can_drag;
}

bool
UIInvDrag_Hold(struct UIInvDrag* drag, int mouse_x, int mouse_y)
{
    int dx;
    int dy;
    bool was_promoted;

    assert(drag);
    assert(UIInvDrag_Armed(drag));

    /* Non-draggable (IF_SETEVENTS drag depth 0, e.g. a worn slot): the press
     * still counts as a click on release, but no amount of holding or moving
     * may turn it into a drag. This is the ONLY gate on that -- neither counter
     * below advances, so the promotion test never sees a reason to say yes.
     * It also keeps a ghosted fixed cell still: on the uniform lane such a
     * cell does fade, and an offset accumulating underneath would slide it
     * around the screen after a drag the release will refuse to perform. */
    if( !drag->can_drag )
        return false;

    dx = mouse_x - drag->grab_x;
    dy = mouse_y - drag->grab_y;

    was_promoted = UIInvDrag_Promoted(drag);
    drag->cycles++;
    /* Latched: leaving the zone once is what the gate asks. A pointer that
     * drifts back through the origin mid-drag is still dragging. */
    if( dx > drag->dead_zone || dx < -drag->dead_zone || dy > drag->dead_zone ||
        dy < -drag->dead_zone )
        drag->threshold = true;

    /* The visible offset is gated independently of the promotion, and per
     * axis: a gesture that has travelled sideways but not vertically shows its
     * sideways travel only, so the item does not appear to wander off the axis
     * the hand is actually moving along. */
    if( dx < drag->dead_zone && dx > -drag->dead_zone )
        dx = 0;
    if( dy < drag->dead_zone && dy > -drag->dead_zone )
        dy = 0;
    if( drag->cycles < drag->dead_time )
    {
        dx = 0;
        dy = 0;
    }

    if( dx != drag->dx || dy != drag->dy || (!was_promoted && UIInvDrag_Promoted(drag)) )
    {
        drag->dx = dx;
        drag->dy = dy;
        return true;
    }
    return false;
}

void
UIInvDrag_AddressingKey(struct UIInvDrag const* drag, int out_key[4])
{
    assert(drag);
    assert(out_key);
    out_key[0] = drag->component_id;
    out_key[1] = drag->from_slot;
    out_key[2] = drag->source_id;
    out_key[3] = drag->obj_id;
}
