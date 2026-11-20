#ifndef SRC_TASK_TRIGGER_H
#define SRC_TASK_TRIGGER_H

/*
 * WHAT A TASK WAITS ON, when it is not its own read.
 *
 * A task that cannot go on parks. A parked task is not stepped: its queue
 * passes over it the way it passes over a task whose read is on the wire, and
 * it costs nothing per pump. It is made ready again by exactly one thing,
 * named when it parked:
 *
 *   a TRIGGER  a small named object a task parks on (it is then one of the
 *              trigger's SUBSCRIBERS). Waiting is legal only while the trigger
 *              is PRIMED: some OWNER -- a task, a job, or an outside party the
 *              client is waiting for -- has committed to firing it. FIRE
 *              moves every subscriber to ready and never runs one inline.
 *              An owner that ENDS with a trigger still primed has it fired
 *              for it, with the outcome ABANDONED (the one place a task is
 *              freed, task_free; the one place a job ends, ToriRS_TaskJob_End),
 *              so a primed trigger always fires. Every task carries one
 *              (ToriRS_Task::ended): primed when it is queued, fired when it is
 *              freed, however it ends.
 *   a COUNT    an int a fan-out or the assets table counts on (the join a
 *              task's siblings, or its awaited loads, decrement as they end):
 *              the decrement that takes it to zero makes the task ready
 *              (ToriRS_CountDropped). Not primed: something is counted on it,
 *              and every counted thing ends.
 *   a CYCLE    a clock (struct ToriRS_Clock): "not before cycle N". Not
 *              primed either: the clock is always impending. The owner of the
 *              clock advances it in one place, and that wakes every task that
 *              is due, in due order.
 *
 * There is no condition a parked task re-tests and no timeout: a wait ends
 * because something fired it. A wait that never ends is a bug upstream -- an
 * owner that never ends -- and "tasks never hang - that is just a game
 * crash" (the owner): every parked task names what it waits on and every
 * trigger its owner, so the chain can be printed down to the read, the cycle
 * or the running owner it bottoms out in (ToriRS_TaskChainFormat), and a wait
 * that would close a loop is caught when it is made, with the chain.
 *
 * A wait is ASKED FOR by the task (the macros in asyncio.h set its
 * `park_req`, then yield) and MADE by the queue that ran it
 * (ToriRS_TaskQueue_RunTask -> ToriRS_TaskPark): a child run inline by
 * TASK_AWAITEX asks, its parent carries the request up, and the task the
 * queue sees is the one parked. That is the same route a render request and
 * a slice stop take.
 *
 * App-free, and no queue: a trigger knows its owner and its subscribers, a
 * subscriber knows its trigger, and that is all. The test is
 * game/test/task_trigger_test.c.
 */

#include <stddef.h>
#include <stdint.h>

struct ToriRS_Task;
struct ToriRS_Trigger;

/* How a trigger last ended (ToriRS_Trigger::outcome). */
enum ToriRS_TriggerOutcome
{
    TORIRS_TRIGGER_NEVER = 0,
    /* Its owner fired it: what was coming has come. */
    TORIRS_TRIGGER_FIRED,
    /* Its owner ended without firing it, or a newer prime replaced this one,
     * or its storage is going away: what was coming will not come. */
    TORIRS_TRIGGER_ABANDONED,
};

/* Why a parked task was made ready (ToriRS_Task::woke): how its last wait
 * ended. The waiter re-checks what it waits for either way. */
enum ToriRS_TaskWake
{
    TORIRS_WAKE_NONE = 0,
    TORIRS_WAKE_FIRED,
    TORIRS_WAKE_ABANDONED,
    /* The count it waited on reached zero (or dropped, for an "any" wait). */
    TORIRS_WAKE_COUNT,
    /* Its cycle came. */
    TORIRS_WAKE_CYCLE,
    /* It was cancelled while it waited (the pending index's cancel). */
    TORIRS_WAKE_CANCELLED,
};

