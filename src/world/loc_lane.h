#ifndef SRC_WORLD_LOC_LANE_H
#define SRC_WORLD_LOC_LANE_H

/*
 * The loc lane's two counters: the order zone loc changes apply in.
 *
 * A loc change takes a ticket when it is queued, loads whatever it needs
 * alongside every other queued change, and applies only when the change
 * queued before it has, so a LOC_DEL lands after the add it deletes. Tickets
 * count from 1, so 0 means "no ticket" until the counter wraps. Each ticket is
 * released exactly once, on apply or when its task is freed unrun, and a
 * change's turn is the one after the last released.
 *
 * The counters are unsigned and wrap: whether a ticket has been released is
 * asked as a signed difference, so it holds across the wrap.
 *
 * This does not know what a loc change is, which task holds a ticket, when a
 * task runs or is freed, or which scene it applies to. The caller takes a
 * ticket when it queues a change, releases it exactly once, and asks whose
 * turn it is.
 */

#include <stdbool.h>
#include <stdint.h>

struct LocLane
{
    /** The newest ticket taken; 0 before the first. */
    uint32_t enqueued;
    /** How many tickets have been released. */
    uint32_t applied;
};

/** Take the next ticket for a change being queued. */
uint32_t
LocLane_Take(struct LocLane* lane);

/** The newest ticket taken: the change queued last. */
uint32_t
LocLane_Newest(struct LocLane const* lane);

/** How many tickets have been released. Moves only on a release, so a cache
 *  of what the lane has applied can be keyed on it. */
uint32_t
LocLane_Applied(struct LocLane const* lane);

/** Whether `ticket` is next to apply: every ticket before it is released. */
bool
LocLane_IsTurn(
    struct LocLane const* lane,
    uint32_t ticket);

/** Whether `ticket` has not been released yet. */
bool
LocLane_Pending(
    struct LocLane const* lane,
    uint32_t ticket);

/** Release one ticket: the change whose turn it was applied, was dropped, or
 *  was freed unrun. Once per ticket. */
void
LocLane_Release(struct LocLane* lane);

#endif /* SRC_WORLD_LOC_LANE_H */
