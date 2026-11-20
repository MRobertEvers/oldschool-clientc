/*
 * The frame contract's queue half (app.h, beside the three runners), held by
 * the runner itself (task_runner.h) and checked here with no App.
 *
 * Pins:
 *
 *   1. RESULTS BY QUEUE. A task that asks for a frame (RENDER) on the main
 *      queue ends that queue's pass with its request carried and the task
 *      behind it not run, and the frame stays owed until the owner says one
 *      was shown; on the parallel or the background queue the same request
 *      trips the runner's assert. SPENT anywhere but the background queue
 *      trips it too.
 *   1b. ONLY THE BACKGROUND QUEUE IS SLICED. A task that asks whether the
 *      slice is over is told no on the main queue, even inside a paced pass
 *      whose deadline is long gone (a blocking REBUILD's inline gather), and
 *      runs on; the paced pass itself stops between tasks and says PACED,
 *      never SPENT. Only the background queue's task stops. A deadline
 *      handed to the parallel queue trips the check.
 *   2. THE PARALLEL PUMP DRAINS. One pump (TaskRunner_PumpToStandstill)
 *      never returns while a task that is neither done nor parked (on its
 *      read, a trigger, a count, a cycle) remains -- however many plain
 *      yields that takes -- and ends with exactly the tasks that wait. A
 *      pump ended before that is caught (TaskRunner_PumpEnd asserts).
 *   3. NO TASK IS LEFT WITHOUT ITS TURN. A background pump that leaves a
 *      task that could run unstepped is caught.
 *   5. THE WAITS. The set of things a task on the parallel queue may wait on
 *      is closed (task_trigger.h has the kinds; game/rs_parallel_task.h the
 *      macros): an asset, many assets, a join on tasks it fanned out, its
 *      turn behind an earlier task for the same key (that task's end), the
 *      rebuild of its view (the view's built trigger), a client cycle (a
 *      clock). Each PARKS the task with its kind recorded (a pump ends on it
 *      without a breach) and the task is not stepped again until what it
 *      parked on wakes it -- the count reaching 0, the trigger firing or
 *      being abandoned, the clock reaching its cycle -- and a cancel wakes it
 *      at once. There is no free-form state wait any more. The trigger
 *      layer's own pins (prime, fire, abandon, loops, cancel) are
 *      game/test/task_trigger_test.c.
 *   4. WHO STEPS THE BACKGROUND QUEUE. A background pass entered from inside
 *      the script settle, or from inside a task of another queue, trips the
 *      check; the job owner's finish (owner = 1) from the packet pump does
 *      not, and neither does a pass from the frame body.
 *   5. THE BETWEEN-FRAMES PUMP. Across a pump made between frames (the
 *      browser's torirs_web_io_pump), a pass of the background queue trips
 *      the check, and so does a pass of the main queue once the boot is
 *      over; the boot's own main-queue passes do not. The passes are the
 *      runners' own counters, read before and after a real pass.
 *
 * The death checks run in a forked child and pass when it dies by SIGABRT
 * (the assert), which is how the assert is proved to fire without changing
 * the code under test. Built without NDEBUG (the Makefile target), so the
 * asserts are live.
 *
 *   make -C src test-queue-contract
 */
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"
#include "task_runner.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* platform_x_io.h keeps this opaque. A read whose path is "never" stays on
 * the wire for good; every other read is answered inside Process. */
struct PlatformX_IO
{
    int process_calls;
};

void
PlatformX_IO_Pump(struct PlatformX_IO* px)
{
    (void)px;
}

int
PlatformX_IO_Process(struct PlatformX_IO* px, struct ToriRS_IOBatch* io)
{
    px->process_calls++;
    for( int i = 0; i < io->active_count; i++ )
        if( io->active[i]->kind == TORIRS_IOK_FILE_READ && strcmp(io->active[i]->u.file.path, "never") == 0 )
            io->active[i]->pending = 1;
    ToriRS_IOBatch_Reset(io);
    return 0;
}

struct FakeClock
{
    uint64_t now_us;
};

static uint64_t
fake_now(void* ud)
{
    return ((struct FakeClock*)ud)->now_us;
}

static void
task_free_libc(struct ToriRS_Task* task)
{
    free(task);
}

/* --- tasks ------------------------------------------------------------------ */

/* Yields plainly `yields` times, then ends: work a pump must not stop on. */
struct Yielder
{
    struct ToriRS_Task task;
    struct pt pt;
    int yields;
    int left;
    int* ended;
};

static int
Yielder_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Yielder* self = (struct Yielder*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    for( self->left = self->yields; self->left > 0; self->left-- )
        PT_YIELD(&self->pt);
    (*self->ended)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_yielder_vtable = { .run = Yielder_Run, .free = task_free_libc };

static struct ToriRS_Task*
new_yielder(int yields, int* ended)
{
    struct Yielder* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_yielder_vtable;
    strcpy(t->task.name, "yielder");
    t->yields = yields;
    t->ended = ended;
    PT_INIT(&t->pt);
    return &t->task;
}

/* Asks for a read that is never answered: waits on IO for good. */
struct Stuck
{
    struct ToriRS_Task task;
    struct pt pt;
};

static int
Stuck_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Stuck* self = (struct Stuck*)base;
    PT_BEGIN(&self->pt);
    ToriRS_IO_QueueFileRead(io, 0, "never");
    PT_YIELD(&self->pt);
    PT_END(&self->pt);
}

