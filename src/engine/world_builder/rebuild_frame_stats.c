#include "engine/world_builder/rebuild_frame_stats.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(
    REBUILD_FRAME_STATS_AFTER_FRAMES <= REBUILD_FRAME_STATS_AFTER,
    "the frames after a window fit their series");

static uint16_t
rebuild_frame_stats_tenths(uint64_t frame_us)
{
    uint64_t const tenths = frame_us / 100u;
    return tenths > 65535u ? 65535u : (uint16_t)tenths;
}

void
RebuildFrameStats_Reset(struct RebuildFrameStats* stats)
{
    assert(stats);
    memset(stats, 0, sizeof(*stats));
}

void
RebuildFrameStats_Arm(struct RebuildFrameStats* stats)
{
    assert(stats);
    stats->armed = 1;
}

int
RebuildFrameStats_AfterPending(struct RebuildFrameStats const* stats)
{
    assert(stats);
    return stats->after_left > 0;
}

void
RebuildFrameStats_Open(
    struct RebuildFrameStats* stats,
    int background)
{
    struct RebuildFrameStats kept;

    assert(stats);
    /* The frame the window opens in keeps the marks it took before. */
    kept = *stats;
    memset(stats, 0, sizeof(*stats));
    stats->armed = kept.armed;
    if( kept.armed )
    {
        memcpy(stats->mark_name, kept.mark_name, sizeof(stats->mark_name));
        memcpy(stats->mark_us, kept.mark_us, sizeof(stats->mark_us));
        stats->marks = kept.marks;
    }
    stats->open = 1;
    stats->background = background;
}

void
RebuildFrameStats_Finished(struct RebuildFrameStats* stats)
{
    assert(stats);
    if( stats->open )
        stats->closing = 1;
}

void
RebuildFrameStats_Drop(struct RebuildFrameStats* stats)
{
    assert(stats);
    stats->open = 0;
}

void
RebuildFrameStats_ClearMarks(struct RebuildFrameStats* stats)
{
    assert(stats);
    stats->marks = 0;
}

int
RebuildFrameStats_WantsMark(struct RebuildFrameStats const* stats)
{
    assert(stats);
    /* Kept through the frames after the window too. */
    if( !stats->armed && !stats->open && stats->after_left == 0 )
        return 0;
    return stats->marks < REBUILD_FRAME_STATS_MARKS;
}

void
RebuildFrameStats_Mark(
    struct RebuildFrameStats* stats,
    char const* name,
    uint64_t now_us)
{
    assert(stats);
    assert(name);
    assert(RebuildFrameStats_WantsMark(stats));
    stats->mark_name[stats->marks] = name;
    stats->mark_us[stats->marks] = now_us;
    stats->marks++;
}

void
RebuildFrameStats_SetCommit(
    struct RebuildFrameStats* stats,
    double build_ms,
    double swap_ms,
    double shift_ms)
{
    assert(stats);
    stats->build_ms = build_ms;
    stats->swap_ms = swap_ms;
    stats->shift_ms = shift_ms;
}

void
RebuildFrameStats_SetTail(
    struct RebuildFrameStats* stats,
    double tail_ms,
    double finish_ms)
{
    assert(stats);
    stats->tail_ms = tail_ms;
    stats->finish_ms = finish_ms;
}

void
RebuildFrameStats_SetHeld(
    struct RebuildFrameStats* stats,
    double held_ms)
{
    assert(stats);
    stats->held_ms = held_ms;
}

int
RebuildFrameStats_Note(
    struct RebuildFrameStats* stats,
    uint64_t frame_us)
{
    static uint64_t const edges_us[REBUILD_FRAME_STATS_BUCKETS - 1] = { 2000, 5000, 10000, 20000, 50000 };
    int bucket = REBUILD_FRAME_STATS_BUCKETS - 1;
    int due = 0;

    assert(stats);
    if( !stats->open )
    {
        if( stats->after_left > 0 )
        {
            stats->after[stats->after_frames++] = rebuild_frame_stats_tenths(frame_us);
            if( frame_us >= REBUILD_FRAME_STATS_SLOW_AFTER_US && stats->marks > 1 )
                due |= REBUILD_FRAME_STATS_AFTER_SLOW;
            if( --stats->after_left == 0 )
                due |= REBUILD_FRAME_STATS_AFTER_DONE;
        }
        return due;
    }
    if( stats->frames == 0 && stats->armed )
        due |= REBUILD_FRAME_STATS_FIRST;
    if( stats->frames < REBUILD_FRAME_STATS_SERIES )
        stats->series[stats->frames] = rebuild_frame_stats_tenths(frame_us);
    for( int i = 0; i < REBUILD_FRAME_STATS_BUCKETS - 1; i++ )
        if( frame_us < edges_us[i] )
        {
            bucket = i;
            break;
        }
    stats->hist[bucket]++;
    stats->frames++;
    stats->sum_us += frame_us;
    stats->last_us = frame_us;
    if( frame_us > stats->max_us )
        stats->max_us = frame_us;
    if( !stats->closing )
        return due;
    /* The frame that finished the rebuild is the last one counted. */
    stats->open = 0;
    stats->after_left = REBUILD_FRAME_STATS_AFTER_FRAMES;
    stats->after_frames = 0;
    return due | REBUILD_FRAME_STATS_CLOSED;
}

int
RebuildFrameStats_SeriesCount(struct RebuildFrameStats const* stats)
{
    assert(stats);
    return stats->frames < REBUILD_FRAME_STATS_SERIES ? stats->frames : REBUILD_FRAME_STATS_SERIES;
}

void
RebuildFrameStats_CountOver(
    uint16_t const* tenths,
    int count,
    int* out_over8,
    int* out_over16,
    unsigned* out_max_tenths)
{
    assert(tenths);
    assert(out_over8);
    assert(out_over16);
    assert(out_max_tenths);
    *out_over8 = 0;
    *out_over16 = 0;
    *out_max_tenths = 0u;
    for( int i = 0; i < count; i++ )
    {
        unsigned const t = tenths[i];
        *out_over8 += t > 83u;
        *out_over16 += t > 167u;
        if( t > *out_max_tenths )
            *out_max_tenths = t;
    }
}

void
RebuildFrameStats_FormatSeries(
    uint16_t const* tenths,
    int count,
    char* out,
    size_t out_size)
{
    size_t used = 0u;

    assert(tenths);
    assert(out);
    assert(out_size > 0u);
    out[0] = '\0';
    for( int i = 0; i < count && used + 12u < out_size; i++ )
        used += (size_t)snprintf(out + used, out_size - used, "%s%u.%u", i ? "," : "",
            (unsigned)(tenths[i] / 10u), (unsigned)(tenths[i] % 10u));
}

void
RebuildFrameStats_FormatMarks(
    struct RebuildFrameStats const* stats,
    double min_ms,
    char* out,
    size_t out_size)
{
    size_t used = 0u;

    assert(stats);
    assert(out);
    assert(out_size > 0u);
    out[0] = '\0';
    for( int i = 1; i < stats->marks && used + 40u < out_size; i++ )
    {
        double const ms = (double)(stats->mark_us[i] - stats->mark_us[i - 1]) / 1000.0;
        if( ms >= min_ms )
            used += (size_t)snprintf(out + used, out_size - used, " %s=%.2f", stats->mark_name[i], ms);
    }
}
