#ifndef SRC_UI_INV_DRAG_H
#define SRC_UI_INV_DRAG_H

/*
 * When a press on an inventory slot becomes a drag.
 *
 * A left press on a filled slot is ambiguous for as long as the finger is
 * down: it is going to end as a click on the slot's default menu row, or as a
 * drag that drops the item somewhere else, and nothing about the press itself
 * says which. The reference resolves it with two independent gates -- the
 * pointer has to leave a dead ZONE, and the button has to stay down for a dead
 * TIME -- and until both are passed the gesture is still a click.
 *
 * Both gates matter and they fail differently. Without the zone, a hand that
 * shakes one pixel on a trackpad turns every click into a drag, and items get
 * swapped by people who were trying to eat them. Without the time, a fast
 * flick across two slots swaps them before the click can be recognised, which
 * is how a bank gets rearranged by someone withdrawing from it. Neither is
 * visible in a screenshot and neither produces a log line: they present as
 * "the inventory did something I did not ask for".
 *
 * The ghost is deliberately NOT the promotion. The armed cell renders
 * translucent from the PRESS, before either gate is passed (Client.ts:8589,
 * :10207), because the feedback has to arrive while the user is still deciding
 * what the gesture is. Tying the ghost to the promotion instead would mean the
 * item only fades once the drag is already committed, which reads as lag.
 *
 * Nothing here knows about a widget tree, a container, or a packet. The caller
 * finds the cell under the pointer, decides whether the gesture may still own
 * it, and performs the drop; this decides what the gesture IS.
 */

#include <stdbool.h>
#include <stdint.h>

enum
{
    /** The reference's fallbacks when a widget declares neither (objGrab). */
    UI_INV_DRAG_DEAD_ZONE_DEFAULT = 5,
    UI_INV_DRAG_DEAD_TIME_DEFAULT = 5,
};

/**
 * The cell a press landed on, as the caller resolved it.
 *
 * `node_index` and `node_incarnation` are the caller's fence against the
 * gesture transferring to a different node: a component id survives a rebuild
 * into the same recycled slot, an incarnation does not.
 */
struct UIInvDragPress
{
    /** Component uid the server is told about. */
    int component_id;
    int32_t node_index;
    uint64_t node_incarnation;
    /** The cell's IF_SETEVENTS drag depth is non-zero. */
    bool can_drag;
    int slot;
    /** InvManager source id, or -1 for a CS2 cell that carries its own obj. */
    int inv_source_id;
    int obj_id;
    /** Widget's own gates; 0 means "unspecified", not "no gate". */
    uint8_t dead_zone;
    uint8_t dead_time;
};

struct UIInvDrag
{
    /** -1 when no press is being tracked. */
    int component_id;
    int32_t node_index;
    uint64_t node_incarnation;
    bool can_drag;
    int from_slot;
    int source_id;
    int obj_id;
    int cycles;
    /** Pointer position at the press (reference objGrabX/objGrabY). */
    int grab_x;
    int grab_y;
    /** The pointer has left the dead zone at least once since the press. */
    bool threshold;
    int dead_zone;
    int dead_time;
    /** Visible offset of the armed cell, already dead-zoned. */
    int dx;
    int dy;
};

/** Disarm. Also the initial state -- there is no separate constructor. */
void
UIInvDrag_Reset(struct UIInvDrag* drag);

static inline bool
UIInvDrag_Armed(struct UIInvDrag const* drag)
{
    return drag->component_id >= 0;
}

/**
 * Start tracking a press. The caller has already decided this press may arm:
 * that it landed on a cell, that no popup swallowed it, and that nothing else
 * has the pointer.
 */
void
UIInvDrag_Arm(struct UIInvDrag* drag, struct UIInvDragPress const* press, int mouse_x, int mouse_y);

/**
 * Has the press become a drag?
 *
 * Both gates. A cell that cannot be dragged answers no as well, because
 * neither gate is ever advanced for one -- see UIInvDrag_Hold.
 */
bool
UIInvDrag_Promoted(struct UIInvDrag const* drag);

/**
 * Which lane published the armed cell.
 *
 * The two disagree about where "may this be dragged" is written down, and the
 * ghost has to ask the lane that owns the answer.
 */
enum UIInvDragLane
{
    /** CS1: no per-cell drag depth exists; every armed cell shows feedback. */
    UI_INV_DRAG_LANE_UNIFORM,
    /** IF3/CS2: the cell's own IF_SETEVENTS drag depth decides. */
    UI_INV_DRAG_LANE_PER_CELL,
};

/**
 * Should the armed cell render translucent?
 *
 * From the press, not the promotion. See the note at the top of this file.
 */
bool
UIInvDrag_Ghosting(struct UIInvDrag const* drag, enum UIInvDragLane lane);

/**
 * Advance one frame with the button still down.
 *
 * Returns true when something the user can see changed -- the offset moved, or
 * this is the frame the press became a drag -- so the caller can repaint.
 */
bool
UIInvDrag_Hold(struct UIInvDrag* drag, int mouse_x, int mouse_y);

/**
 * The four numbers that say WHICH item this gesture owns.
 *
 * They are written together at the press and cleared together at the reset, so
 * a caller hashing them sees a change exactly when the gesture's identity
 * changes -- never on a frame where the gesture merely moved.
 */
void
UIInvDrag_AddressingKey(struct UIInvDrag const* drag, int out_key[4]);

#endif