static void
stuck_free(struct ToriRS_Task* task)
{
    /* Freed at queue teardown with its read still out. */
    task->io.pending = 0;
    free(task);
}

static struct ToriRS_TaskVTable k_stuck_vtable = { .run = Stuck_Run, .free = stuck_free };

static struct ToriRS_Task*
new_stuck(void)
{
    struct Stuck* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_stuck_vtable;
    strcpy(t->task.name, "stuck");
    PT_INIT(&t->pt);
    return &t->task;
}

/* Fans a stuck loader out and joins it: blocked for good, as a join. */
struct Joiner
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_TaskQueue* queue;
    int pending;
};

static int
Joiner_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Joiner* self = (struct Joiner*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    ToriRS_TaskQueue_AddParallelPoolSubTask(self->queue, new_stuck(), &self->pending);
    PT_TASK_JOIN(pending);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_joiner_vtable = { .run = Joiner_Run, .free = task_free_libc };

static struct ToriRS_Task*
new_joiner(struct ToriRS_TaskQueue* queue)
{
    struct Joiner* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_joiner_vtable;
    strcpy(t->task.name, "joiner");
    t->queue = queue;
    PT_INIT(&t->pt);
    return &t->task;
}

/* Asks for a frame once, then ends. */
struct Asker
{
    struct ToriRS_Task task;
    struct pt pt;
};

static int
Asker_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Asker* self = (struct Asker*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    PT_TASK_YIELD_TO_RENDER(TORIRS_RENDER_BOOT_BAR, 37, "asking");
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_asker_vtable = { .run = Asker_Run, .free = task_free_libc };

static struct ToriRS_Task*
new_asker(void)
{
    struct Asker* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_asker_vtable;
    strcpy(t->task.name, "asker");
    PT_INIT(&t->pt);
    return &t->task;
}

/* Stops itself for a slice: only legal where there is one. */
struct Slicer
{
    struct ToriRS_Task task;
    struct pt pt;
    struct FakeClock* clock;
};

static int
Slicer_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Slicer* self = (struct Slicer*)base;
    PT_BEGIN(&self->pt);
    for( ;; )
    {
        self->clock->now_us += 1000;
        PT_TASK_YIELD_IF_SPENT();
    }
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_slicer_vtable = { .run = Slicer_Run, .free = task_free_libc };

/* `units` units of 1 ms, asking after each whether the slice is over, then
 * ends: what a gather does between its stages. */
struct Units
{
    struct ToriRS_Task task;
    struct pt pt;
    struct FakeClock* clock;
    int units;
    int done;
    int* ended;
};

static int
Units_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Units* self = (struct Units*)base;
    PT_BEGIN(&self->pt);
    while( self->done < self->units )
    {
        self->clock->now_us += 1000;
        self->done++;
        PT_TASK_YIELD_IF_SPENT();
    }
    (*self->ended)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_units_vtable = { .run = Units_Run, .free = task_free_libc };

static struct Units*
new_units(struct FakeClock* clock, int units, int* ended)
{
    struct Units* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_units_vtable;
    strcpy(t->task.name, "units");
    t->clock = clock;
    t->units = units;
    t->ended = ended;
    PT_INIT(&t->pt);
    return t;
}

static struct ToriRS_Task*
new_slicer(struct FakeClock* clock)
{
    struct Slicer* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_slicer_vtable;
    strcpy(t->task.name, "slicer");
    t->clock = clock;
    PT_INIT(&t->pt);
    return &t->task;
}

/* --- harness ------------------------------------------------------------------ */

static void
runner_init(
    struct TaskRunner* runner,
    struct PlatformX_IO* px,
    struct FakeClock* clock,
    int role)
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->role = role;
    runner->parallel = role != TASK_RUNNER_ROLE_MAIN;
    if( clock )
        TaskRunner_SetClock(runner, fake_now, clock);
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

/* Run `body` in a child; did it die by SIGABRT (an assert, or the strict
 * abort)? The child's stderr is kept: its `queue_contract:` line is the
 * evidence, and a reader of the test log sees which check fired. */
static int
dies_by_abort(void (*body)(void))
{
    pid_t pid;
    int status = 0;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    assert(pid >= 0);
    if( pid == 0 )
    {
        body();
        _exit(0);
    }
    (void)waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

/* --- 1. results by queue -------------------------------------------------------- */

static void
test_render_ends_the_main_queues_pass(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int ended = 0;

    printf("TEST: a task asking for a frame on the main queue ends that queue's pass\n");
    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_MAIN);
    ToriRS_TaskQueue_Add(runner.queue, new_asker());
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(0, &ended));
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_RENDER);
    CHECK(runner.render.percent == 37);
    CHECK(strcmp(runner.render.caption, "asking") == 0);
    /* The frame is shown before anything behind the asker runs. */
    CHECK(ended == 0);
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    CHECK(ended == 1);
    runner_free(&runner);
}

