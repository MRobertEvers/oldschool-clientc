/*
 * The frame times around a world rebuild, driven with made-up frame times and
 * mark times. `make -C src test-rebuild-frame-stats`.
 */
#include "engine/world_builder/rebuild_frame_stats.h"

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

static void
opened(
    struct RebuildFrameStats* stats,
    int background)
{
    memset(stats, 0x5a, sizeof(*stats));
    RebuildFrameStats_Reset(stats);
    RebuildFrameStats_Open(stats, background);
}

static void
test_a_reset_window_counts_nothing(void)
{
    struct RebuildFrameStats stats;

    memset(&stats, 0x5a, sizeof(stats));
    RebuildFrameStats_Reset(&stats);
    TEST_ASSERT(!stats.open, "not open");
    TEST_ASSERT(!RebuildFrameStats_AfterPending(&stats), "nothing after");
    TEST_ASSERT(!RebuildFrameStats_WantsMark(&stats), "no marks wanted");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 5000) == 0, "a frame makes nothing due");
    TEST_ASSERT(stats.frames == 0, "and is not counted");
    RebuildFrameStats_Finished(&stats);
    TEST_ASSERT(!stats.closing, "finishing no window closes nothing");
}

static void
test_the_window_counts_buckets_and_closes(void)
{
    struct RebuildFrameStats stats;
    uint64_t const frames[] = { 1000, 3000, 7000, 15000, 30000, 80000, 1999, 2000 };

    opened(&stats, 1);
    TEST_ASSERT(stats.open && stats.background, "open, background");
    for( size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++ )
        TEST_ASSERT(RebuildFrameStats_Note(&stats, frames[i]) == 0, "an open window reports nothing");
    TEST_ASSERT(stats.frames == 8, "every frame counted");
    TEST_ASSERT(stats.sum_us == 139999, "summed");
    TEST_ASSERT(stats.max_us == 80000, "the longest");
    TEST_ASSERT(stats.last_us == 2000, "the last");
    TEST_ASSERT(stats.hist[0] == 2, "under 2 ms (2000 is not)");
    TEST_ASSERT(stats.hist[1] == 2, "under 5 ms");
    TEST_ASSERT(stats.hist[2] == 1, "under 10 ms");
    TEST_ASSERT(stats.hist[3] == 1, "under 20 ms");
    TEST_ASSERT(stats.hist[4] == 1, "under 50 ms");
    TEST_ASSERT(stats.hist[5] == 1, "the rest");
    TEST_ASSERT(stats.series[0] == 10 && stats.series[5] == 800, "the series in tenths");

    RebuildFrameStats_Finished(&stats);
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 4000) == REBUILD_FRAME_STATS_CLOSED, "the closing frame");
    TEST_ASSERT(stats.frames == 9 && stats.last_us == 4000, "the closing frame is counted");
    TEST_ASSERT(!stats.open, "closed");
    TEST_ASSERT(RebuildFrameStats_AfterPending(&stats), "the frames after it are kept");
    TEST_ASSERT(stats.after_left == REBUILD_FRAME_STATS_AFTER_FRAMES, "for the whole tail");
    TEST_ASSERT(RebuildFrameStats_SeriesCount(&stats) == 9, "nine in the series");
}

static void
test_the_series_stops_at_its_end(void)
{
    struct RebuildFrameStats stats;

    opened(&stats, 0);
    for( int i = 0; i < REBUILD_FRAME_STATS_SERIES + 10; i++ )
        (void)RebuildFrameStats_Note(&stats, 100u * (uint64_t)(i + 1));
    TEST_ASSERT(stats.frames == REBUILD_FRAME_STATS_SERIES + 10, "every frame counted");
    TEST_ASSERT(RebuildFrameStats_SeriesCount(&stats) == REBUILD_FRAME_STATS_SERIES, "the series is capped");
    TEST_ASSERT(stats.series[REBUILD_FRAME_STATS_SERIES - 1] == REBUILD_FRAME_STATS_SERIES, "the last kept");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 10000000u) == 0, "not closing");
    TEST_ASSERT(stats.max_us == 10000000u, "a long frame is still the longest");
}

