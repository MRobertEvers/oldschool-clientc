#ifndef SRC_GAME_RS_SERVER_TICK_H
#define SRC_GAME_RS_SERVER_TICK_H

/*
 * Whether the server tick being applied is complete, and how many have been.
 *
 * A server tick's packets arrive over several reads, so the packet queue runs
 * dry inside a tick as often as at its end. On a revision that sends
 * SERVER_TICK_END that packet is the only boundary used: once one has been
 * seen, the first packet of a tick that may change the UI OPENS the tick, and
 * the fence closes it. While it is open the live tree may hold only part of
 * the tick, so the renderer keeps the preceding committed frame. This is the
 * same fence the clientscript hold in game/rs_clientscript_queue.h waits for.
 *
 * Two things are remembered about an open tick besides the fact of it:
 *
 *   - The cycle it opened on, which the lost-fence backstop measures from. A
 *     fence cut short by a disconnect would otherwise latch the picture for
 *     good.
 *   - Whether any of its UI packets so far was something other than a plain
 *     VARP_SMALL / VARP_LARGE. A tick open on varps alone is the shape a
 *     REBUILD arrives in, and the one a background rebuild may suspend the
 *     latch for while it holds, re-arming it at its commit.
 *
 * The serial counts executed ticks in wire order: one per SERVER_TICK_END, or
 * one per PLAYER_INFO on a revision that sends no fence. A loc change stamped
 * with an earlier serial than the packet reading it is one the reference has
 * already built.
 *
 * This does not know which packets may change the UI, which packet is the
 * fence on a given revision, or how long the backstop allows; the one ratio
 * between the two clocks, APP_SERVER_TICK_LOGIC_CYCLES, sits beside it for
 * every caller that converts. The caller classifies each packet, passes the
 * cycle and the bound, and does whatever the answer calls for: logging,
 * flushing held scripts, redrawing.
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * Logic cycles in one server tick: 600ms of server against a 20ms client
 * cycle. The client has two clocks and this is the only ratio between them,
 * so anything converting a server-tick duration into cycles says it here
 * (UPDATE_REBOOT_TIMER's countdown, the clientscript fence of
 * game/rs_clientscript_queue.h).
 */
#define APP_SERVER_TICK_LOGIC_CYCLES 30

struct RS_ServerTick
{
    /** This connection has sent SERVER_TICK_END at least once. Until then the
     *  dry-pipeline fallback is the boundary (a revision that sends none). */
    int fence_seen;
    /** A fenced tick has started applying and its SERVER_TICK_END has not yet
     *  executed. */
    int open;
    /** Logic cycle the open tick opened (or was re-armed) on, truncated as the
     *  caller's cycle is stored. Meaningless while the tick is closed. */
    int open_cycle;
    /** Some packet of the open tick so far was a UI mutator other than a plain
     *  varp. Recomputed from each tick's opening packet. */
    int open_nonvar;
    /** Server ticks executed, ever. */
    unsigned serial;
};

/** Nothing seen, nothing open, no ticks counted. */
void
RS_ServerTick_Reset(struct RS_ServerTick* tick);

/**
 * A packet that may change the UI was popped, after a fence has been seen.
 *
 * Opens the tick when it is closed, recording `cycle` and whether this opening
 * packet was something other than a plain varp; on a tick already open, a
 * packet that is not a plain varp marks it as such. Returns true when this
 * packet is the one that opened the tick.
 */
bool
RS_ServerTick_NotePacket(
    struct RS_ServerTick* tick,
    bool plain_varp,
    uint64_t cycle);

/** SERVER_TICK_END executed: a fence has now been seen, the tick is closed, and
 *  one more tick is counted. */
void
RS_ServerTick_Fence(struct RS_ServerTick* tick);

/** A tick ended on a revision that sends no fence (its PLAYER_INFO): counted,
 *  and nothing else changes. */
void
RS_ServerTick_EndUnfenced(struct RS_ServerTick* tick);

/** Close the tick without a fence: the recovery for one cut short. */
void
RS_ServerTick_Close(struct RS_ServerTick* tick);

/**
 * Whether the open tick has waited `max_cycles` or more since it opened.
 *
 * False on a closed tick. The subtraction is the caller's cycle less the
 * stored, truncated one, in 64-bit unsigned arithmetic.
 */
bool
RS_ServerTick_Overdue(
    struct RS_ServerTick const* tick,
    uint64_t cycle,
    uint64_t max_cycles);

/**
 * Lift the latch for a hold: closes the tick and returns whether it was open,
 * for the caller to keep and hand back to Resume.
 */
bool
RS_ServerTick_Suspend(struct RS_ServerTick* tick);

/** Re-arm a suspended tick, counting the backstop from `cycle`. The varp-only
 *  mark is left as it was. */
void
RS_ServerTick_Resume(
    struct RS_ServerTick* tick,
    uint64_t cycle);

bool
RS_ServerTick_IsOpen(struct RS_ServerTick const* tick);

/** Whether the open tick has carried a UI packet other than a plain varp.
 *  Not gated on the tick being open; the caller asks both. */
bool
RS_ServerTick_OpenNonVar(struct RS_ServerTick const* tick);

bool
RS_ServerTick_FenceSeen(struct RS_ServerTick const* tick);

/** Server ticks executed, ever. */
unsigned
RS_ServerTick_Serial(struct RS_ServerTick const* tick);

#endif /* SRC_GAME_RS_SERVER_TICK_H */