/* The request is owed until the owner says a frame was shown: what keeps the
 * browser's between-frames pump (App_PumpAsync, from_frame 0) from resuming
 * a boot that asked to be seen first. */
static void
test_render_is_owed_until_a_frame_is_shown(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int ended = 0;

    printf("TEST: a frame asked for is owed until the owner says one was shown\n");
    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_MAIN);
    ToriRS_TaskQueue_Add(runner.queue, new_asker());
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(1, &ended));
    CHECK(!TaskRunner_RenderOwed(&runner));
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_RENDER);
    CHECK(TaskRunner_RenderOwed(&runner));
    /* Passes that ask for nothing do not pay it; only the frame does. */
    CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PROGRESSED);
    CHECK(TaskRunner_RenderOwed(&runner));
    TaskRunner_RenderShown(&runner);
    CHECK(!TaskRunner_RenderOwed(&runner));
    while( TaskRunner_Step(&runner) != TASK_RUNNER_IDLE )
        ;
    CHECK(ended == 1);
    CHECK(!TaskRunner_RenderOwed(&runner));
    runner_free(&runner);
}

static void
render_on_parallel(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int steps = 0;

    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TaskQueue_Add(runner.queue, new_asker());
    (void)TaskRunner_PumpToStandstill(&runner, 100, &steps);
}

static void
render_on_background(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };

    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_BACKGROUND);
    ToriRS_TaskQueue_Add(runner.queue, new_asker());
    (void)TaskRunner_RunUntil(&runner, clock.now_us + 2000);
}

static void
spent_on_parallel(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };

    /* A parallel queue given a deadline is itself the breach the contract
     * forbids (TaskRunner_StepUntil), before any task could ask it. */
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TaskQueue_Add(runner.queue, new_slicer(&clock));
    (void)TaskRunner_StepUntil(&runner, clock.now_us + 1);
}

/* A task that stops itself for a slice on the main queue, by hand (what the
 * macro can no longer do there): the result check still catches it. */
struct HandSpent
{
    struct ToriRS_Task task;
    struct pt pt;
};

static int
HandSpent_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct HandSpent* self = (struct HandSpent*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    self->task.spent = 1;
    PT_YIELD(&self->pt);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_hand_spent_vtable = { .run = HandSpent_Run, .free = task_free_libc };

static void
spent_on_main(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct HandSpent* t = calloc(1, sizeof(*t));

    assert(t);
    t->task.vtable = &k_hand_spent_vtable;
    strcpy(t->task.name, "hand_spent");
    PT_INIT(&t->pt);
    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_MAIN);
    ToriRS_TaskQueue_Add(runner.queue, &t->task);
    (void)TaskRunner_Step(&runner);
}

static void
test_render_and_spent_elsewhere_trip_the_assert(void)
{
    printf("TEST: RENDER on the parallel or background queue, SPENT on the main queue, a deadline on the "
           "parallel queue, assert\n");
    CHECK(dies_by_abort(render_on_parallel));
    CHECK(dies_by_abort(render_on_background));
    CHECK(dies_by_abort(spent_on_parallel));
    CHECK(dies_by_abort(spent_on_main));
}

/* --- 1b. only the background queue is sliced ------------------------------------ */

static void
test_only_the_background_queue_is_sliced(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct ToriRS_Slice slice = { 0 };
    struct Units* units;
    int ended = 0;
    int steps = 0;

    printf("TEST: a slice check answers \"not over\" on every queue but the background queue\n");

    /* The check itself: a deadline long past, asked off a sliced queue. */
    slice.now_us = fake_now;
    slice.now_ud = &clock;
    clock.now_us = 10000;
    slice.deadline_us = 1;
    CHECK(ToriRS_SliceSpent(&slice) == 0);
    slice.sliced = 1;
    CHECK(ToriRS_SliceSpent(&slice) == 1);

    /* The main queue in a paced pass whose deadline is already gone: the
     * head (a gather asking between its stages) runs all six units and
     * ends in one step; it never returns SPENT. */
    clock.now_us = 0;
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_MAIN);
    units = new_units(&clock, 6, &ended);
    ToriRS_TaskQueue_Add(runner.queue, &units->task);
    CHECK(TaskRunner_StepUntil(&runner, clock.now_us + 1) == TASK_RUNNER_IDLE);
    CHECK(ended == 1);
    CHECK(clock.now_us == 6000);
    /* The pace still stops the pass BETWEEN tasks, and calls it PACED: two
     * tasks of three units, an expired deadline, one runs. */
    ended = 0;
    ToriRS_TaskQueue_Add(runner.queue, &new_units(&clock, 3, &ended)->task);
    ToriRS_TaskQueue_Add(runner.queue, &new_units(&clock, 3, &ended)->task);
    runner.queue->head->render_blocking = 1;
    runner.queue->head->next->render_blocking = 1;
    CHECK(TaskRunner_SettleFrameUntil(&runner, clock.now_us + 1) == TASK_RUNNER_PACED);
    CHECK(ended == 1);
    CHECK(TaskRunner_SettleFrameUntil(&runner, 0) == TASK_RUNNER_IDLE);
    CHECK(ended == 2);
    runner_free(&runner);

    /* The parallel queue: drained, no deadline, and the same task runs to
     * its end in one pump. */
    clock.now_us = 0;
    ended = 0;
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TaskQueue_Add(runner.queue, &new_units(&clock, 6, &ended)->task);
    CHECK(TaskRunner_PumpToStandstill(&runner, 100, &steps) == TASK_RUNNER_IDLE);
    CHECK(ended == 1);
    CHECK(steps == 1);
    runner_free(&runner);

    /* The background queue: the same task, a 2.5 ms slice. Its turn's first
     * ask is the grace; it stops at the third unit, SPENT, and is resumed by
     * the next slice, not before. */
    clock.now_us = 0;
    ended = 0;
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_BACKGROUND);
    units = new_units(&clock, 6, &ended);
    ToriRS_TaskQueue_Add(runner.queue, &units->task);
    CHECK(TaskRunner_RunUntil(&runner, clock.now_us + 2500) == TASK_RUNNER_SPENT);
    CHECK(ended == 0);
    CHECK(units->done == 3);
    while( runner.queue->head )
        (void)TaskRunner_RunUntil(&runner, clock.now_us + 2500);
    CHECK(ended == 1);
    runner_free(&runner);
}

