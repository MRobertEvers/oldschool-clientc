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

#include "game/rs_server_tick.h"
#include "net/rev/revpacket.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * The capacity the hold starts with.
 *
 * Measured rather than guessed: the busiest tick in this tree is a panel open
 * (`~pricechecker_open` pushes two, a bank open pushes six), and login's burst
 * is the outlier at just under twenty. 64 leaves that room; a tick that holds
 * more GROWS the hold (doubling) rather than running its overflow anywhere
 * else. Order matters more than the cap: a script run on another queue runs
 * out of wire order, ahead of the tick's packets it was held behind.
 */
#define RS_CLIENTSCRIPT_QUEUE_INITIAL 64

/**
 * The ceiling the hold may grow to. A tick that holds this many scripts is a
 * fence that never comes, which the backstop below releases long before; past
 * it is an assert, never a dropped or reordered script.
 */
#define RS_CLIENTSCRIPT_QUEUE_CEILING 65536

/**
 * Logic cycles a held clientscript may wait for a fence that never comes.
 *
 * One whole server tick — long enough that a healthy connection never reaches
 * it, short enough that a tick truncated by a disconnect costs one tick of
 * delay rather than the script.
 */
#define APP_CLIENTSCRIPT_FENCE_MAX_CYCLES APP_SERVER_TICK_LOGIC_CYCLES

struct RS_ClientScriptQueue
{
    /** `capacity` slots (ToriMem_Script), a ring from `head`. */
    struct PktRunClientScript* items;
    int capacity;
    int head;
    int count;
    /** Logic cycle the oldest held payload arrived on -- what the backstop
     *  measures from. Meaningless while the queue is empty. */
    uint32_t held_since_cycle;
};

/** Make an empty hold of RS_CLIENTSCRIPT_QUEUE_INITIAL slots. */
void
RS_ClientScriptQueue_Init(struct RS_ClientScriptQueue* queue);

/** Release the slots (NULL is accepted). Unusable until Init again. */
void
RS_ClientScriptQueue_Free(struct RS_ClientScriptQueue* queue);

/** Forget every held payload, keeping the slots: a logout, whose session's
 *  scripts never run. */
void
RS_ClientScriptQueue_Reset(struct RS_ClientScriptQueue* queue);

/**
 * Hold one payload until the fence. Always holds it: a full hold grows
 * (doubling, up to RS_CLIENTSCRIPT_QUEUE_CEILING, past which it asserts), so
 * no script is ever run ahead of the packets it was held behind.
 */
void
RS_ClientScriptQueue_Hold(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript const* request,
    uint32_t cycle);

/** How many are waiting. */
int
RS_ClientScriptQueue_Count(struct RS_ClientScriptQueue const* queue);

/** How many the hold has room for before it next grows. */
int
RS_ClientScriptQueue_Capacity(struct RS_ClientScriptQueue const* queue);

/**
 * Take the oldest held payload, or false when there is none.
 *
 * Copied out rather than handed back by pointer, because the caller dispatches
 * it and the dispatch can push -- and a push into the slot just vacated (or a
 * push that grows the hold) would rewrite the payload being run.
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
