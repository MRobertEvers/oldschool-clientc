#ifndef SRC_INV_INV_ICON_RETRY_H
#define SRC_INV_INV_ICON_RETRY_H

/*
 * When the next inventory obj-icon reconcile pass may run.
 *
 * One pass is in flight at a time. A pass that resolved at least one slot
 * (gave it an icon, or found its parts all resident and none to make) lets
 * the next pass run at once. A pass that resolved nothing -- every ask came
 * back without its parts -- makes the next one wait, and each further pass
 * that resolves nothing doubles the wait, from 500 ms up to a 4000 ms
 * ceiling. A changed inventory lifts the wait without resetting it, so a new
 * item is not held behind the pause of one whose parts did not arrive.
 *
 * The doubling is a rule of its own (InvIconRetry_NextWait) because the
 * placeholder park paces its misses the same way. It is
 * UITreeSceneBridge_PumpSprites' pacing, for the same reason: a read that
 * came back empty -- a dropped JS5 socket, a browser storage error -- is not
 * an answer, and an icon given up on after a handful of back-to-back asks
 * stays blank for the session, the asks all landing inside the outage. The
 * ceiling is what an outage costs after it ends.
 *
 * This does not know about containers, slots, a cache or a task runner. The
 * caller passes the clock, says whether any slot still lacks an icon, runs
 * the pass, counts what it resolved, and redraws.
 */

#include <stdbool.h>
#include <stdint.h>

/** The first wait after a pass that resolved nothing. */
#define INV_ICON_RETRY_FIRST_MS 500u
/** The longest wait. */
#define INV_ICON_RETRY_MAX_MS 4000u

struct InvIconRetry
{
    /** A pass is running. */
    int inflight;
    /** No pass starts before this clock value; 0 = none held. */
    uint64_t next_pass_ms;
    /** The wait the last unresolved pass set; 0 = none yet. */
    uint32_t wait_ms;
};

/** The wait after `wait_ms` (0 = none yet): doubled, up to the ceiling. */
uint32_t
InvIconRetry_NextWait(uint32_t wait_ms);

/** No pass in flight and the clock has reached the next pass. */
bool
InvIconRetry_Due(
    struct InvIconRetry const* retry,
    uint64_t now_ms);

/** A pass has been queued; none other is due until it ends. */
void
InvIconRetry_Begin(struct InvIconRetry* retry);

/**
 * What the pass came to. Resolving anything clears the wait; resolving
 * nothing backs it off and holds the next pass until `now_ms` plus the wait.
 * The pass is still in flight until InvIconRetry_End.
 */
void
InvIconRetry_NotePass(
    struct InvIconRetry* retry,
    int resolved,
    uint64_t now_ms);

/** The pass is over. */
void
InvIconRetry_End(struct InvIconRetry* retry);

/** An inventory changed: the next pass may run now. The wait is kept. */
void
InvIconRetry_Lift(struct InvIconRetry* retry);

/** A pass is running. */
bool
InvIconRetry_InFlight(struct InvIconRetry const* retry);

#endif /* SRC_INV_INV_ICON_RETRY_H */