/* --- 2. the parallel pump drains ------------------------------------------------ */

static void
test_one_pump_drains_the_parallel_queue(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int ended = 0;
    int steps = 0;
    int left = 0;
    enum TaskRunnerStat stat;

    printf("TEST: one pump drains the parallel queue to a standstill\n");
    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_PARALLEL);
    /* A stuck read at the head must not stop the tasks behind it; a join on
     * it blocks for good; the yielders need 1, 7 and 40 more passes. */
    ToriRS_TaskQueue_Add(runner.queue, new_stuck());
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(1, &ended));
    ToriRS_TaskQueue_Add(runner.queue, new_joiner(runner.queue));
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(7, &ended));
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(40, &ended));
    stat = TaskRunner_PumpToStandstill(&runner, 1000, &steps);
    CHECK(stat == TASK_RUNNER_WAITING);
    CHECK(ended == 3);
    CHECK(runner.progressed == 1);
    /* What is left is exactly what waits: two stuck reads and a join. */
    for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
    {
        left++;
        CHECK(t->io.pending || t->parked);
    }
    CHECK(left == 3);
    CHECK(!runner.in_pump);
    /* The next pump finds nothing to do and says so without running. */
    stat = TaskRunner_PumpToStandstill(&runner, 1000, &steps);
    CHECK(stat == TASK_RUNNER_WAITING);
    CHECK(steps == 1);
    CHECK(runner.progressed == 0);
    runner_free(&runner);
}

static void
parallel_pump_ended_early(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int ended = 0;

    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(5, &ended));
    TaskRunner_PumpBegin(&runner);
    (void)TaskRunner_Step(&runner);
    /* A yielder that can still run: this pump did not drain. */
    (void)TaskRunner_PumpEnd(&runner, "ended early on purpose");
}

static void
test_a_pump_ended_early_is_caught(void)
{
    printf("TEST: a parallel pump that returns with a runnable task trips the check\n");
    CHECK(dies_by_abort(parallel_pump_ended_early));
}

/* --- 3. no task without its turn ------------------------------------------------ */

static void
background_task_left_unstepped(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    int ended = 0;

    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_BACKGROUND);
    TaskRunner_PumpBegin(&runner);
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(5, &ended));
    /* No pass ran: the yielder never had its turn. */
    (void)TaskRunner_PumpEnd(&runner, "no pass on purpose");
}

static void
test_a_task_without_its_turn_is_caught(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    int ended = 0;

    printf("TEST: a background pump leaves no runnable task without its turn\n");
    /* The real pump with an expired slice: every task stepped, no breach. */
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_BACKGROUND);
    for( int i = 0; i < 4; i++ )
        ToriRS_TaskQueue_Add(runner.queue, new_slicer(&clock));
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(3, &ended));
    TaskRunner_PumpBegin(&runner);
    (void)TaskRunner_StepUntil(&runner, clock.now_us + 1);
    CHECK(TaskRunner_PumpEnd(&runner, "test") == 0);
    for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
        CHECK(t->turn == runner.pump);
    runner_free(&runner);
    CHECK(dies_by_abort(background_task_left_unstepped));
}

/* --- 4. who steps the background queue ------------------------------------------ */

static struct TaskRunner g_main;
static struct TaskRunner g_parallel;
static struct TaskRunner g_background;

static void
background_from_the_settle(void)
{
    TaskRunner_CheckBackgroundEntry(&g_main, &g_parallel, &g_background, "the script settle", 0);
}

static void
background_from_a_parallel_task(void)
{
    g_parallel.stepping = 1;
    TaskRunner_CheckBackgroundEntry(&g_main, &g_parallel, &g_background, NULL, 0);
}

static void
owner_from_a_parallel_task(void)
{
    g_parallel.stepping = 1;
    TaskRunner_CheckBackgroundEntry(&g_main, &g_parallel, &g_background, "the packet pump", 1);
}

