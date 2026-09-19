#ifndef SRC_PERF_FRAME_TIME_RING_H
#define SRC_PERF_FRAME_TIME_RING_H

/*
 * The last few frame durations, and their mean.
 *
 * What a frame-rate readout differences. Kept as a ring because the readout
 * wants an average and the damage tracker wants the newest, and neither wants
 * a running total that a single stalled frame poisons for the rest of the
 * session.
 */

#include <stdint.h>

/** Frames the readout averages over. Small on purpose: this is meant to move
 *  while you watch it, not to settle. */
#define FRAME_TIME_RING_SAMPLES 10

struct FrameTimeRing
{
    uint32_t samples_us[FRAME_TIME_RING_SAMPLES];
    /** Where the NEXT sample lands, so the newest is the slot before it. */
    int head;
    /** Samples written so far, capped at FRAME_TIME_RING_SAMPLES. */
    int count;
};

void
FrameTimeRing_Reset(struct FrameTimeRing* ring);

/**
 * Record one frame.
 *
 * Saturating: a frame longer than about 71 minutes is stored as the largest
 * duration there is rather than wrapping to a small one. A wrapped sample
 * would make the single worst frame of a session read as the best.
 */
void
FrameTimeRing_Add(
    struct FrameTimeRing* ring,
    uint64_t frame_us);

/** The most recent frame, or 0 when none has been recorded -- which is the
 *  same answer, because a reset ring is a ring of zeroes. */
uint32_t
FrameTimeRing_NewestUs(struct FrameTimeRing const* ring);

/** Mean of the samples HELD so far, not of the ring -- a ring that is a third
 *  full must not be averaged against seven zeroes. 0 when there are none. */
uint32_t
FrameTimeRing_MeanUs(struct FrameTimeRing const* ring);

#endif /* SRC_PERF_FRAME_TIME_RING_H */