/* What a parked task is parked on (ToriRS_ParkRequest::on). */
enum ToriRS_ParkOn
{
    TORIRS_PARK_NONE = 0,
    TORIRS_PARK_TRIGGER,
    TORIRS_PARK_COUNT,
    TORIRS_PARK_CYCLE,
};

/*
 * WHAT A PARKED TASK WAITS FOR, named for the trace (ToriRS_ParkRequest::
 * kind). Labels only -- what wakes it is `on` -- and a closed set:
 *
 *   ASSET    an asset load counted on the task's join (Assets_Await;
 *            PT_PARALLEL_AWAIT_ASSET / _ASSETS, TASK_AWAIT_ASSETS).
 *   JOIN     tasks it fanned out (ToriRS_TaskQueue_AddParallelPoolSubTask,
 *            PT_TASK_JOIN; PT_PARALLEL_AWAIT_JOIN when it can be cancelled).
 *   TURN     an earlier task for the same key ending (PT_PARALLEL_AWAIT_TURN:
 *            that task's `ended` trigger).
 *   REBUILD  the rebuild of its view (PT_PARALLEL_AWAIT_REBUILD: the view's
 *            built trigger, primed by the rebuild).
 *   CYCLE    a client cycle (PT_PARALLEL_AWAIT_CYCLE).
 *   TRIGGER  any other trigger (PT_TASK_AWAIT_TRIGGER): the trigger's name
 *            says which.
 */
enum ToriRS_TaskWait
{
    TORIRS_TASK_WAIT_NONE = 0,
    TORIRS_TASK_WAIT_ASSET,
    TORIRS_TASK_WAIT_JOIN,
    TORIRS_TASK_WAIT_TURN,
    TORIRS_TASK_WAIT_REBUILD,
    TORIRS_TASK_WAIT_CYCLE,
    TORIRS_TASK_WAIT_TRIGGER,
};

char const*
ToriRS_TaskWaitName(int wait);

/* ---- owners ------------------------------------------------------------- */

enum ToriRS_OwnerKind
{
    TORIRS_OWNER_NONE = 0,
    /* A task (ToriRS_Task::owner): it ends when it is freed. */
    TORIRS_OWNER_TASK,
    /* A job (ToriRS_TaskJob::owner): it ends at ToriRS_TaskJob_End. */
    TORIRS_OWNER_JOB,
    /* Something outside the client the client is waiting on -- the server's
     * next packet, the network -- standing as an owner with one end of its
     * own (a view that awaits its first scene ends when the view is
     * released). The bottom of a chain, like a read. */
    TORIRS_OWNER_EXTERNAL,
};

/*
 * Something with one end that commits to firing the triggers it primes. Zero
 * is "not yet an owner"; every field is the trigger layer's.
 */
struct ToriRS_TriggerOwner
{
    int kind;
    /* A job's or an outside owner's name; a task's is the task's. */
    char const* name;
    /* TORIRS_OWNER_TASK: the task. */
    struct ToriRS_Task* task;
    /* The triggers it has primed and not fired, through
     * ToriRS_Trigger::owner_next. */
    struct ToriRS_Trigger* primed;
};

/* A job or an outside party as an owner (a task is made one by
 * ToriRS_TaskOwner, asyncio.h). `name` is borrowed for the owner's life. */
void
ToriRS_TriggerOwner_Init(
    struct ToriRS_TriggerOwner* owner,
    int kind,
    char const* name);

/* The owner's name: its task's, or the one it was given. */
char const*
ToriRS_TriggerOwner_Name(struct ToriRS_TriggerOwner const* owner);