static void
test_who_steps_the_background_queue(void)
{
    printf("TEST: the background queue is never stepped from a settle or a task of another queue\n");
    memset(&g_main, 0, sizeof(g_main));
    memset(&g_parallel, 0, sizeof(g_parallel));
    memset(&g_background, 0, sizeof(g_background));
    /* The frame body, and the owner finishing its job from the packet pump:
     * both allowed. */
    TaskRunner_CheckBackgroundEntry(&g_main, &g_parallel, &g_background, NULL, 0);
    TaskRunner_CheckBackgroundEntry(&g_main, &g_parallel, &g_background, "the packet pump", 1);
    CHECK(dies_by_abort(background_from_the_settle));
    CHECK(dies_by_abort(background_from_a_parallel_task));
    CHECK(dies_by_abort(owner_from_a_parallel_task));
}

/* --- 5. the waits --------------------------------------------------------------- */

enum
{
    KIND_WAITER = RS_PENDING_KINDS_TEST_FIRST,
};

enum WaitMode
{
    W_ASSET = 0,
    W_ASSETS,
    W_JOIN,
    W_TURN,
    W_REBUILD,
    W_CYCLE,
    W_MODES,
};

static char const* const k_mode_names[W_MODES] = { "asset", "assets", "join", "turn", "rebuild", "cycle" };
static int const k_mode_kinds[W_MODES] = {
    TORIRS_TASK_WAIT_ASSET, TORIRS_TASK_WAIT_ASSET,   TORIRS_TASK_WAIT_JOIN,
    TORIRS_TASK_WAIT_TURN,  TORIRS_TASK_WAIT_REBUILD, TORIRS_TASK_WAIT_CYCLE,
};

/* The world the waits look at: an assets table with a loader that answers
 * only when told, a pending index, a client clock, a view whose built
 * trigger a rebuild (an outside owner here) primes. */
struct NamedFx
{
    struct TaskRunner runner;
    struct PlatformX_IO px;
    struct Assets* assets;
    struct RS_Pending* index;
    struct ToriRS_Clock clock;
    int built;
    struct ToriRS_Trigger view_built;
    struct ToriRS_TriggerOwner rebuild;
    struct ToriRS_TriggerOwner rebuild2;
    /* What the earlier task for the waiter's key waits on before it ends. */
    struct ToriRS_Trigger gate;
    struct ToriRS_TriggerOwner gate_owner;
};

enum
{
    OUT_WAITING = 0,
    OUT_RESUMED,
    OUT_CANCELLED,
};

struct Waiter
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct NamedFx* fx;
    int mode;
    int join;
    uint32_t due;
    int* outcome;
    int runs;
};

/* A sibling a JOIN waits for: ends at cycle `due` (a cycle wait itself). */
struct CycleSibling
{
    struct ToriRS_Task task;
    struct pt pt;
    struct NamedFx* fx;
    uint32_t due;
};

static enum AssetStart
named_fake_start(void* data, struct Assets* assets, struct AssetKey key)
{
    (void)data;
    (void)assets;
    (void)key;
    return ASSET_START_LOADING;
}

static bool
named_fake_resident(void* data, struct AssetKey key)
{
    (void)data;
    (void)key;
    return false;
}

static struct RS_PendingKey
waiter_key(void)
{
    return RS_PendingKey_View(0, 3, 4, 0, 1, 1);
}

static int
CycleSibling_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct CycleSibling* self = (struct CycleSibling*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    TASK_AWAIT_CYCLE(&self->task, &self->pt, &self->fx->clock, self->due);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_cycle_sibling_vtable = { .run = CycleSibling_Run, .free = task_free_libc };

static int
Waiter_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Waiter* self = (struct Waiter*)base;
    struct NamedFx* fx = self->fx;
    (void)io;

    self->runs++;
    PT_BEGIN(&self->pt);
    if( self->mode == W_ASSET )
        PT_PARALLEL_AWAIT_ASSET(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 7, 0));
    else if( self->mode == W_ASSETS )
    {
        (void)Assets_Await(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 8, 0), &self->par.join);
        (void)Assets_Await(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 9, 0), &self->par.join);
        PT_PARALLEL_AWAIT_ASSETS(fx->assets);
    }
    else if( self->mode == W_JOIN )
    {
        struct CycleSibling* sib = calloc(1, sizeof(*sib));
        assert(sib);
        sib->task.vtable = &k_cycle_sibling_vtable;
        strcpy(sib->task.name, "sibling");
        sib->fx = fx;
        sib->due = self->due;
        PT_INIT(&sib->pt);
        ToriRS_TaskQueue_AddParallelPoolSubTask(fx->runner.queue, &sib->task, &self->join);
        PT_PARALLEL_AWAIT_JOIN(fx->runner.queue, join);
    }
    else if( self->mode == W_TURN )
        PT_PARALLEL_AWAIT_TURN(fx->index);
    else if( self->mode == W_REBUILD )
        PT_PARALLEL_AWAIT_REBUILD(&fx->view_built, fx->built);
    else
        PT_PARALLEL_AWAIT_CYCLE(&fx->clock, self->due);
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        *self->outcome = OUT_CANCELLED;
        PT_EXIT(&self->pt);
    }
    *self->outcome = OUT_RESUMED;
    PT_END(&self->pt);
}

static void
Waiter_Free(struct ToriRS_Task* base)
{
    struct Waiter* self = (struct Waiter*)base;
    RS_ParallelTask_Release(self->fx->index, self->fx->assets, &self->par);
    free(self);
}

