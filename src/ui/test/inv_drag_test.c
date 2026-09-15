/*
 * When a press on an inventory slot becomes a drag.
 *
 * Every assertion here is about a gesture that was misread, and a misread
 * gesture has no visible tell: the inventory simply does something other than
 * what the hand asked for. A click that promotes swaps two items; a drag that
 * never promotes eats the item instead of moving it; a ghost that waits for the
 * promotion makes the client feel late.
 */

#include "ui/inv_drag.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** A backpack cell: draggable, and declaring no gates of its own. */
static struct UIInvDragPress
backpack_cell(void)
{
    struct UIInvDragPress press;
    memset(&press, 0, sizeof(press));
    press.component_id = 149;
    press.node_index = 12;
    press.node_incarnation = 7;
    press.can_drag = true;
    press.slot = 3;
    press.inv_source_id = 93;
    press.obj_id = 1042;
    press.dead_zone = 0;
    press.dead_time = 0;
    return press;
}

/** Hold the button down for `frames` cycles without moving the pointer. */
static void
hold_still(struct UIInvDrag* drag, int frames)
{
    int i;
    for( i = 0; i < frames; i++ )
        UIInvDrag_Hold(drag, drag->grab_x, drag->grab_y);
}

static void
test_reset_is_disarmed(void)
{
    struct UIInvDrag drag;

    /* Deliberately dirty: reset has to be a state, not a set of tweaks. */
    memset(&drag, 0x5a, sizeof(drag));
    UIInvDrag_Reset(&drag);

    TEST_ASSERT(!UIInvDrag_Armed(&drag), "a reset gesture owns no cell");
    TEST_ASSERT(drag.component_id == -1, "a disarmed gesture names no component");
    TEST_ASSERT(drag.node_index == -1, "a disarmed gesture names no node");
    TEST_ASSERT(drag.source_id == -1, "a disarmed gesture names no container");
    TEST_ASSERT(drag.obj_id == -1, "a disarmed gesture names no item");
    TEST_ASSERT(drag.dx == 0 && drag.dy == 0, "a disarmed gesture has no offset");
    TEST_ASSERT(!drag.threshold, "a disarmed gesture has not left any dead zone");
    TEST_ASSERT(drag.cycles == 0, "a disarmed gesture has been held for no frames");
}

static void
test_arm_records_the_cell(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 400, 300);

    TEST_ASSERT(UIInvDrag_Armed(&drag), "a press on a cell arms the gesture");
    TEST_ASSERT(drag.component_id == 149, "the gesture owns the pressed component");
    TEST_ASSERT(drag.from_slot == 3, "the gesture owns the pressed slot");
    TEST_ASSERT(drag.source_id == 93, "the gesture owns the pressed container");
    TEST_ASSERT(drag.obj_id == 1042, "the gesture owns the item that was there");
    /* The incarnation is the whole point of recording the node: a component id
     * survives a rebuild into a recycled slot and would hand the gesture to
     * whatever landed there. */
    TEST_ASSERT(drag.node_index == 12, "the gesture fences on the exact node");
    TEST_ASSERT(drag.node_incarnation == 7, "the gesture fences on that node's incarnation");
    TEST_ASSERT(drag.grab_x == 400 && drag.grab_y == 300, "the press position is the origin");
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "a press is not yet a drag");
}

static void
test_widget_gates_default_when_unspecified(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 0, 0);
    TEST_ASSERT(drag.dead_zone == UI_INV_DRAG_DEAD_ZONE_DEFAULT, "an unstated zone gets the default");
    TEST_ASSERT(drag.dead_time == UI_INV_DRAG_DEAD_TIME_DEFAULT, "an unstated time gets the default");

    press.dead_zone = 20;
    press.dead_time = 2;
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 0, 0);
    TEST_ASSERT(drag.dead_zone == 20, "a widget's own zone is taken");
    TEST_ASSERT(drag.dead_time == 2, "a widget's own time is taken");
}

