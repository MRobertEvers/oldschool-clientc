#ifndef SRC_GAME_RS_CLIENTSCRIPT_QUEUE_H
#define SRC_GAME_RS_CLIENTSCRIPT_QUEUE_H

/*
 * RUNCLIENTSCRIPT payloads, held until the server's tick is complete.
 *
 * A server tick's packets arrive over several reads, and a clientscript run
 * the moment its packet lands reads a client that has applied some of that
 * tick and not the rest. The reference holds them to the tick boundary; so
 * does this. What releases them is the caller's business -- a SERVER_TICK_END
 * on revisions that send one, a dry pipeline on revisions that do not, and a
 * bounded backstop either way so a tick cut short by a disconnect costs one
 * tick of delay rather than the script.
 *
 * A RING rather than an array with a count, and that is the interesting part.
 * Dispatching a held script can push another -- CC_TRIGGEROP's queue drain
 * reaches this path -- so the flush is re-entrant, and a flush that walks an
 * array it is also being appended to will lose entries and run others twice.
 * Taking a snapshot of the count and popping from the front costs nothing and
 * cannot: a script pushed during the flush lands past the snapshot and waits
 * for the next one, which is exactly where it belongs.
 */

#include "net/rev/revpacket.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Payloads one server tick may hold before the fence.
 *
 * Measured rather than guessed: the busiest tick in this tree is a panel open
 * (`~pricechecker_open` pushes two, a bank open pushes six), and login's burst
 * is the outlier at just under twenty. 64 leaves that room; past it the caller
 * runs the script immediately, so the cap costs ordering and never a script.
 */
#define RS_CLIENTSCRIPT_QUEUE_MAX 64

struct RS_ClientScriptQueue
{
    struct PktRunClientScript items[RS_CLIENTSCRIPT_QUEUE_MAX];
    int head;
    int count;
    /** Logic cycle the oldest held payload arrived on -- what the backstop
     *  measures from. Meaningless while the queue is empty. */
    uint32_t held_since_cycle;
};

void
RS_ClientScriptQueue_Reset(struct RS_ClientScriptQueue* queue);

/**
 * Hold one payload until the fence. False when the queue is full, and then the
 * caller runs it now: a script that arrives late is better than one that never
 * arrives, and the cap is set where that trade has already been made.
 */
bool
RS_ClientScriptQueue_Hold(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript const* request,
    uint32_t cycle);

/** How many are waiting. */
int
RS_ClientScriptQueue_Count(struct RS_ClientScriptQueue const* queue);

/**
 * Take the oldest held payload, or false when there is none.
 *
 * Copied out rather than handed back by pointer, because the caller dispatches
 * it and the dispatch can push -- and a push into the slot just vacated would
 * rewrite the payload being run.
 */
bool
RS_ClientScriptQueue_Pop(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript* out);

/**
 * Whether the oldest held payload has waited longer than the fence is allowed
 * to take.
 *
 * The backstop for a tick that was cut short: a fence that never arrives would
 * otherwise strand a script for the rest of the session. False on an empty
 * queue -- nothing is waiting, so nothing is overdue.
 */
bool
RS_ClientScriptQueue_FenceOverdue(
    struct RS_ClientScriptQueue const* queue,
    uint32_t cycle,
    uint32_t max_cycles);

#endif /* SRC_GAME_RS_CLIENTSCRIPT_QUEUE_H */