static struct ToriRS_TaskVTable k_waiter_vtable = { .run = Waiter_Run, .free = Waiter_Free };

/* The earlier task for the waiter's key (a turn waits for its end): it waits
 * on the fixture's gate, then ends. */
struct Earlier
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct NamedFx* fx;
};

static int
Earlier_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Earlier* self = (struct Earlier*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    while( !self->par.cancelled && ToriRS_Trigger_Primed(&self->fx->gate) )
        TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TRIGGER, &self->fx->gate);
    PT_END(&self->pt);
}

static void
Earlier_Free(struct ToriRS_Task* base)
{
    struct Earlier* self = (struct Earlier*)base;
    RS_ParallelTask_Release(self->fx->index, self->fx->assets, &self->par);
    free(self);
}

static struct ToriRS_TaskVTable k_earlier_vtable = { .run = Earlier_Run, .free = Earlier_Free };

static struct Waiter*
add_waiter(struct NamedFx* fx, int mode, uint32_t due, int* outcome)
{
    struct Waiter* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_waiter_vtable;
    snprintf(t->task.name, sizeof(t->task.name), "wait_%s", k_mode_names[mode]);
    t->fx = fx;
    t->mode = mode;
    t->due = due;
    t->outcome = outcome;
    *outcome = OUT_WAITING;
    PT_INIT(&t->pt);
    RS_ParallelTask_Add(fx->runner.queue, fx->index, &t->task, &t->par, KIND_WAITER, waiter_key(), 0, 1, fx->clock.now);
    return t;
}

static void
named_fx_init(struct NamedFx* fx)
{
    struct AssetLoader loader;

    memset(fx, 0, sizeof(*fx));
    runner_init(&fx->runner, &fx->px, NULL, TASK_RUNNER_ROLE_PARALLEL);
    fx->assets = Assets_New();
    loader.start = named_fake_start;
    loader.resident = named_fake_resident;
    loader.data = fx;
    Assets_SetLoader(fx->assets, ASSET_KIND_MODEL, &loader);
    fx->index = RS_Pending_New();
    ToriRS_Clock_Init(&fx->clock, "the test clock", 100);
    ToriRS_Trigger_Init(&fx->view_built, "view 0 built");
    ToriRS_Trigger_Init(&fx->gate, "the earlier task's gate");
    ToriRS_TriggerOwner_Init(&fx->rebuild, TORIRS_OWNER_EXTERNAL, "a rebuild");
    ToriRS_TriggerOwner_Init(&fx->rebuild2, TORIRS_OWNER_EXTERNAL, "a second rebuild");
    ToriRS_TriggerOwner_Init(&fx->gate_owner, TORIRS_OWNER_EXTERNAL, "the gate");
}

static void
named_fx_free(struct NamedFx* fx)
{
    CHECK(fx->runner.queue->head == NULL);
    CHECK(RS_Pending_Listed(fx->index) == 0);
    runner_free(&fx->runner);
    ToriRS_TriggerOwner_End(&fx->rebuild, 0);
    ToriRS_TriggerOwner_End(&fx->rebuild2, 0);
    ToriRS_TriggerOwner_End(&fx->gate_owner, 0);
    ToriRS_Clock_Retire(&fx->clock);
    RS_Pending_Free(fx->index);
    Assets_Free(fx->assets);
}

static enum TaskRunnerStat
named_pump(struct NamedFx* fx)
{
    int steps = 0;
    return TaskRunner_PumpToStandstill(&fx->runner, 100, &steps);
}

/* Make mode `mode`'s wait end. */
static void
named_release(struct NamedFx* fx, int mode)
{
    if( mode == W_ASSET )
        Assets_Finished(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 7, 0), true);
    else if( mode == W_ASSETS )
    {
        Assets_Finished(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 8, 0), true);
        Assets_Finished(fx->assets, AssetKey_Make(ASSET_KIND_MODEL, 9, 0), true);
    }
    else if( mode == W_JOIN || mode == W_CYCLE )
        ToriRS_Clock_Advance(&fx->clock, 110);
    else if( mode == W_TURN )
        ToriRS_Trigger_Fire(&fx->gate);
    else
    {
        fx->built = 1;
        ToriRS_Trigger_Fire(&fx->view_built);
    }
}

/* What each mode's wait is behind, set up before the waiter. */
static void
named_arm(struct NamedFx* fx, int mode)
{
    if( mode == W_TURN )
    {
        struct Earlier* e = calloc(1, sizeof(*e));
        assert(e);
        e->task.vtable = &k_earlier_vtable;
        strcpy(e->task.name, "earlier");
        e->fx = fx;
        PT_INIT(&e->pt);
        ToriRS_Trigger_Prime(&fx->gate, &fx->gate_owner);
        RS_ParallelTask_Add(fx->runner.queue, fx->index, &e->task, &e->par, KIND_WAITER, waiter_key(), 0, 1, fx->clock.now);
    }
    else if( mode == W_REBUILD )
    {
        ToriRS_Trigger_Prime(&fx->view_built, &fx->rebuild);
        fx->built = 0;
    }
}