static void
test_a_press_replaces_whatever_was_held(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    /* A gesture mid-drag, then a press on a different cell without a reset in
     * between. The caller normally cancels first, but the press is what says
     * "this is the gesture now": carrying the old gesture's progress into it
     * would promote the new press on its first frame, dropping an item the
     * user has only just touched. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_Hold(&drag, 400, 400);
    hold_still(&drag, 9);
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "the first gesture is a drag");

    press.slot = 7;
    press.obj_id = 555;
    UIInvDrag_Arm(&drag, &press, 200, 200);
    TEST_ASSERT(drag.cycles == 0, "the new press has been held for no frames");
    TEST_ASSERT(!drag.threshold, "the new press has left no dead zone");
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "and so it is not a drag yet");
}

static void
test_both_gates_are_required(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    /* Held long enough, never moved: a deliberate long-press on an item, which
     * the reference still delivers as a click. Promoting it here is how a bank
     * gets rearranged by someone who was reading a tooltip. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    hold_still(&drag, 30);
    TEST_ASSERT(drag.cycles == 30, "a held frame is a counted frame");
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "time alone does not make a drag");

    /* Moved far, released almost immediately: a fast flick across the grid.
     * Promoting it swaps two slots before the click could be recognised. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_Hold(&drag, 300, 100);
    TEST_ASSERT(drag.threshold, "leaving the zone is recorded on the frame it happens");
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "travel alone does not make a drag");

    /* Both: an actual drag. */
    UIInvDrag_Hold(&drag, 300, 100);
    UIInvDrag_Hold(&drag, 300, 100);
    UIInvDrag_Hold(&drag, 300, 100);
    UIInvDrag_Hold(&drag, 300, 100);
    TEST_ASSERT(drag.cycles == UI_INV_DRAG_DEAD_TIME_DEFAULT, "five held frames");
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "past both gates is a drag");
}

static void
test_the_time_gate_is_a_floor_not_an_equality(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    /* A slow drag is held for far longer than the gate. If the gate were an
     * equality it would promote for one frame in the middle of the gesture and
     * then demote, and the release would run the default menu row instead of
     * the drop -- "it only works if I let go quickly". */
    hold_still(&drag, 4);
    UIInvDrag_Hold(&drag, 300, 100);
    {
        int i;
        for( i = 0; i < 60; i++ )
            UIInvDrag_Hold(&drag, 300, 100);
    }
    TEST_ASSERT(drag.cycles == 65, "a slow drag is held far past the gate");
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "a long drag is still a drag");
}

static void
test_the_zone_gate_latches(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    hold_still(&drag, 5);
    UIInvDrag_Hold(&drag, 300, 100);
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "the gesture promoted on the way out");
    /* Dragging an item back over the slot it came from is how it is returned.
     * Un-latching here would turn the release into a click on that slot, which
     * eats or wields the item instead of putting it down. */
    UIInvDrag_Hold(&drag, 100, 100);
    TEST_ASSERT(drag.threshold, "coming back inside the zone does not un-promote");
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "a drag returned to its origin is still a drag");
}

static void
test_the_zone_is_measured_per_axis_and_both_ways(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();
    int axis;

    /* Four directions, each exactly one past the zone. A comparison written
     * against one axis only, or against the positive direction only, passes a
     * single-direction test and drops drags going the other way. */
    for( axis = 0; axis < 4; axis++ )
    {
        int const zone = UI_INV_DRAG_DEAD_ZONE_DEFAULT;
        int const dx = axis == 0 ? zone + 1 : (axis == 1 ? -zone - 1 : 0);
        int const dy = axis == 2 ? zone + 1 : (axis == 3 ? -zone - 1 : 0);
        UIInvDrag_Reset(&drag);
        UIInvDrag_Arm(&drag, &press, 100, 100);
        UIInvDrag_Hold(&drag, 100 + dx, 100 + dy);
        TEST_ASSERT(drag.threshold, "one pixel past the zone leaves it, on any axis");
    }

    /* Exactly ON the zone boundary is still inside it. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_Hold(&drag, 100 + UI_INV_DRAG_DEAD_ZONE_DEFAULT, 100);
    TEST_ASSERT(!drag.threshold, "the zone boundary belongs to the zone");
}

static void
test_the_offset_is_gated_per_axis(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    hold_still(&drag, 5);

    /* Far sideways, barely vertical: the icon follows the hand along the axis
     * it is actually travelling and stays put on the other, instead of
     * wandering off at an angle nobody asked for. */
    UIInvDrag_Hold(&drag, 140, 102);
    TEST_ASSERT(drag.dx == 40, "the travelled axis shows its travel");
    TEST_ASSERT(drag.dy == 0, "the axis still inside the zone shows nothing");

    /* And the other way round. One gate written for both axes passes the case
     * above and leaks the small offset on whichever axis it forgot. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    hold_still(&drag, 5);
    UIInvDrag_Hold(&drag, 102, 140);
    TEST_ASSERT(drag.dy == 40, "the travelled axis shows its travel");
    TEST_ASSERT(drag.dx == 0, "the axis still inside the zone shows nothing");
}

static void
test_the_offset_waits_for_the_time_gate(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    /* Moved far on the very first frame. The icon must not jump: until the
     * dead time is served the gesture may still be a click, and a click whose
     * icon leapt across the inventory reads as a glitch. */
    UIInvDrag_Hold(&drag, 300, 400);
    TEST_ASSERT(drag.threshold, "the pointer has left the zone");
    TEST_ASSERT(drag.dx == 0 && drag.dy == 0, "the icon does not move before the time gate");
    UIInvDrag_Hold(&drag, 300, 400);
    UIInvDrag_Hold(&drag, 300, 400);
    UIInvDrag_Hold(&drag, 300, 400);
    TEST_ASSERT(drag.dx == 0 && drag.dy == 0, "still pinned one frame short of the gate");
    UIInvDrag_Hold(&drag, 300, 400);
    TEST_ASSERT(drag.dx == 200 && drag.dy == 300, "once served, the icon is at the pointer");
}

