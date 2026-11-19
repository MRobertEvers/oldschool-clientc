#ifndef SRC_PERF_BOOT_PUMP_STATS_H
#define SRC_PERF_BOOT_PUMP_STATS_H

/*
 * What the frame loop's task pump counts about itself, and whether its last
 * pass left work queued.
 *
 * During a bake: how many frames the boot took, how many scheduler steps ran
 * in total, and how many of those frames reached the per-frame step budget.
 * The last one says whether the boot is bound by work or by the frame loop
 * handing it too few slices. After the boot: the frames that used their whole
 * budget with work still pending, and the steps they ran -- the frame queue
 * being drip-fed a slice at a time. A pump that is not a frame counts no
 * frame, and a frame that settled a CS2 transaction is not a busy frame.
 *
 * The pending flag describes the last pass only: it is cleared when a pass
 * begins and raised when the pass ends with the runner not idle. The frame
 * loop reads it to skip its pacing sleep, because sleeping through queued
 * work paces the download instead of the screen.
 *
 * This does not know what a step is, what the budget is, or whether a bake
 * is running. The caller passes the step count, the budget and what kind of
 * pass it was, and prints the report (TORIRS_BOOT_STATS) from the readers.
 */

#include <stdbool.h>

struct BootPumpStats
{
    int boot_frames;
    long boot_steps;
    int boot_frames_budget_capped;
    int busy_frames;
    long busy_steps;
    /** The last pass ended with the runner not idle. */
    int async_pending;
};

/** No frames, no steps, nothing pending. */
void
BootPumpStats_Reset(struct BootPumpStats* stats);

/** `steps` scheduler steps ran during a bake. */
void
BootPumpStats_NoteBootSteps(
    struct BootPumpStats* stats,
    long steps);

/** A pump pass begins: nothing is pending until it says so. */
void
BootPumpStats_PassBegin(struct BootPumpStats* stats);

/**
 * A pump pass ran `steps` steps against `budget`.
 *
 * Counts nothing unless `from_frame`. A `booting` frame counts as a boot
 * frame, capped when it reached the budget; otherwise a frame that reached
 * the budget without `settling_cs2` counts as busy, with its steps.
 */
void
BootPumpStats_NoteFrame(
    struct BootPumpStats* stats,
    bool from_frame,
    bool booting,
    bool settling_cs2,
    int steps,
    int budget);

/** The pass ended with the runner not idle. */
void
BootPumpStats_SetPending(struct BootPumpStats* stats);

/** The last pass left work queued. */
int
BootPumpStats_Pending(struct BootPumpStats const* stats);

int
BootPumpStats_BootFrames(struct BootPumpStats const* stats);

long
BootPumpStats_BootSteps(struct BootPumpStats const* stats);

int
BootPumpStats_BootFramesCapped(struct BootPumpStats const* stats);

int
BootPumpStats_BusyFrames(struct BootPumpStats const* stats);

long
BootPumpStats_BusySteps(struct BootPumpStats const* stats);

#endif /* SRC_PERF_BOOT_PUMP_STATS_H */
