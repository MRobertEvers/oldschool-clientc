#ifndef SRC_GAME_RS_SAVE_DEBOUNCE_H
#define SRC_GAME_RS_SAVE_DEBOUNCE_H

/*
 * When a settings store that keeps changing is quiet long enough to be
 * written.
 *
 * A change marks the store with the cycle it was seen on, and every later
 * change moves the mark. The write is due once the mark is `settle` cycles or
 * more behind the current cycle; whoever writes then clears it. A slider drag
 * reported every cycle is therefore one write, after it stops.
 *
 * The mark is the cycle itself and 0 means clean, so a change marked at cycle
 * 0 leaves the store looking clean: no write falls due for it until a later
 * change marks it again.
 *
 * This does not know what the store is, how a change is detected, how long the
 * settle is or how the write is done. The caller notices the change, passes the
 * cycle and the settle, queues the write when it is due, and clears the mark.
 */

#include <stdbool.h>
#include <stdint.h>

struct RS_SaveDebounce
{
    /** Cycle of the latest change not yet written, or 0 when there is none. */
    uint64_t dirty_cycle;
};

/** The store changed on `cycle`. Marking at cycle 0 is the same as Clear. */
void
RS_SaveDebounce_Mark(
    struct RS_SaveDebounce* debounce,
    uint64_t cycle);

/**
 * A write that does not wait for the settle (a flush at shutdown): returns
 * whether one is wanted -- `changed`, or a change marked and not yet written
 * -- and clears the mark.
 */
bool
RS_SaveDebounce_Take(
    struct RS_SaveDebounce* debounce,
    bool changed);

/**
 * Whether a marked change has gone `settle` cycles or more without another:
 * false when nothing is marked. The subtraction is `cycle` less the mark in
 * 64-bit unsigned arithmetic.
 */
bool
RS_SaveDebounce_Due(
    struct RS_SaveDebounce const* debounce,
    uint64_t cycle,
    uint64_t settle);

/** The store is on disk (or about to be): nothing is marked. */
void
RS_SaveDebounce_Clear(struct RS_SaveDebounce* debounce);

#endif /* SRC_GAME_RS_SAVE_DEBOUNCE_H */