static void
test_named_waits_resume_and_cancel(void)
{
    printf("TEST: every wait parks with its kind, is not stepped until it is woken, resumes when what it "
           "waits for comes, and ends on cancel\n");
    for( int mode = 0; mode < W_MODES; mode++ )
    {
        struct NamedFx fx;
        struct Waiter* waiter;
        int outcome;
        int runs;

        /* Resumes when what it waits for comes. */
        named_fx_init(&fx);
        named_arm(&fx, mode);
        waiter = add_waiter(&fx, mode, 110, &outcome);
        CHECK(named_pump(&fx) == TASK_RUNNER_WAITING);
        CHECK(outcome == OUT_WAITING);
        CHECK(waiter->task.parked);
        CHECK(waiter->task.park.kind == k_mode_kinds[mode]);
        CHECK(!fx.runner.in_pump);
        /* Still parked a pump later, and not stepped: nothing woke it. */
        runs = waiter->runs;
        CHECK(named_pump(&fx) == TASK_RUNNER_WAITING);
        CHECK(outcome == OUT_WAITING);
        CHECK(waiter->runs == runs);
        if( mode == W_REBUILD )
        {
            /* A second rebuild primes it while the first is pending: the
             * older prime is abandoned, the waiter wakes, finds it primed
             * again, and parks again. */
            ToriRS_Trigger_Prime(&fx.view_built, &fx.rebuild2);
            CHECK(!waiter->task.parked);
            CHECK(waiter->task.woke == TORIRS_WAKE_ABANDONED);
            CHECK(named_pump(&fx) == TASK_RUNNER_WAITING);
            CHECK(waiter->task.parked);
            CHECK(outcome == OUT_WAITING);
        }
        named_release(&fx, mode);
        CHECK(named_pump(&fx) == TASK_RUNNER_IDLE);
        if( outcome != OUT_RESUMED )
            fprintf(stderr, "FAIL: the %s wait did not resume\n", k_mode_names[mode]);
        CHECK(outcome == OUT_RESUMED);
        named_fx_free(&fx);

        /* Ends at once on a cancel, what it waits for still to come. */
        named_fx_init(&fx);
        named_arm(&fx, mode);
        waiter = add_waiter(&fx, mode, 110, &outcome);
        CHECK(named_pump(&fx) == TASK_RUNNER_WAITING);
        /* (The turn's earlier task shares the key, and is cancelled too.) */
        CHECK(RS_Pending_CancelKey(fx.index, KIND_WAITER, waiter_key()) == (mode == W_TURN ? 2 : 1));
        /* The cancel itself made it ready. */
        CHECK(!waiter->task.parked);
        CHECK(waiter->task.woke == TORIRS_WAKE_CANCELLED);
        (void)named_pump(&fx);
        if( outcome != OUT_CANCELLED )
            fprintf(stderr, "FAIL: the %s wait did not end on its cancel\n", k_mode_names[mode]);
        CHECK(outcome == OUT_CANCELLED);
        /* A join's sibling outlives its cancelled parent and ends on its own. */
        if( mode == W_JOIN )
        {
            CHECK(fx.runner.queue->head != NULL);
            ToriRS_Clock_Advance(&fx.clock, 110);
            CHECK(named_pump(&fx) == TASK_RUNNER_IDLE);
        }
        if( mode == W_ASSET || mode == W_ASSETS )
        {
            /* The loads it dropped still end in the table; nothing counts
             * into the freed task. */
            named_release(&fx, mode);
        }
        named_fx_free(&fx);
    }
}

static void
between_frames_stepped_background(void)
{
    TaskRunner_CheckBetweenFramesExit(1, 0, 1, NULL);
}

static void
between_frames_stepped_main_after_boot(void)
{
    TaskRunner_CheckBetweenFramesExit(0, 2, 0, "PacketExec");
}

static void
test_the_between_frames_pump(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    int ended = 0;
    long before;

    printf("TEST: the between-frames pump steps the main queue only for the boot, never the background\n");
    /* The count the App reads is the runner's own: one pass is one. */
    runner_init(&runner, &px, NULL, TASK_RUNNER_ROLE_MAIN);
    ToriRS_TaskQueue_Add(runner.queue, new_yielder(0, &ended));
    before = runner.telemetry.passes;
    (void)TaskRunner_Step(&runner);
    CHECK(runner.telemetry.passes - before == 1);
    CHECK(ended == 1);
    runner_free(&runner);
    /* Allowed: nothing stepped; the boot's own main-queue passes. */
    TaskRunner_CheckBetweenFramesExit(0, 0, 0, NULL);
    TaskRunner_CheckBetweenFramesExit(1, 5, 0, "AppBoot");
    CHECK(dies_by_abort(between_frames_stepped_background));
    CHECK(dies_by_abort(between_frames_stepped_main_after_boot));
}

/* --- the compute check reads a step's COST, not its wall time ----------------
 *
 * A parallel step that took long on the wall clock but cost little processor
 * time (it waited for a core on a loaded machine) is not computing; one that
 * cost more than the threshold is, whatever the wall clock says. Without a
 * cost clock the check falls back to the wall time. */
static struct FakeClock g_burn_wall;
static uint64_t g_burn_cost_us;