static void
test_the_frames_after(void)
{
    struct RebuildFrameStats stats;
    int due = 0;

    opened(&stats, 0);
    RebuildFrameStats_Finished(&stats);
    (void)RebuildFrameStats_Note(&stats, 3000);
    for( int i = 0; i < REBUILD_FRAME_STATS_AFTER_FRAMES - 1; i++ )
    {
        due = RebuildFrameStats_Note(&stats, 1000);
        TEST_ASSERT(!(due & REBUILD_FRAME_STATS_AFTER_DONE), "not done before the last");
    }
    TEST_ASSERT(stats.after_frames == REBUILD_FRAME_STATS_AFTER_FRAMES - 1, "kept");
    due = RebuildFrameStats_Note(&stats, 2000);
    TEST_ASSERT(due == REBUILD_FRAME_STATS_AFTER_DONE, "the last frame after it");
    TEST_ASSERT(!RebuildFrameStats_AfterPending(&stats), "nothing left after");
    TEST_ASSERT(stats.after[REBUILD_FRAME_STATS_AFTER_FRAMES - 1] == 20, "in tenths");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 2000) == 0, "nothing is due after that");
    TEST_ASSERT(stats.after_frames == REBUILD_FRAME_STATS_AFTER_FRAMES, "and nothing kept");
}

static void
test_a_slow_frame_after_reports_its_marks(void)
{
    struct RebuildFrameStats stats;

    opened(&stats, 0);
    RebuildFrameStats_Finished(&stats);
    (void)RebuildFrameStats_Note(&stats, 3000);
    TEST_ASSERT(RebuildFrameStats_WantsMark(&stats), "marks are kept through the tail");
    RebuildFrameStats_ClearMarks(&stats);
    RebuildFrameStats_Mark(&stats, "start", 100);
    TEST_ASSERT(
        RebuildFrameStats_Note(&stats, REBUILD_FRAME_STATS_SLOW_AFTER_US) == 0,
        "one mark is not a split");
    RebuildFrameStats_ClearMarks(&stats);
    RebuildFrameStats_Mark(&stats, "start", 100);
    RebuildFrameStats_Mark(&stats, "body", 5100);
    TEST_ASSERT(
        RebuildFrameStats_Note(&stats, REBUILD_FRAME_STATS_SLOW_AFTER_US - 1) == 0,
        "a frame under the line is not slow");
    TEST_ASSERT(
        RebuildFrameStats_Note(&stats, REBUILD_FRAME_STATS_SLOW_AFTER_US) == REBUILD_FRAME_STATS_AFTER_SLOW,
        "a slow frame after the window");
}

static void
test_the_marks(void)
{
    struct RebuildFrameStats stats;
    char line[REBUILD_FRAME_STATS_MARKS * 40];

    opened(&stats, 0);
    RebuildFrameStats_Mark(&stats, "start", 1000);
    RebuildFrameStats_Mark(&stats, "body", 1200);
    RebuildFrameStats_Mark(&stats, "floor", 4700);
    RebuildFrameStats_FormatMarks(&stats, 0.0, line, sizeof(line));
    TEST_ASSERT(strcmp(line, " body=0.20 floor=3.50") == 0, "every split");
    RebuildFrameStats_FormatMarks(&stats, 0.3, line, sizeof(line));
    TEST_ASSERT(strcmp(line, " floor=3.50") == 0, "only the splits of 0.3 ms or more");

    RebuildFrameStats_ClearMarks(&stats);
    for( int i = 0; i < REBUILD_FRAME_STATS_MARKS; i++ )
        RebuildFrameStats_Mark(&stats, "m", (uint64_t)i);
    TEST_ASSERT(!RebuildFrameStats_WantsMark(&stats), "no room past the last");

    RebuildFrameStats_Drop(&stats);
    RebuildFrameStats_ClearMarks(&stats);
    TEST_ASSERT(!RebuildFrameStats_WantsMark(&stats), "a dropped window wants no marks");
}

static void
test_the_commit_split_and_a_reopen(void)
{
    struct RebuildFrameStats stats;

    opened(&stats, 1);
    RebuildFrameStats_SetCommit(&stats, 3.0, 3.0, 0.5);
    RebuildFrameStats_SetTail(&stats, 2.0, 1.5);
    RebuildFrameStats_SetHeld(&stats, 0.25);
    TEST_ASSERT(stats.build_ms == 3.0 && stats.swap_ms == 3.0 && stats.shift_ms == 0.5, "the commit");
    TEST_ASSERT(stats.tail_ms == 2.0 && stats.finish_ms == 1.5, "the tail");
    TEST_ASSERT(stats.held_ms == 0.25, "the held packets");
    (void)RebuildFrameStats_Note(&stats, 9000);

    RebuildFrameStats_Open(&stats, 0);
    TEST_ASSERT(stats.open && !stats.background, "reopened, blocking");
    TEST_ASSERT(stats.frames == 0 && stats.build_ms == 0.0 && stats.held_ms == 0.0, "the last one is forgotten");
}