/*
 * THE OWNER ENDS. Every trigger it still has primed fires with the outcome
 * ABANDONED. `completed`: the owner ran to its end (a task that reached
 * PT_END or PT_EXIT, a job that committed) rather than being cancelled or
 * dropped -- a completed owner with a trigger still primed forgot to fire
 * it, which asserts in a debug build and prints one line naming the trigger
 * and the owner otherwise. An owner that was cancelled or dropped abandoning
 * is normal. Accepts an owner that never primed anything.
 */
void
ToriRS_TriggerOwner_End(
    struct ToriRS_TriggerOwner* owner,
    int completed);

/* ---- triggers ----------------------------------------------------------- */

struct ToriRS_Trigger
{
    /* What it is, for the chain (a literal or storage that outlives it). */
    char const* name;
    /* While primed: who will fire it. NULL: not primed. */
    struct ToriRS_TriggerOwner* owner;
    struct ToriRS_Trigger* owner_prev;
    struct ToriRS_Trigger* owner_next;
    /* The tasks parked on it, through ToriRS_Task::park_next. */
    struct ToriRS_Task* subs;
    /* How it last ended, and how many times it has. */
    int outcome;
    uint32_t ends;
};

/* An unprimed trigger named `name` (borrowed). Zeroed storage is an unprimed
 * trigger too; this only names it. */
void
ToriRS_Trigger_Init(
    struct ToriRS_Trigger* trigger,
    char const* name);

/*
 * PRIME: `owner` commits to firing `trigger`. Both required. A trigger primed
 * by a DIFFERENT owner is abandoned first (its subscribers wake ABANDONED,
 * find it primed again, and wait again: a second rebuild started while the
 * first was pending). The same owner priming it twice is a bug and aborts.
 */
void
ToriRS_Trigger_Prime(
    struct ToriRS_Trigger* trigger,
    struct ToriRS_TriggerOwner* owner);

/* Is it primed: is something coming that will fire it? */
int
ToriRS_Trigger_Primed(struct ToriRS_Trigger const* trigger);

/*
 * FIRE: requires primed. Records FIRED, unprimes, and moves every subscriber
 * to ready (ToriRS_TaskWakes counts each). Never runs a subscriber: its own
 * queue runs it at that queue's next pass.
 */
void
ToriRS_Trigger_Fire(struct ToriRS_Trigger* trigger);

/* Fire it if it is primed, whoever primed it: for a trigger that mirrors a
 * state (the boot's "built" mirrors the client phase). */
void
ToriRS_Trigger_FireIfPrimed(struct ToriRS_Trigger* trigger);

/* How its last end went (enum ToriRS_TriggerOutcome). */
int
ToriRS_Trigger_Outcome(struct ToriRS_Trigger const* trigger);

/*
 * Its storage is going away: if primed, abandon it (its subscribers wake
 * ABANDONED). The deallocator's half: accepts a trigger never primed. The
 * trigger is always storage embedded in its owner, so it is never NULL.
 */
void
ToriRS_Trigger_Retire(struct ToriRS_Trigger* trigger);

/* How many tasks are parked on it. Diagnostics and tests. */
int
ToriRS_Trigger_Subscribers(struct ToriRS_Trigger const* trigger);

/* ---- clocks ------------------------------------------------------------- */

/*
 * A clock tasks park on until a due cycle: a list ordered by due cycle, woken
 * by its owner advancing it in one place. `now` is the last cycle it was
 * advanced to. Zeroed storage is a clock at cycle 0 named nothing.
 */
struct ToriRS_Clock
{
    char const* name;
    uint32_t now;
    /* Parked tasks, earliest due first, through ToriRS_Task::park_next. */
    struct ToriRS_Task* head;
};

void
ToriRS_Clock_Init(
    struct ToriRS_Clock* clock,
    char const* name,
    uint32_t now);

/* Has `now` reached `due`? A signed difference, so it holds across the wrap
 * of a 32-bit cycle count. */
