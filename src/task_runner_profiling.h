#ifndef TASK_RUNNER_PROFILING_H
#define TASK_RUNNER_PROFILING_H

/*
 * The task runner's measurements (task_profiling.h has the switch and what
 * each one prints). TaskRunner_Step calls these at the points it measures --
 * the start and end of a pass, the start and end of one task's step -- and
 * the App sets the thresholds through the setters. With TASK_PROFILING unset
 * every one is an empty `static inline` and the runner reads no clock for
 * any of them.
 *
 * Included by task_runner.h, after struct TaskRunner, TaskRunner_Now and
 * TaskRunner_ContractBreach, which these use; not on its own.
 */

#ifndef SRC_TASK_RUNNER_H
#error "task_runner_profiling.h is included by task_runner.h"
#endif

#include "task_profiling.h"

#include <stdint.h>

/* One pass's measurement, on TaskRunner_Step's stack (a task may step its
 * own queue again, so none of this lives on the runner). */
struct TaskProfPass
{
#if TASK_PROFILING
    /* Every step of this pass is timed: slow-task line, longest step,
     * compute check. */
    int measure;
    /* The unit meter: the background queue's sliced passes only. */
    int metered;
    /* The step in progress: its name (copied first: a task that ends is
     * freed by its run), whether it is an ordinary task, and its start on
     * the cost clock and the wall clock. */
    char name[32];
    int ordinary;
    uint64_t c0;
    uint64_t t0;
#else
    char unused;
#endif
};

/* Thresholds and clocks, set by the queue's owner. Each is the whole of
 * what it says; nothing else reads them. */
static inline void
TaskProf_SetSlowTask(
    struct TaskRunner* runner,
    uint64_t slow_us)
{
    assert(runner);
#if TASK_PROFILING
    runner->prof.slow_task_us = slow_us;
#else
    (void)runner;
    if( slow_us != 0 )
    {
        static int said;
        if( !said )
            fprintf(stderr, "TORIRS_TASK_SLOW_MS: " TASK_PROFILING_NOT_COMPILED_IN "\n");
        said = 1;
    }
#endif
}

static inline void
TaskProf_SetComputeStep(
    struct TaskRunner* runner,
    uint64_t compute_us)
{
    assert(runner);
#if TASK_PROFILING
    runner->prof.compute_step_us = compute_us;
#else
    (void)runner;
    (void)compute_us;
#endif
}

/*
 * The clock the compute check and the unit meter read a step's COST on:
 * processor time of the thread that steps the queue
 * (PlatformWindow_ThreadCpuUs on the App's queues), so a step measured on a
 * loaded machine reads what it computed, not how long it waited for a core.
 * Without one the compute check reads the wall clock and the unit meter is
 * off. The runner names no platform: the clock is handed in.
 */
static inline void
TaskProf_SetCostClock(
    struct TaskRunner* runner,
    uint64_t (*cost_now_us)(void))
{
    assert(runner);
    assert(cost_now_us);
#if TASK_PROFILING
    runner->prof.cost_now_us = cost_now_us;
#else
    (void)runner;
    (void)cost_now_us;
#endif
}

static inline void
TaskProf_SetUnitLimit(
    struct TaskRunner* runner,
    uint64_t unit_us)
{
    assert(runner);
#if TASK_PROFILING
    runner->prof.unit_limit_us = unit_us;
#else
    (void)runner;
    (void)unit_us;
#endif
}

/* The App boots: its loads are not what the compute threshold measures. */
static inline void
TaskProf_ComputeCheckOff(
    struct TaskRunner* runner,
    int off)
{
    assert(runner);
#if TASK_PROFILING
    runner->prof.compute_check_off = off;
#else
    (void)runner;
    (void)off;
#endif
}

/* The longest step and the longest unit run on this queue so far, and
 * whose; 0 and "" without the switch. */
static inline uint64_t
TaskProf_LongestStepUs(struct TaskRunner const* runner)
{
    assert(runner);
#if TASK_PROFILING
    return runner->prof.longest_step_us;
#else
    (void)runner;
    return 0;
#endif
}

static inline char const*
TaskProf_LongestStepName(struct TaskRunner const* runner)
{
    assert(runner);
#if TASK_PROFILING
    return runner->prof.longest_step_name;
#else
    (void)runner;
    return "";
#endif
}

