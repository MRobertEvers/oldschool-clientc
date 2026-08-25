#ifndef TORIDRAW_FRAME_AB_H
#define TORIDRAW_FRAME_AB_H

/*
 * An A/B that switches arms INSIDE one run, because comparing two runs on this
 * box does not work.
 *
 * The Pentium 4 target is bimodal: the same binary drawing byte-identical
 * geometry takes either ~56.4 s or ~60.7 s for 1500 frames. That was proved by
 * diffing the logs of two runs of one executable -- 25 lines each, identical --
 * at 60.719 s and 56.453 s. The gap is 7.5%, and it is multiplicative, so it
 * does not shrink by running longer. Two hand-written kernels have now been
 * measured against it and both came back NEUTRAL with a 2-sigma bound of
 * +-3.2 s around an effect of about 0.6 s. The instrument, not the kernels,
 * is what produced those verdicts.
 *
 * Whatever causes the mode -- thermal, a background service, DRAM refresh --
 * it persists for a whole run. So the fix is to stop making the run the unit of
 * comparison. This alternates the two arms frame by frame within a single
 * process, in an ABBA cycle:
 *
 *     frame   0 1 2 3 4 5 6 7
 *     arm     A B B A A B B A
 *
 * ABBA is what makes it work, rather than plain alternation:
 *
 *   - It cancels linear drift within every group of four. If the machine is
 *     slowly getting slower, ABAB charges all of that drift to B; ABBA charges
 *     half to each.
 *   - It balances frame parity exactly. A gets frames 0 and 3 -- one even, one
 *     odd -- and B gets 1 and 2. The camera is pinned but the scene still
 *     animates, so adjacent frames are not identical work; over any whole
 *     number of cycles each arm has drawn the same count of even and odd
 *     frames, and any even/odd asymmetry cancels instead of becoming the
 *     result.
 *
 * The mode itself lasts far longer than four frames, so both arms sit inside
 * the same mode for essentially the whole run and it subtracts out.
 *
 * Enable with TORIDRAW_FRAME_AB=1. Disabled, ToriDraw_FrameAbArm() returns 0
 * and the caller takes its normal path; there is one predictable branch per
 * frame, on a 37 ms frame.
 */

#include <assert.h>
#include <stdint.h>

/** Which arm this frame belongs to: 0 = baseline, 1 = the change. */
int ToriDraw_FrameAbArm(void);

/** True when TORIDRAW_FRAME_AB=1 asked for the alternating comparison. */
int ToriDraw_FrameAbEnabled(void);

/** Start timing the region under test. Pairs with ToriDraw_FrameAbEnd. */
void ToriDraw_FrameAbBegin(void);

/**
 * @brief Stop timing and charge the elapsed time to this frame's arm.
 *
 * Also advances the frame counter, so the arm changes here and not at Begin --
 * a caller that reads the arm between Begin and End sees one consistent value.
 */
void ToriDraw_FrameAbEnd(void);

/**
 * @brief Write the comparison to stderr.
 *
 * Reports each arm's mean frame time and the difference, having discarded a
 * warmup prefix: the first frames of a run include scene load-in and cold
 * caches, and they land in whichever arm the cycle happens to give them.
 */
void ToriDraw_FrameAbDump(const char* label);

#endif /* TORIDRAW_FRAME_AB_H */
