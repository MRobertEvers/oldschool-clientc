#include "perf/boot_pump_stats.h"

#include <assert.h>
#include <string.h>

void
BootPumpStats_Reset(struct BootPumpStats* stats)
{
    assert(stats);
    memset(stats, 0, sizeof(*stats));
}

void
BootPumpStats_NoteBootSteps(
    struct BootPumpStats* stats,
    long steps)
{
    assert(stats);
    assert(steps >= 0);
    stats->boot_steps += steps;
}

void
BootPumpStats_PassBegin(struct BootPumpStats* stats)
{
    assert(stats);
    stats->async_pending = 0;
    stats->async_runnable = 0;
    stats->async_moved = 0;
}

void
BootPumpStats_NoteFrame(
    struct BootPumpStats* stats,
    bool from_frame,
    bool booting,
    bool settling_cs2,
    int steps,
    int budget)
{
    assert(stats);
    if( !from_frame )
    {
        /* A pump is not a frame: none of the per-frame counts below. */
    }
    else if( booting )
    {
        stats->boot_frames++;
        if( steps >= budget )
            stats->boot_frames_budget_capped++;
    }
    else if( !settling_cs2 && steps >= budget )
    {
        stats->busy_frames++;
        stats->busy_steps += steps;
    }
}

void
BootPumpStats_SetPending(struct BootPumpStats* stats)
{
    assert(stats);
    stats->async_pending = 1;
}

void
BootPumpStats_SetRunnable(struct BootPumpStats* stats)
{
    assert(stats);
    stats->async_pending = 1;
    stats->async_runnable = 1;
}

void
BootPumpStats_SetMoved(struct BootPumpStats* stats)
{
    assert(stats);
    stats->async_moved = 1;
}

int
BootPumpStats_Pending(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->async_pending;
}

int
BootPumpStats_Runnable(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->async_runnable;
}

int
BootPumpStats_Moved(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->async_moved;
}

int
BootPumpStats_BootFrames(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->boot_frames;
}

long
BootPumpStats_BootSteps(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->boot_steps;
}

int
BootPumpStats_BootFramesCapped(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->boot_frames_budget_capped;
}

int
BootPumpStats_BusyFrames(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->busy_frames;
}

long
BootPumpStats_BusySteps(struct BootPumpStats const* stats)
{
    assert(stats);
    return stats->busy_steps;
}