static inline uint64_t
TaskProf_LongestUnitUs(struct TaskRunner const* runner)
{
    assert(runner);
#if TASK_PROFILING
    return runner->prof.longest_unit_us;
#else
    (void)runner;
    return 0;
#endif
}

static inline char const*
TaskProf_LongestUnitName(struct TaskRunner const* runner)
{
    assert(runner);
#if TASK_PROFILING
    return runner->prof.longest_unit_name;
#else
    (void)runner;
    return "";
#endif
}

#if TASK_PROFILING

/* TORIRS_QUEUE_CONTRACT_UNIT_MS: the background unit limit in force for a
 * runner whose own is `runner_us` (TaskRunnerProf::unit_limit_us). Unset: the
 * runner's; set: that many milliseconds for every runner, 0 turning the check
 * off. */
static inline uint64_t
TaskProf_ContractUnitUs(uint64_t runner_us)
{
    static int read;
    static int set;
    static uint64_t env_us;
    if( !read )
    {
        char const* env = getenv("TORIRS_QUEUE_CONTRACT_UNIT_MS");
        read = 1;
        set = env && env[0];
        env_us = set ? (uint64_t)(atof(env) * 1000.0) : 0;
    }
    return set ? env_us : runner_us;
}

/*
 * The step just run on a metered (background, sliced) pass: its longest
 * unit, the work between two asks of "is the slice over?", against the limit.
 * A unit is what a sliced pass can overrun its deadline by -- the guaranteed
 * turn of a pass whose slice is already over (the floor's special case: the
 * step runs whatever the clock says) runs one unit, and asks; a pass with
 * time left runs units until an ask finds it over. So a unit over the limit
 * is a task that cannot keep to a slice, however short the slice is made.
 */
static inline void
TaskProf_UnitCheck(
    struct TaskRunner* runner,
    char const* name)
{
    struct TaskRunnerProf* prof;
    uint64_t unit;
    uint64_t limit_us;
    char line[256];
    int seen = 0;

    assert(runner);
    assert(name);
    prof = &runner->prof;
    assert(runner->io->slice.prof.unit_cost_us);
    TaskProf_SliceUnitEnd(&runner->io->slice.prof);
    unit = runner->io->slice.prof.unit_longest;
    if( unit > prof->longest_unit_us )
    {
        prof->longest_unit_us = unit;
        memcpy(prof->longest_unit_name, name, sizeof(prof->longest_unit_name));
        prof->longest_unit_name[sizeof(prof->longest_unit_name) - 1] = '\0';
    }
    limit_us = TaskProf_ContractUnitUs(prof->unit_limit_us);
    if( limit_us == 0 || unit <= limit_us )
        return;
    for( int i = 0; i < prof->unit_named_count; i++ )
        if( strcmp(prof->unit_named[i], name) == 0 )
            seen = 1;
    if( seen )
        return;
    if( prof->unit_named_count < (int)(sizeof(prof->unit_named) / sizeof(prof->unit_named[0])) )
    {
        memcpy(prof->unit_named[prof->unit_named_count], name, sizeof(prof->unit_named[0]));
        prof->unit_named[prof->unit_named_count][sizeof(prof->unit_named[0]) - 1] = '\0';
        prof->unit_named_count++;
    }
    snprintf(
        line,
        sizeof(line),
        "background task %s ran %.2f ms between two slice checks (its cost clock; limit %.2f ms): it "
        "overruns any slice by that much, so its work must be cut into smaller steps",
        name[0] ? name : "(unnamed)",
        (double)unit / 1000.0,
        (double)limit_us / 1000.0);
    TaskRunner_ContractBreach(line);
}

/* A pass begins: is it measured, and is it metered? `timed`: it has a
 * deadline. */
static inline void
TaskProf_PassBegin(
    struct TaskRunner* runner,
    struct TaskProfPass* pass,
    int timed)
{
    struct TaskRunnerProf const* prof;

    assert(runner);
    assert(pass);
    prof = &runner->prof;
    pass->measure = runner->io->slice.now_us &&
                    (prof->slow_task_us || prof->compute_step_us || runner->role == TASK_RUNNER_ROLE_BACKGROUND);
    /* The unit meter: on the background queue's sliced passes only. A pass
     * with no deadline is an owner running a job to its end, which no slice
     * bounds. */
    pass->metered = runner->role == TASK_RUNNER_ROLE_BACKGROUND && timed && runner->io->slice.sliced &&
                    prof->cost_now_us && TaskProf_ContractUnitUs(prof->unit_limit_us) != 0;
}