static void
test_count_and_format(void)
{
    uint16_t const tenths[] = { 12, 84, 83, 168, 167, 0 };
    char out[64];
    char tiny[14];
    int over8 = -1;
    int over16 = -1;
    unsigned max = 0;

    RebuildFrameStats_CountOver(tenths, 6, &over8, &over16, &max);
    TEST_ASSERT(over8 == 3, "over 8.3 ms");
    TEST_ASSERT(over16 == 1, "over 16.7 ms");
    TEST_ASSERT(max == 168, "the longest");
    RebuildFrameStats_CountOver(tenths, 0, &over8, &over16, &max);
    TEST_ASSERT(over8 == 0 && over16 == 0 && max == 0, "nothing counted");

    RebuildFrameStats_FormatSeries(tenths, 3, out, sizeof(out));
    TEST_ASSERT(strcmp(out, "1.2,8.4,8.3") == 0, "a series");
    RebuildFrameStats_FormatSeries(tenths, 0, out, sizeof(out));
    TEST_ASSERT(out[0] == '\0', "an empty series");
    RebuildFrameStats_FormatSeries(tenths, 6, tiny, sizeof(tiny));
    TEST_ASSERT(strcmp(tiny, "1.2") == 0, "cut to fit");
}

/* Armed stats mark every frame: the frame a window opens in keeps the marks
 * it took before the opening, and its note -- only its, only when armed --
 * makes the first frame's report due. A blocking rebuild's one frame is both
 * the first and the closing one. */
static void
test_armed_stats_report_the_first_frame_whole(void)
{
    struct RebuildFrameStats stats;
    char line[REBUILD_FRAME_STATS_MARKS * 40];

    memset(&stats, 0x5a, sizeof(stats));
    RebuildFrameStats_Reset(&stats);
    TEST_ASSERT(!RebuildFrameStats_WantsMark(&stats), "reset: not armed");
    RebuildFrameStats_Arm(&stats);
    TEST_ASSERT(RebuildFrameStats_WantsMark(&stats), "armed: marks with no window");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 9000) == 0, "a frame with no window makes nothing due");
    RebuildFrameStats_ClearMarks(&stats);
    RebuildFrameStats_Mark(&stats, "start", 1000);
    RebuildFrameStats_Mark(&stats, "tick_pump", 1500);
    RebuildFrameStats_Open(&stats, 1);
    RebuildFrameStats_Mark(&stats, "bg_floor", 2500);
    TEST_ASSERT(stats.marks == 3, "the marks before the opening are kept");
    RebuildFrameStats_FormatMarks(&stats, 0.0, line, sizeof(line));
    TEST_ASSERT(strcmp(line, " tick_pump=0.50 bg_floor=1.00") == 0, "the whole frame's splits");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 3000) == REBUILD_FRAME_STATS_FIRST, "the first frame is due");
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 3000) == 0, "the second is not");
    RebuildFrameStats_Finished(&stats);
    TEST_ASSERT(RebuildFrameStats_Note(&stats, 3000) == REBUILD_FRAME_STATS_CLOSED, "the closing frame closes");
    TEST_ASSERT(RebuildFrameStats_WantsMark(&stats), "still armed after the window");

    RebuildFrameStats_Open(&stats, 0);
    RebuildFrameStats_Finished(&stats);
    TEST_ASSERT(
        RebuildFrameStats_Note(&stats, 3000) == (REBUILD_FRAME_STATS_FIRST | REBUILD_FRAME_STATS_CLOSED),
        "a one-frame window is first and closing at once");
}

int
main(void)
{
    test_a_reset_window_counts_nothing();
    test_the_window_counts_buckets_and_closes();
    test_the_series_stops_at_its_end();
    test_the_frames_after();
    test_a_slow_frame_after_reports_its_marks();
    test_the_marks();
    test_the_commit_split_and_a_reopen();
    test_count_and_format();
    test_armed_stats_report_the_first_frame_whole();
    if( g_failures )
    {
        printf("rebuild_frame_stats_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rebuild_frame_stats_test: ok\n");
    return 0;
}