static inline int
ToriRS_CycleReached(
    uint32_t now,
    uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

/* The clock reads `now`: every task due by then is made ready, earliest due
 * first. The one call that moves a clock. */
void
ToriRS_Clock_Advance(
    struct ToriRS_Clock* clock,
    uint32_t now);

/* The clock's storage is going away: every task parked on it is made ready
 * (it re-checks, and finds the clock gone, or its cycle come). */
void
ToriRS_Clock_Retire(struct ToriRS_Clock* clock);

/* How many tasks are parked on it. */
int
ToriRS_Clock_Parked(struct ToriRS_Clock const* clock);

/* ---- counts ------------------------------------------------------------- */

/*
 * `*count` (a join) was just decremented, or zeroed: a task parked on it
 * until zero is made ready if it now is, and one parked on any drop is made
 * ready either way. The one call every place that lowers a join makes (the
 * queue's end of a sibling, the assets table's end of a load, a detach).
 */
void
ToriRS_CountDropped(int* count);

/* How many tasks are parked on counts. Diagnostics and tests. */
int
ToriRS_CountParked(void);

/* ---- parking (the queue's half; the macros are in asyncio.h) ------------- */

/* One wait: what a task asks to be parked on (ToriRS_Task::park_req, for the
 * yield that just happened) and, once parked, what it is parked on
 * (ToriRS_Task::park). */
struct ToriRS_ParkRequest
{
    /* enum ToriRS_ParkOn; NONE: no wait asked for. */
    int on;
    /* enum ToriRS_TaskWait: what it is for, in the trace. */
    int kind;
    struct ToriRS_Trigger* trigger;
    int* count;
    /* A count wait made ready by any drop, not only by zero (a boot bar
     * that moves as each load lands). */
    int any;
    struct ToriRS_Clock* clock;
    uint32_t due;
};

/* Park `task` on what its `park_req` names (ToriRS_TaskQueue_RunTask, after a
 * run that asked). A trigger must be primed (or this aborts, naming the
 * trigger and the task), and the wait must not close a loop (or this aborts
 * with the chain). */
void
ToriRS_TaskPark(struct ToriRS_Task* task);

/* Make a parked `task` ready: unlink it from what it waits on and record
 * why. */
void
ToriRS_TaskWake(
    struct ToriRS_Task* task,
    int why);

/* A parked task is being freed: unlink it, waking nothing. */
void
ToriRS_TaskUnparkForFree(struct ToriRS_Task* task);

/* How many tasks have been made ready since the process began. A runner's
 * owner compares it across a pump to know a wake left ready work behind
 * (App_AsyncRunnable): a fire never runs its subscribers, so something must
 * say they are ready. */
uint32_t
ToriRS_TaskWakes(void);

/*
 * The chain below a parked task: what it is parked on, that trigger's owner,
 * what THAT is parked on, and so on, down to what it bottoms out in -- a
 * read, a count, a cycle, a running owner, a job, an outside owner.
 * Writes one line into `out`; returns how it bottoms out (enum
 * ToriRS_ChainEnd).
 */
enum ToriRS_ChainEnd
{
    /* Not parked. */
    TORIRS_CHAIN_READY = 0,
    TORIRS_CHAIN_READ,
    TORIRS_CHAIN_COUNT,
    TORIRS_CHAIN_CYCLE,
    /* A task owner that is neither parked nor reading: it is running, or
     * ready to. */
    TORIRS_CHAIN_RUNNING,
    TORIRS_CHAIN_JOB,
    TORIRS_CHAIN_EXTERNAL,
    /* Cannot happen by construction; the strict audit aborts on it. */
    TORIRS_CHAIN_UNSOUND,
};

int
ToriRS_TaskChainFormat(
    struct ToriRS_Task const* task,
    char* out,
    size_t out_size);

/* Print a fatal line and abort, in every build: a wait that could only hang
 * (an unprimed trigger, a loop) is a crash, not a state to carry on in. */
void
ToriRS_TaskFatal(char const* line);

#endif /* SRC_TASK_TRIGGER_H */
