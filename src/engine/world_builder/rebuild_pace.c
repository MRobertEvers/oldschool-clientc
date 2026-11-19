#include "engine/world_builder/rebuild_pace.h"

#include <assert.h>
#include <string.h>

void
RebuildPace_Reset(struct RebuildPace* pace)
{
    assert(pace);
    memset(pace, 0, sizeof(*pace));
}

void
RebuildPace_Configure(
    struct RebuildPace* pace,
    uint64_t budget_us,
    uint64_t window_us)
{
    assert(pace);
    pace->budget_us = budget_us;
    pace->window_us = window_us;
}

int
RebuildPace_Pacing(struct RebuildPace const* pace)
{
    assert(pace);
    return pace->until_us != 0;
}

void
RebuildPace_Arm(
    struct RebuildPace* pace,
    uint64_t now_us,
    uint64_t frame)
{
    assert(pace);
    if( pace->budget_us == 0 )
        return;
    pace->until_us = now_us + pace->window_us;
    pace->hard_until_us = now_us + REBUILD_PACE_HARD_LIMIT_US;
    if( pace->hard_until_us < pace->until_us )
        pace->hard_until_us = pace->until_us;
    pace->commit_frame = frame;
    pace->pop_frame = frame;
    /* The commit frame has had its work: the scripts the commit itself
     * queued get a short slice, and the first packet waits for the next
     * frame (RebuildPace_HoldsPop). */
    pace->deadline_frame = frame;
    pace->deadline_us = now_us + REBUILD_PACE_COMMIT_FRAME_US;
    pace->stat_pop_stops = 0;
    pace->stat_settle_stops = 0;
}

uint64_t
RebuildPace_Deadline(
    struct RebuildPace* pace,
    uint64_t now_us,
    uint64_t frame,
    int* out_ended)
{
    assert(pace);
    assert(out_ended);
    *out_ended = 0;
    if( pace->until_us == 0 )
        return 0;
    if( now_us >= pace->until_us )
    {
        /* The window is the bound: whatever is still held applies at once
         * from here. */
        *out_ended = 1;
        pace->until_us = 0;
        return 0;
    }
    if( pace->deadline_frame != frame )
    {
        pace->deadline_frame = frame;
        pace->deadline_us = now_us + pace->budget_us;
    }
    return pace->deadline_us;
}

void
RebuildPace_NoteStop(
    struct RebuildPace* pace,
    uint64_t now_us)
{
    uint64_t until;

    assert(pace);
    if( pace->until_us == 0 )
        return;
    until = now_us + REBUILD_PACE_STOP_LINGER_US;
    if( until > pace->hard_until_us )
        until = pace->hard_until_us;
    if( until > pace->until_us )
        pace->until_us = until;
}

int
RebuildPace_HoldsPop(
    struct RebuildPace* pace,
    uint64_t deadline_us,
    uint64_t now_us,
    uint64_t frame)
{
    assert(pace);
    assert(deadline_us != 0);
    if( pace->commit_frame != frame )
    {
        if( now_us < deadline_us || pace->pop_frame != frame )
        {
            pace->pop_frame = frame;
            return 0;
        }
    }
    pace->stat_pop_stops++;
    return 1;
}

void
RebuildPace_NoteSettleStop(struct RebuildPace* pace)
{
    assert(pace);
    pace->stat_settle_stops++;
}