/* `task` is about to run one step. */
static inline void
TaskProf_StepBegin(
    struct TaskRunner* runner,
    struct TaskProfPass* pass,
    struct ToriRS_Task const* task)
{
    assert(runner);
    assert(pass);
    assert(task);
    if( !pass->measure && !pass->metered )
        return;
    _Static_assert(sizeof(pass->name) == sizeof(task->name), "a step's name is the task's");
    memcpy(pass->name, task->name, sizeof(pass->name));
    if( pass->metered )
    {
        runner->io->slice.prof.unit_cost_us = runner->prof.cost_now_us;
        runner->io->slice.prof.unit_mark = runner->io->slice.prof.unit_cost_us();
        runner->io->slice.prof.unit_longest = 0;
    }
    if( pass->measure )
    {
        pass->ordinary = !task->render_blocking;
        pass->c0 = runner->prof.cost_now_us ? runner->prof.cost_now_us() : 0;
        pass->t0 = TaskRunner_Now(runner);
    }
}

/* The step begun by TaskProf_StepBegin has returned (its task may be
 * freed: only the copied name is read). */
static inline void
TaskProf_StepEnd(
    struct TaskRunner* runner,
    struct TaskProfPass* pass)
{
    struct TaskRunnerProf* prof;

    assert(runner);
    assert(pass);
    prof = &runner->prof;
    if( pass->measure )
    {
        char const* name = pass->name;
        uint64_t dt;
        uint64_t cost;

        dt = TaskRunner_Now(runner) - pass->t0;
        /* What the step computed: its thread's processor time where
         * the runner has that clock, else the wall time it took. */
        cost = prof->cost_now_us ? prof->cost_now_us() - pass->c0 : dt;
        if( prof->slow_task_us && dt >= prof->slow_task_us )
            fprintf(stderr, "task_slow: %s %.2f ms\n", name[0] ? name : "(unnamed)", (double)dt / 1000.0);
        if( dt > prof->longest_step_us )
        {
            prof->longest_step_us = dt;
            memcpy(prof->longest_step_name, name, sizeof(prof->longest_step_name));
        }
        if( runner->role == TASK_RUNNER_ROLE_PARALLEL && prof->compute_step_us && pass->ordinary &&
            !prof->compute_check_off && cost >= prof->compute_step_us )
        {
            int seen = 0;
            for( int i = 0; i < prof->compute_named_count; i++ )
                if( strcmp(prof->compute_named[i], name) == 0 )
                    seen = 1;
            if( !seen )
            {
                char line[256];
                if( prof->compute_named_count < (int)(sizeof(prof->compute_named) / sizeof(prof->compute_named[0])) )
                    memcpy(prof->compute_named[prof->compute_named_count++], name, sizeof(pass->name));
                snprintf(
                    line,
                    sizeof(line),
                    "parallel task %s computed %.2f ms in one step (%s; %.2f ms wall; threshold "
                    "%.2f ms): CPU work belongs on the background queue",
                    name[0] ? name : "(unnamed)",
                    (double)cost / 1000.0,
                    prof->cost_now_us ? "thread CPU time" : "wall time",
                    (double)dt / 1000.0,
                    (double)prof->compute_step_us / 1000.0);
                TaskRunner_ContractBreach(line);
            }
        }
    }
    if( pass->metered )
        TaskProf_UnitCheck(runner, pass->name);
}

/* The pass is over: no step of it is metered any more. */
static inline void
TaskProf_PassEnd(struct TaskRunner* runner)
{
    assert(runner);
    runner->io->slice.prof.unit_cost_us = NULL;
}

#else /* !TASK_PROFILING */

static inline void
TaskProf_PassBegin(
    struct TaskRunner* runner,
    struct TaskProfPass* pass,
    int timed)
{
    (void)runner;
    (void)pass;
    (void)timed;
}

static inline void
TaskProf_StepBegin(
    struct TaskRunner* runner,
    struct TaskProfPass* pass,
    struct ToriRS_Task const* task)
{
    (void)runner;
    (void)pass;
    (void)task;
}

static inline void
TaskProf_StepEnd(
    struct TaskRunner* runner,
    struct TaskProfPass* pass)
{
    (void)runner;
    (void)pass;
}

static inline void
TaskProf_PassEnd(struct TaskRunner* runner)
{
    (void)runner;
}

#endif /* TASK_PROFILING */

#endif /* TASK_RUNNER_PROFILING_H */