static void
test_hold_reports_only_visible_change(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    /* A still hand holds the button down for many frames. Repainting each one
     * is a full UI walk per frame for no change on screen. */
    TEST_ASSERT(!UIInvDrag_Hold(&drag, 100, 100), "a motionless frame is not a repaint");

    hold_still(&drag, 3);
    /* The frame the offset first appears is a repaint even though the pointer
     * did not move on it: serving the time gate un-zeroes the offset. */
    TEST_ASSERT(UIInvDrag_Hold(&drag, 120, 100), "the frame the icon first moves repaints");
    TEST_ASSERT(!UIInvDrag_Hold(&drag, 120, 100), "the same offset again does not repaint");

    /* Promotion alone repaints: the gesture crosses into being a drag on a
     * frame where the pointer has not moved, and the cursor's meaning changes
     * with it. */
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_Hold(&drag, 100 + UI_INV_DRAG_DEAD_ZONE_DEFAULT + 1, 100);
    hold_still(&drag, 3);
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "four frames is one short of the gate");
    TEST_ASSERT(UIInvDrag_Hold(&drag, drag.grab_x, drag.grab_y), "the promoting frame repaints");
    TEST_ASSERT(UIInvDrag_Promoted(&drag), "and it is the frame that promoted");
}

static void
test_a_cell_that_cannot_be_dragged_never_promotes(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    /* A worn-equipment slot: IF_SETEVENTS drag depth 0. The press is still a
     * click on release -- that is how equipment is removed -- but no amount of
     * holding or moving may turn it into a drag. */
    press.can_drag = false;
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_Hold(&drag, 400, 400);
    hold_still(&drag, 30);
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "a non-draggable cell never promotes");
    TEST_ASSERT(drag.dx == 0 && drag.dy == 0, "and never acquires an offset");
    TEST_ASSERT(drag.cycles == 0, "a gesture that cannot promote counts no frames");
    TEST_ASSERT(UIInvDrag_Armed(&drag), "but it still owns the cell until release");
}

static void
test_ghosting_starts_at_the_press(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    UIInvDrag_Reset(&drag);
    TEST_ASSERT(
        !UIInvDrag_Ghosting(&drag, UI_INV_DRAG_LANE_PER_CELL),
        "nothing fades while no cell is held");
    /* The uniform lane has no per-cell flag standing between it and a yes, so
     * it is the lane where a missing "is anything held?" test shows: every
     * inventory icon fades, permanently, with nobody touching the mouse. */
    TEST_ASSERT(
        !UIInvDrag_Ghosting(&drag, UI_INV_DRAG_LANE_UNIFORM),
        "nor on the lane that fades whatever it armed");

    UIInvDrag_Arm(&drag, &press, 100, 100);
    /* The fade is the answer to "am I holding this?", and it has to arrive
     * while the user is still deciding what the gesture is. Tied to the
     * promotion instead, the item only dims once the drag is committed, which
     * is what "items don't dim when the left button is down" was. */
    TEST_ASSERT(!UIInvDrag_Promoted(&drag), "the gesture has not promoted");
    TEST_ASSERT(
        UIInvDrag_Ghosting(&drag, UI_INV_DRAG_LANE_PER_CELL),
        "the cell fades from the press anyway");
}

