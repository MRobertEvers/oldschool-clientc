#include "world/loc_lane.h"

#include <assert.h>

uint32_t
LocLane_Take(struct LocLane* lane)
{
    assert(lane);
    return ++lane->enqueued;
}

uint32_t
LocLane_Newest(struct LocLane const* lane)
{
    assert(lane);
    return lane->enqueued;
}

uint32_t
LocLane_Applied(struct LocLane const* lane)
{
    assert(lane);
    return lane->applied;
}

bool
LocLane_IsTurn(
    struct LocLane const* lane,
    uint32_t ticket)
{
    assert(lane);
    return lane->applied + 1 == ticket;
}

bool
LocLane_Pending(
    struct LocLane const* lane,
    uint32_t ticket)
{
    assert(lane);
    return (int32_t)(lane->applied - ticket) < 0;
}

void
LocLane_Release(struct LocLane* lane)
{
    assert(lane);
    lane->applied++;
}
