#ifndef SRC_ENGINE_WORLD_BUILDER_REBUILD_FRAME_STATS_H
#define SRC_ENGINE_WORLD_BUILDER_REBUILD_FRAME_STATS_H

/*
 * The frame times around a world rebuild: every frame from the one a REBUILD
 * started in to the one it finished in, and the frames just after it.
 *
 * A window is opened when a rebuild starts, in either mode, and marked
 * closing when the rebuild finishes. Every frame noted while it is open is
 * counted, summed, its longest and last kept, put in one of six buckets
 * (under 2, 5, 10, 20 and 50 ms, and the rest) and, for the first 192, kept
 * in a series in tenths of a ms. The frame noted while it is closing is the
 * last one counted: the window then closes, and the next 48 frames are kept in
 * a series of their own. The commit's split -- the build or swap, the entity
 * shift, the finish, the held packets -- is recorded beside it, and so are up
 * to 32 named marks through the current frame, each with its time, while a
 * window or the frames after it are open.
 *
 * A noted frame says which reports are due: the closed window's, the frames
 * after it once all 48 are in, and a slow frame among those.
 *
 * Nothing here reads a clock, decides whether timing is on or prints. The
 * caller passes each frame's time and each mark's, opens a window only when
 * timing is on, and formats and prints the reports from what this holds.
 */

#include <stddef.h>
#include <stdint.h>

#define REBUILD_FRAME_STATS_BUCKETS 6
#define REBUILD_FRAME_STATS_SERIES 192
#define REBUILD_FRAME_STATS_AFTER 64
#define REBUILD_FRAME_STATS_MARKS 32
/* Frames kept after a window closes. */
#define REBUILD_FRAME_STATS_AFTER_FRAMES 48
/* A frame after the window this long or longer reports its marks. */
#define REBUILD_FRAME_STATS_SLOW_AFTER_US 12000u

/* What a noted frame made due (RebuildFrameStats_Note). */
enum
{
    /* The window closed on this frame. */
    REBUILD_FRAME_STATS_CLOSED = 1 << 0,
    /* A frame after the window was slow: its marks are due. */
    REBUILD_FRAME_STATS_AFTER_SLOW = 1 << 1,
    /* The last frame after the window is in. */
    REBUILD_FRAME_STATS_AFTER_DONE = 1 << 2,
};

struct RebuildFrameStats
{
    int open;
    int closing;
    int background;
    int frames;
    uint64_t max_us;
    uint64_t sum_us;
    uint64_t last_us;
    int hist[REBUILD_FRAME_STATS_BUCKETS];
    double build_ms;
    double tail_ms;
    /* The commit frame's split, background mode: the swap (or the build in
     * place), the entity shift, the finish, and the held packets popped
     * behind the commit in the same frame. */
    double swap_ms;
    double shift_ms;
    double finish_ms;
    double held_ms;
    /* Every frame's time, in tenths of a ms: the window's (series) and, after
     * it closes, the frames that follow it. */
    uint16_t series[REBUILD_FRAME_STATS_SERIES];
    int after_left;
    int after_frames;
    uint16_t after[REBUILD_FRAME_STATS_AFTER];
    /* The current frame's stages, each with the time it ended. */
    char const* mark_name[REBUILD_FRAME_STATS_MARKS];
    uint64_t mark_us[REBUILD_FRAME_STATS_MARKS];
    int marks;
};

/** No window, nothing after one, no marks. */
void
RebuildFrameStats_Reset(struct RebuildFrameStats* stats);

/** Is the series after a window still being filled? Its report is due
 *  before a new window is opened. */
int
RebuildFrameStats_AfterPending(struct RebuildFrameStats const* stats);

/** Open a window, forgetting everything the last one held. `background`: the
 *  rebuild is a background job. */
void
RebuildFrameStats_Open(
    struct RebuildFrameStats* stats,
    int background);

/** The rebuild has finished: the next noted frame closes the window. */
void
RebuildFrameStats_Finished(struct RebuildFrameStats* stats);

/** The rebuild was abandoned: the window is closed with no report. */
void
RebuildFrameStats_Drop(struct RebuildFrameStats* stats);

/** A new frame: forget the last frame's marks. */
void
RebuildFrameStats_ClearMarks(struct RebuildFrameStats* stats);

/** Is a mark taken now? Only while a window or the frames after it are
 *  open, and while there is room. */
int
RebuildFrameStats_WantsMark(struct RebuildFrameStats const* stats);

/** Record a mark the caller has been told is wanted. `name` must outlive the
 *  frame. */
void
RebuildFrameStats_Mark(
    struct RebuildFrameStats* stats,
    char const* name,
    uint64_t now_us);

/** The commit's build/swap and shift. */
void
RebuildFrameStats_SetCommit(
    struct RebuildFrameStats* stats,
    double build_ms,
    double swap_ms,
    double shift_ms);

/** The commit's tail (everything after the swap) and finish. */
void
RebuildFrameStats_SetTail(
    struct RebuildFrameStats* stats,
    double tail_ms,
    double finish_ms);

/** The held packets popped behind the commit. */
void
RebuildFrameStats_SetHeld(
    struct RebuildFrameStats* stats,
    double held_ms);

/** Note one frame of `frame_us`. Returns the REBUILD_FRAME_STATS_* reports
 *  it made due. */
int
RebuildFrameStats_Note(
    struct RebuildFrameStats* stats,
    uint64_t frame_us);

/** How many frames of the window's series are kept. */
int
RebuildFrameStats_SeriesCount(struct RebuildFrameStats const* stats);

/**
 * Over `count` times in tenths of a ms: how many are over 8.3 ms and over
 * 16.7 ms, and the longest.
 */
void
RebuildFrameStats_CountOver(
    uint16_t const* tenths,
    int count,
    int* out_over8,
    int* out_over16,
    unsigned* out_max_tenths);

/** "a,b,c" in ms with one decimal, into `out` (truncated to fit). */
void
RebuildFrameStats_FormatSeries(
    uint16_t const* tenths,
    int count,
    char* out,
    size_t out_size);

/**
 * " name=ms" for every mark after the first, the ms since the one before it,
 * leaving out those under `min_ms`, into `out` (truncated to fit).
 */
void
RebuildFrameStats_FormatMarks(
    struct RebuildFrameStats const* stats,
    double min_ms,
    char* out,
    size_t out_size);

#endif