static uint64_t
burn_cost_now(void)
{
    return g_burn_cost_us;
}

struct Burner
{
    struct ToriRS_Task task;
    uint64_t wall_us;
    uint64_t cost_us;
};

static int
Burner_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Burner* self = (struct Burner*)base;

    (void)io;
    g_burn_wall.now_us += self->wall_us;
    g_burn_cost_us += self->cost_us;
    return TORIRS_ASYNCIO_STAT_DONE;
}

static struct ToriRS_TaskVTable k_burner_vtable = { .run = Burner_Run, .free = task_free_libc };

static void
burn_once(struct TaskRunner* runner, char const* name, uint64_t wall_us, uint64_t cost_us)
{
    struct Burner* task = calloc(1, sizeof(*task));

    assert(task);
    task->task.vtable = &k_burner_vtable;
    snprintf(task->task.name, sizeof(task->task.name), "%s", name);
    task->wall_us = wall_us;
    task->cost_us = cost_us;
    ToriRS_TaskQueue_Add(runner->queue, &task->task);
    (void)TaskRunner_Step(runner);
}

static void
test_the_compute_check_reads_the_cost_clock(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };

    runner_init(&runner, &px, &g_burn_wall, TASK_RUNNER_ROLE_PARALLEL);
    runner.compute_step_us = 4000;
    TaskRunner_SetCostClock(&runner, burn_cost_now);
    /* 12 ms on the wall, 1 ms of processor: a loaded machine, not work. */
    burn_once(&runner, "Waited", 12000, 1000);
    CHECK(runner.compute_named_count == 0);
    /* 1 ms on the wall cannot hide 5 ms of processor (clocks disagree only
     * in a test, but the check must follow the cost one). */
    burn_once(&runner, "Computed", 1000, 5000);
    CHECK(runner.compute_named_count == 1);
    CHECK(runner.compute_named_count == 1 && strcmp(runner.compute_named[0], "Computed") == 0);
    /* Named once per name. */
    burn_once(&runner, "Computed", 1000, 5000);
    CHECK(runner.compute_named_count == 1);
    runner_free(&runner);

    /* No cost clock: the wall time is the cost. */
    runner_init(&runner, &px, &g_burn_wall, TASK_RUNNER_ROLE_PARALLEL);
    runner.compute_step_us = 4000;
    burn_once(&runner, "WallOnly", 12000, 1000);
    CHECK(runner.compute_named_count == 1);
    runner_free(&runner);
}

/* --- a frame is published over a parallel queue at a standstill -------------
 *
 * The check at the publish (TaskRunner_CheckPublishedAtStandstill): a task a
 * pump stepped and left ready trips it; a task parked on its read, or one
 * added after the last pump and never stepped (an asset the emit asked for),
 * does not. */
static struct TaskRunner g_publish_runner;

static void
publish_over_a_stepped_ready_task(void)
{
    TaskRunner_CheckPublishedAtStandstill(&g_publish_runner, "the test frame");
}

static void
test_a_frame_is_published_at_a_standstill(void)
{
    struct PlatformX_IO px = { 0 };
    int ended = 0;

    runner_init(&g_publish_runner, &px, NULL, TASK_RUNNER_ROLE_PARALLEL);
    /* Parked on a read that never lands, and one never stepped: neither is
     * work a pump left behind. */
    ToriRS_TaskQueue_Add(g_publish_runner.queue, new_stuck());
    (void)TaskRunner_Step(&g_publish_runner);
    ToriRS_TaskQueue_Add(g_publish_runner.queue, new_yielder(3, &ended));
    TaskRunner_CheckPublishedAtStandstill(&g_publish_runner, "the test frame");
    /* Stepped once inside a pump that then stopped short (what a settle
     * does: one pass, not a pump to a standstill) and still ready:
     * publishing over it is the breach. The pump number is what a
     * TaskRunner_PumpBegin gives the pass; set by hand, so no pump end
     * judges the short pump first. */
    g_publish_runner.pump = 1;
    (void)TaskRunner_Step(&g_publish_runner);
    CHECK(dies_by_abort(publish_over_a_stepped_ready_task));
    /* Pumped to a standstill: only the parked read is left. */
    {
        int steps = 0;
        (void)TaskRunner_PumpToStandstill(&g_publish_runner, 64, &steps);
    }
    CHECK(ended == 1);
    TaskRunner_CheckPublishedAtStandstill(&g_publish_runner, "the test frame");
    runner_free(&g_publish_runner);
}

int
main(void)
{
    test_render_ends_the_main_queues_pass();
    test_render_is_owed_until_a_frame_is_shown();
    test_render_and_spent_elsewhere_trip_the_assert();
    test_only_the_background_queue_is_sliced();
    test_one_pump_drains_the_parallel_queue();
    test_a_pump_ended_early_is_caught();
    test_a_task_without_its_turn_is_caught();
    test_who_steps_the_background_queue();
    test_named_waits_resume_and_cancel();
    test_the_between_frames_pump();
    test_the_compute_check_reads_the_cost_clock();
    test_a_frame_is_published_at_a_standstill();
    if( g_failures )
    {
        printf("queue_contract_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("queue_contract_test: all passed\n");
    return 0;
}
