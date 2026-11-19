#ifndef SRC_ENGINE_WORLD_BUILDER_REBUILD_PACE_H
#define SRC_ENGINE_WORLD_BUILDER_REBUILD_PACE_H

/*
 * The paced catch-up after a background rebuild's commit: how much packet and
 * script work each frame may do, and for how long after the commit that limit
 * holds.
 *
 * While a background rebuild gathers, the packets behind its REBUILD are held.
 * When it commits they are all due at once, and so are the scripts they
 * dispatch. Applied in one frame, that is a frame long enough to see. So the
 * commit frame does the commit and nothing else, and for a window after it
 * each frame gets a budget: the first time a frame asks, its deadline is fixed
 * one budget ahead, so a frame always gets its budget however late in it the
 * work starts. A packet is never cut, so a frame can overrun by one packet.
 *
 * Work stopped for a deadline keeps pacing alive a little past the stop, so
 * what the window left queued does not all land on the frame the window ends;
 * nothing keeps it alive past a hard limit after the commit.
 *
 * Nothing here reads a clock, pops a packet or runs a script, and nothing here
 * prints. The caller passes the time and the frame serial it means by "this
 * frame", reads the environment for the budget and the window, does the work
 * up to the deadline it is given, and says when that work stopped.
 */

#include <stdint.h>

/* The defaults the caller configures with when the environment says nothing:
 * the work per frame, and how long after a commit it stays paced. */
#define REBUILD_PACE_BUDGET_US_DEFAULT 4000
#define REBUILD_PACE_WINDOW_MS_DEFAULT 1000
/* The slice the commit frame's own follow-up scripts get. */
#define REBUILD_PACE_COMMIT_FRAME_US 2000
/* A stop keeps pacing alive this long past it; nothing keeps it alive longer
 * than the hard limit after the commit. */
#define REBUILD_PACE_STOP_LINGER_US 100000
#define REBUILD_PACE_HARD_LIMIT_US 3000000

struct RebuildPace
{
    /** Work one paced frame may do; 0 turns pacing off. */
    uint64_t budget_us;
    /** How long after the commit pacing lasts, before any stop extends it. */
    uint64_t window_us;
    /** Pacing ends at this time; 0: not pacing. */
    uint64_t until_us;
    /** However much is still stopping, pacing ends here. */
    uint64_t hard_until_us;
    /** This frame's paced work stops here; valid for `deadline_frame`. */
    uint64_t deadline_us;
    uint64_t deadline_frame;
    /** The frame the commit ran on. */
    uint64_t commit_frame;
    /** The last frame a packet was popped on while paced. */
    uint64_t pop_frame;
    /** Packets and frame-queue passes stopped since the last commit. */
    int stat_pop_stops;
    int stat_settle_stops;
};

/** Not pacing, nothing counted, and pacing off until configured. */
void
RebuildPace_Reset(struct RebuildPace* pace);

/** The budget per frame (0: pacing off) and the window after a commit. */
void
RebuildPace_Configure(
    struct RebuildPace* pace,
    uint64_t budget_us,
    uint64_t window_us);

/** Is a window open? Nothing else here needs a time while it is not. */
int
RebuildPace_Pacing(struct RebuildPace const* pace);

/**
 * Start pacing: the commit ran at `now_us` on frame `frame`. The commit frame's
 * deadline is a short slice, and no packet is popped on it. No-op when pacing
 * is off (budget 0). Clears the stop counts.
 */
void
RebuildPace_Arm(
    struct RebuildPace* pace,
    uint64_t now_us,
    uint64_t frame);

/**
 * The time this frame's paced work must stop at, or 0 when nothing is paced.
 *
 * `now_us` is read only while pacing. When it has passed the end of the
 * window, pacing stops here and `*out_ended` is set to 1 so the caller can
 * report the stop counts, which stay as they were until the next Arm.
 */
uint64_t
RebuildPace_Deadline(
    struct RebuildPace* pace,
    uint64_t now_us,
    uint64_t frame,
    int* out_ended);

/**
 * Paced work was stopped for this frame's deadline at `now_us`: pacing lasts
 * at least the linger past it, up to the hard limit. No-op when not pacing.
 */
void
RebuildPace_NoteStop(
    struct RebuildPace* pace,
    uint64_t now_us);

/**
 * May the pump pop another packet this frame, given the frame's nonzero
 * `deadline_us` and the time `now_us`? Never on the commit frame. Otherwise
 * yes inside the budget, and yes for the frame's first packet whatever the
 * budget says: the frame queue is paced from the same deadline and runs first,
 * and a frame it filled must not leave the packets behind it standing for the
 * whole window.
 *
 * Returns 1 when the pop is held, and counts it; the caller then notes the
 * stop (RebuildPace_NoteStop) at the time it stops.
 */
int
RebuildPace_HoldsPop(
    struct RebuildPace* pace,
    uint64_t deadline_us,
    uint64_t now_us,
    uint64_t frame);

/** A frame-queue pass was stopped for the deadline: count it. */
void
RebuildPace_NoteSettleStop(struct RebuildPace* pace);

#endif
