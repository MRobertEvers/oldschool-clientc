/*
 * The frame contract's queue half (app.h, beside the three runners), held by
 * the runner itself (task_runner.h) and checked here with no App.
 *
 * Pins:
 *
 *   1. RESULTS BY QUEUE. A task that asks for a frame (RENDER) on the main
 *      queue ends that queue's pass with its request carried and the task
 *      behind it not run; on the parallel or the background queue the same
 *      request trips the runner's assert. SPENT anywhere but the background
 *      queue trips it too.
 *   2. THE PARALLEL PUMP DRAINS. One pump (TaskRunner_PumpToStandstill)
 *      never returns while a task that is neither done, waiting on a read,
 *      nor joined remains -- however many plain yields that takes -- and
 *      ends with exactly the tasks that wait. A pump ended before that is
 *      caught (TaskRunner_PumpEnd asserts).
 *   3. NO TASK IS LEFT WITHOUT ITS TURN. A background pump that leaves a
 *      task that could run unstepped is caught.
 *   4. WHO STEPS THE BACKGROUND QUEUE. A background pass entered from inside
 *      the script settle, or from inside a task of another queue, trips the
 *      check; the job owner's finish (owner = 1) from the packet pump does
 *      not, and neither does a pass from the frame body.
 *
 * The death checks run in a forked child and pass when it dies by SIGABRT
 * (the assert), which is how the assert is proved to fire without changing
 * the code under test. Built without NDEBUG (the Makefile target), so the
 * asserts are live.
 *
 *   make -C src test-queue-contract
 */
#include "asyncio.h"
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
     * forbids; this is what a task that checks it would then return. */
    runner_init(&runner, &px, &clock, TASK_RUNNER_ROLE_PARALLEL);
    ToriRS_TaskQueue_Add(runner.queue, new_slicer(&clock));
    (void)TaskRunner_StepUntil(&runner, clock.now_us + 1);
    (void)TaskRunner_StepUntil(&runner, clock.now_us + 1);
}

static void
test_render_and_spent_elsewhere_trip_the_assert(void)
{
    printf("TEST: RENDER on the parallel or background queue, SPENT on the parallel queue, assert\n");
    CHECK(dies_by_abort(render_on_parallel));
    CHECK(dies_by_abort(render_on_background));
    CHECK(dies_by_abort(spent_on_parallel));
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
        CHECK(t->io.pending || t->blocked);
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

int
main(void)
{
    test_render_ends_the_main_queues_pass();
    test_render_and_spent_elsewhere_trip_the_assert();
    test_one_pump_drains_the_parallel_queue();
    test_a_pump_ended_early_is_caught();
    test_a_task_without_its_turn_is_caught();
    test_who_steps_the_background_queue();
    if( g_failures )
    {
        printf("queue_contract_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("queue_contract_test: all passed\n");
    return 0;
}