static void
test_the_lane_decides_whether_a_fixed_cell_fades(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();

    press.can_drag = false;
    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);

    /* On the per-cell lane a fixed cell shows no feedback: fading it would
     * promise a drag the release is not going to deliver. */
    TEST_ASSERT(
        !UIInvDrag_Ghosting(&drag, UI_INV_DRAG_LANE_PER_CELL),
        "a fixed cell does not fade where the lane knows it is fixed");
    /* The uniform lane has no per-cell flag to consult at all, so consulting
     * one there means reading whatever the cell resolver happened to leave --
     * every cell would fade, or none would, by accident rather than by rule. */
    TEST_ASSERT(
        UIInvDrag_Ghosting(&drag, UI_INV_DRAG_LANE_UNIFORM),
        "the uniform lane fades whatever it armed");
}

static void
test_addressing_key_changes_only_with_the_gesture(void)
{
    struct UIInvDrag drag;
    struct UIInvDragPress press = backpack_cell();
    int armed[4];
    int moved[4];
    int disarmed[4];
    int other[4];

    UIInvDrag_Reset(&drag);
    UIInvDrag_Arm(&drag, &press, 100, 100);
    UIInvDrag_AddressingKey(&drag, armed);

    /* Carrying the item across the screen is not a change of identity. A key
     * that moved with the pointer would re-walk the whole inventory on every
     * frame of every drag. */
    hold_still(&drag, 5);
    UIInvDrag_Hold(&drag, 400, 400);
    UIInvDrag_AddressingKey(&drag, moved);
    TEST_ASSERT(memcmp(armed, moved, sizeof(armed)) == 0, "carrying the item is not a change");

    UIInvDrag_Reset(&drag);
    UIInvDrag_AddressingKey(&drag, disarmed);
    TEST_ASSERT(memcmp(armed, disarmed, sizeof(armed)) != 0, "letting go is a change");

    /* One field at a time. Emit addresses the ghosted cell by all four, and a
     * key that drops any of them leaves a stale ghost on the cell the last
     * gesture used -- which is a translucent item sitting in a slot nobody is
     * holding. Changed together, each of these is caught by the others. */
    {
        struct UIInvDragPress const base = backpack_cell();
        struct UIInvDragPress varied;
        int i;
        for( i = 0; i < 4; i++ )
        {
            varied = base;
            switch( i )
            {
            case 0: varied.component_id = base.component_id + 1; break;
            case 1: varied.slot = base.slot + 1; break;
            case 2: varied.inv_source_id = base.inv_source_id + 1; break;
            default: varied.obj_id = base.obj_id + 1; break;
            }
            UIInvDrag_Reset(&drag);
            UIInvDrag_Arm(&drag, &base, 100, 100);
            UIInvDrag_AddressingKey(&drag, armed);
            UIInvDrag_Reset(&drag);
            UIInvDrag_Arm(&drag, &varied, 100, 100);
            UIInvDrag_AddressingKey(&drag, other);
            TEST_ASSERT(
                memcmp(armed, other, sizeof(armed)) != 0,
                "each part of the address is part of the key");
        }
    }
}

int
main(void)
{
    test_reset_is_disarmed();
    test_arm_records_the_cell();
    test_widget_gates_default_when_unspecified();
    test_a_press_replaces_whatever_was_held();
    test_both_gates_are_required();
    test_the_time_gate_is_a_floor_not_an_equality();
    test_the_zone_gate_latches();
    test_the_zone_is_measured_per_axis_and_both_ways();
    test_the_offset_is_gated_per_axis();
    test_the_offset_waits_for_the_time_gate();
    test_hold_reports_only_visible_change();
    test_a_cell_that_cannot_be_dragged_never_promotes();
    test_ghosting_starts_at_the_press();
    test_the_lane_decides_whether_a_fixed_cell_fades();
    test_addressing_key_changes_only_with_the_gesture();

    if( g_failures )
    {
        printf("inv_drag: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("inv_drag: all checks passed\n");
    return 0;
}
