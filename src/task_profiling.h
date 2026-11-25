#ifndef TASK_PROFILING_H
#define TASK_PROFILING_H

/*
 * TASK PROFILING -- what the task queues measure about the tasks they run,
 * behind ONE compile-time switch.
 *
 * WHAT IT IS. Four diagnostics, all of them measurement and none of them a
 * decision (task_runner_profiling.h has the code):
 *   - TORIRS_TASK_SLOW_MS=<n>: `task_slow: <task> <ms>` for every task on
 *     any queue whose single run took at least n ms.
 *   - the parallel queue's compute check: `queue_contract: parallel task <t>
 *     computed <ms> in one step ...` once per task name when one step costs
 *     more than the lane's threshold (4 ms; TORIRS_QUEUE_CONTRACT_COMPUTE_MS),
 *     aborting under TORIRS_QUEUE_CONTRACT_STRICT=1.
 *   - the background queue's longest step: what
 *     TORIRS_QUEUE_CONTRACT_TRACE prints as `trace background longest step`
 *     and the contract trace line reports, and what the load-split tests
 *     bound.
 *   - the background queue's unit meter: the longest work between two asks
 *     of "is the slice over?" (`trace background longest unit`), and
 *     `queue_contract: background task <t> ran <ms> between two slice
 *     checks` over the unit limit (TORIRS_QUEUE_CONTRACT_UNIT_MS).
 *
 * WHAT IT REPLACES. TaskRunner_Step read the clock around every task it ran
 * on the parallel and the background queue -- the wall clock twice and the
 * cost clock twice -- in every build, because the compute check's threshold
 * is set on every lane and the background queue was always measured. In the
 * browser each read is a call out to performance.now(): 0.27 ms of an
 * 8.3 ms bank-open frame on the NUC (Chrome sampler, 2026-11-25), 3% of it.
 *
 * NOT HERE. A clock read a decision rests on: a pass's deadline
 * (TaskRunner_StepUntil, TaskRunner_SettleFrameUntil), the slice's "is it
 * over?" (ToriRS_SliceSpent), the paced catch-up. Those run in every build.
 *
 * THE SWITCH. `TASK_PROFILING` (make: `TASK_PROFILING=1`, which adds
 * -DTASK_PROFILING=1 to every unit, its own object-directory suffix
 * `_taskprof`). Unset -- the default -- every function here and in
 * task_runner_profiling.h is an empty `static inline`: no clock, no test, no
 * load, no line. Set, the four behave exactly as they did before the switch.
 * A build without it says so once on stderr when TORIRS_TASK_SLOW_MS is set,
 * and the contract trace says "not compiled in" where the longest step was.
 *
 * The state below has one layout whichever way the switch is set, so an
 * object built with it links with one built without: a load-split test
 * measures its own runners (-DTASK_PROFILING=1 on its own unit) against the
 * client's objects.
 *
 * docs/PERF_HARNESS.md "Build knobs" documents the switch.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TASK_PROFILING
#define TASK_PROFILING 0
#endif

#define TASK_PROFILING_NOT_COMPILED_IN "not compiled in (build with TASK_PROFILING=1)"

/* A sliced pass's unit meter (ToriRS_Slice::prof): each ask of "is the slice
 * over?" closes a UNIT, read on `unit_cost_us` (the stepping thread's
 * processor clock; NULL: not metered). The runner sets the mark when it
 * starts a task's step and reads `unit_longest` when the step returns. */
struct TaskSliceProf
{
    uint64_t (*unit_cost_us)(void);
    uint64_t unit_mark;
    uint64_t unit_longest;
};

/* A runner's measurements (TaskRunner::prof). */
struct TaskRunnerProf
{
    /* TORIRS_TASK_SLOW_MS: name every task whose single run took at least
     * this long (0: off). Needs the queue's clock (TaskRunner_SetClock). */
    uint64_t slow_task_us;
    /** A parallel task whose one step COST at least this much processor
     *  time is computing, not waiting: named once per name per session (0:
     *  off). Read on `cost_now_us` when the runner has one (the stepping
     *  thread's CPU clock, TaskProf_SetCostClock), else on the queue's
     *  wall clock; the breach line gives both. */
    uint64_t compute_step_us;
    /** The clock a step's cost is read on (NULL: the wall clock). */
    uint64_t (*cost_now_us)(void);
    /** Set while the App boots: the boot's loads are not what that
     *  threshold measures. */
    int compute_check_off;
    /** The longest single step run on this queue so far, and whose: on the
     *  background queue the unit by which a slice can be overrun. */
    uint64_t longest_step_us;
    char longest_step_name[32];
    char compute_named[16][32];
    int compute_named_count;
    /** The background queue's unit limit: the most processor time a task
     *  may spend between two asks of "is the slice over?"
     *  (TASK_YIELD_IF_SPENT), which is the most a sliced pass can overrun
     *  its deadline by. A unit over it is named once per name, and aborts
     *  under TORIRS_QUEUE_CONTRACT_STRICT, as the parallel compute check
     *  does. Read on `cost_now_us` (TaskProf_SetCostClock: a runner
     *  without one is not metered), on sliced passes only; 0: off.
     *  TORIRS_QUEUE_CONTRACT_UNIT_MS overrides it (0: off). */
    uint64_t unit_limit_us;
    /** The longest unit run on this queue so far, and whose. */
    uint64_t longest_unit_us;
    char longest_unit_name[32];
    char unit_named[16][32];
    int unit_named_count;
};

#if TASK_PROFILING

/** Close the unit in progress on a metered slice: the work since the last
 *  ask (or since the step began) is one unit. */
static inline void
TaskProf_SliceUnitEnd(struct TaskSliceProf* prof)
{
    uint64_t now;

    assert(prof != NULL);
    assert(prof->unit_cost_us != NULL);
    now = prof->unit_cost_us();
    if( now - prof->unit_mark > prof->unit_longest )
        prof->unit_longest = now - prof->unit_mark;
    prof->unit_mark = now;
}

/** A task asked "is the slice over?" on a sliced pass: a unit ends here. */
static inline void
TaskProf_SliceAsk(struct TaskSliceProf* prof)
{
    assert(prof != NULL);
    if( prof->unit_cost_us )
        TaskProf_SliceUnitEnd(prof);
}

#else /* !TASK_PROFILING */

static inline void
TaskProf_SliceAsk(struct TaskSliceProf* prof)
{
    (void)prof;
}

#endif /* TASK_PROFILING */

#endif /* TASK_PROFILING_H */
