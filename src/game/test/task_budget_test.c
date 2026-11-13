/*
 * The queue that is given time: a pass with a deadline, a task that stops
 * itself for it, and a job that must be done by one.
 *
 * Pins:
 *
 *   1. A pass given a deadline reads its clock between tasks and stops when
 *      the deadline has passed, reporting SPENT; the next pass carries on
 *      where the queue stands. One task always runs, or a late caller would
 *      never move the queue.
 *   2. A task that yields-if-spent stops for the slice and is NOT run again
 *      within it: RunUntil does not treat SPENT as progress.
 *   3. A pass with no deadline -- every pass of a drained queue -- never
 *      reads the clock and never stops for it, even for a task that asks.
 *   4. A fan-out joined on the given-time queue completes across several
 *      slices, and a job past its deadline is finished in one unlimited run.
 *   5. A parent cancelled mid-fan-out detaches its loaders, which run to
 *      their own end counted by nothing.
 *
 * The clock is the test's: a counter each task advances by what its work
 * "costs", so the slicing is exact rather than timing-dependent.
 */
#include "asyncio.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

/* platform_x_io.h keeps this opaque; every read here is answered in Process. */
struct PlatformX_IO
{
    int process_calls;
    int pump_calls;
};

void
PlatformX_IO_Pump(struct PlatformX_IO* px)
{
    px->pump_calls++;
}

int
PlatformX_IO_Process(struct PlatformX_IO* px, struct ToriRS_IOBatch* io)
{
    px->process_calls++;
    ToriRS_IOBatch_Reset(io);
    return 0;
}

/* --- the fake clock --------------------------------------------------------- */

struct FakeClock
{
    uint64_t now_us;
    int reads;
};

static uint64_t
fake_now(void* ud)
{
    struct FakeClock* clock = ud;
    clock->reads++;
    return clock->now_us;
}

static void
test_task_free_libc(struct ToriRS_Task* task)
{
    free(task);
}

/* --- a task that costs a fixed time per run and ends ------------------------ */

struct Burst
{
    struct ToriRS_Task task;
    struct pt pt;
    struct FakeClock* clock;
    uint64_t cost_us;
    int* ran;
};

static int
Burst_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Burst* self = (struct Burst*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    self->clock->now_us += self->cost_us;
    (*self->ran)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_burst_vtable = { .run = Burst_Run, .free = test_task_free_libc };

static struct ToriRS_Task*
new_burst(struct FakeClock* clock, uint64_t cost_us, int* ran)
{
    struct Burst* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_burst_vtable;
    strcpy(t->task.name, "burst");
    t->clock = clock;
    t->cost_us = cost_us;
    t->ran = ran;
    PT_INIT(&t->pt);
    return &t->task;
}

/* --- a long task that works in chunks and stops itself for the slice -------- */

struct Long
{
    struct ToriRS_Task task;
    struct pt pt;
    struct FakeClock* clock;
    uint64_t chunk_us;
    int chunks;
    int done;
    int resumes;
};

static int
Long_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Long* self = (struct Long*)base;
    self->resumes++;
    PT_BEGIN(&self->pt);
    for( self->done = 0; self->done < self->chunks; )
    {
        self->clock->now_us += self->chunk_us;
        self->done++;
        PT_TASK_YIELD_IF_SPENT();
    }
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_long_vtable = { .run = Long_Run, .free = test_task_free_libc };

static struct Long*
new_long(struct FakeClock* clock, uint64_t chunk_us, int chunks)
{
    struct Long* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_long_vtable;
    strcpy(t->task.name, "long");
    t->clock = clock;
    t->chunk_us = chunk_us;
    t->chunks = chunks;
    PT_INIT(&t->pt);
    return t;
}

/* --- a fan-out: N loaders, each "reading" once, then the join --------------- */

struct Loader
{
    struct ToriRS_Task task;
    struct pt pt;
    struct FakeClock* clock;
    int* landed;
};

static int
Loader_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Loader* self = (struct Loader*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    self->clock->now_us += 100;
    PT_YIELD(&self->pt);
    /* The decode: what makes a region's gather long. */
    self->clock->now_us += 400;
    (*self->landed)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_loader_vtable = { .run = Loader_Run, .free = test_task_free_libc };

static struct ToriRS_Task*
new_loader(struct FakeClock* clock, int* landed)
{
    struct Loader* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_loader_vtable;
    strcpy(t->task.name, "loader");
    t->clock = clock;
    t->landed = landed;
    PT_INIT(&t->pt);
    return &t->task;
}

struct Gather
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_TaskQueue* queue;
    struct FakeClock* clock;
    int fanout;
    int pending;
    int landed;
    int* finished;
};

static int
Gather_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Gather* self = (struct Gather*)base;
    PT_BEGIN(&self->pt);
    for( int i = 0; i < self->fanout; i++ )
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            self->queue, new_loader(self->clock, &self->landed), &self->pending);
    PT_TASK_JOIN(pending);
    /* The collect step between stages: long enough to stop for. */
    self->clock->now_us += 300;
    PT_TASK_YIELD_IF_SPENT();
    *self->finished = self->landed;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_gather_vtable = { .run = Gather_Run, .free = test_task_free_libc };

static struct Gather*
new_gather(struct ToriRS_TaskQueue* queue, struct FakeClock* clock, int fanout, int* finished)
{
    struct Gather* t = calloc(1, sizeof(*t));
    assert(t);
    t->task.vtable = &k_gather_vtable;
    strcpy(t->task.name, "gather");
    t->queue = queue;
    t->clock = clock;
    t->fanout = fanout;
    t->finished = finished;
    PT_INIT(&t->pt);
    return t;
}

static void
runner_init(struct TaskRunner* runner, struct PlatformX_IO* px, struct FakeClock* clock)
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->parallel = 1;
    if( clock )
        TaskRunner_SetClock(runner, fake_now, clock);
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

/* 1. */
static void
test_deadline_stops_the_walk_and_the_next_pass_resumes(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    int ran = 0;
    int passes = 0;

    runner_init(&runner, &px, &clock);
    for( int i = 0; i < 10; i++ )
        ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 1000, &ran));

    /* 2.5 ms: the first task runs unasked, the clock reads 1 ms -> the second
     * runs, 2 ms -> the third, 3 ms -> stop. */
    TEST_CHECK(TaskRunner_StepUntil(&runner, clock.now_us + 2500) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 3);
    /* The reads queued so far still went out. */
    TEST_CHECK(px.process_calls == 1);
    /* The deadline is the pass's, not left on the queue. */
    TEST_CHECK(runner.io->slice.deadline_us == 0);

    /* Already past: one task still runs, so a late caller moves the queue. */
    TEST_CHECK(TaskRunner_StepUntil(&runner, clock.now_us) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 4);

    while( runner.queue->head )
    {
        TaskRunner_StepUntil(&runner, clock.now_us + 2500);
        passes++;
        TEST_CHECK(passes < 10);
    }
    TEST_CHECK(ran == 10);
    printf("ok - a 2.5 ms deadline stops a 1 ms-a-task walk after 3; the rest go in %d more passes\n",
        passes);
    runner_free(&runner);
}

/* 2. */
static void
test_yield_if_spent_is_not_rerun_within_the_slice(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct Long* task;
    int behind = 0;
    enum TaskRunnerStat stat;
    int slices = 0;

    runner_init(&runner, &px, &clock);
    task = new_long(&clock, 1000, 7);
    ToriRS_TaskQueue_Add(runner.queue, &task->task);
    /* A task behind it: a SPENT stop ends the walk, so it must not run in
     * the slice the long one used up. */
    ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 10, &behind));

    stat = TaskRunner_RunUntil(&runner, clock.now_us + 2500);
    TEST_CHECK(stat == TASK_RUNNER_SPENT);
    /* Chunks at 1, 2, 3 ms; the third saw the slice over and stopped. */
    TEST_CHECK(task->done == 3);
    /* Resumed exactly once: RunUntil did not step again within the slice. */
    TEST_CHECK(task->resumes == 1);
    TEST_CHECK(behind == 0);
    /* And it says why it stopped -- a SPENT stop is progress for the
     * caller's accounting, not a block or a wait. */
    TEST_CHECK(runner.progressed == 1);

    /* Next slice: it resumes after chunk 3, not from the start. */
    stat = TaskRunner_RunUntil(&runner, clock.now_us + 2500);
    TEST_CHECK(stat == TASK_RUNNER_SPENT);
    TEST_CHECK(task->done == 6);
    TEST_CHECK(task->resumes == 2);
    slices = 2;
    while( (stat = TaskRunner_RunUntil(&runner, clock.now_us + 2500)) != TASK_RUNNER_IDLE )
    {
        slices++;
        TEST_CHECK(slices < 10);
    }
    TEST_CHECK(behind == 1);
    printf("ok - a task that yields-if-spent stops once per slice and resumes where it stopped "
           "(%d slices)\n",
        slices + 1);
    runner_free(&runner);
}

/* 3. */
static void
test_a_drained_pass_never_reads_the_clock(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct Long* task;
    int ran = 0;

    /* The same task on a queue given a clock but stepped plainly (and on one
     * with no clock at all, as the packet FIFO and the asset queue are). */
    runner_init(&runner, &px, &clock);
    task = new_long(&clock, 1000, 7);
    ToriRS_TaskQueue_Add(runner.queue, &task->task);
    for( int i = 0; i < 5; i++ )
        ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 1000, &ran));
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    TEST_CHECK(ran == 5);
    TEST_CHECK(clock.reads == 0);
    runner_free(&runner);

    runner_init(&runner, &px, NULL);
    task = new_long(&clock, 1000, 7);
    ToriRS_TaskQueue_Add(runner.queue, &task->task);
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    runner_free(&runner);

    /* And the strict FIFO is still strict: a head that yields stops the walk
     * with or without a slice. */
    runner_init(&runner, &px, &clock);
    runner.parallel = 0;
    ran = 0;
    {
        int landed = 0;
        ToriRS_TaskQueue_Add(runner.queue, new_loader(&clock, &landed));
        ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 1, &ran));
        TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PROGRESSED);
        TEST_CHECK(ran == 0);
        /* The head ends, and only then does the one behind it run. */
        TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
        TEST_CHECK(landed == 1);
        TEST_CHECK(ran == 1);
    }
    TEST_CHECK(clock.reads == 0);
    runner_free(&runner);
    printf("ok - a pass with no deadline runs every yield-if-spent straight through and never "
           "reads the clock\n");
}

/* 4. */
static void
test_a_joined_fanout_completes_across_slices(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct ToriRS_TaskJob job = { 0 };
    struct Gather* gather;
    int finished = -1;
    int slices = 0;

    runner_init(&runner, &px, &clock);
    gather = new_gather(runner.queue, &clock, 40, &finished);
    ToriRS_TaskJob_Start(&job, &runner, &gather->task, clock.now_us + 1000000);
    TEST_CHECK(!ToriRS_TaskJob_Done(&job));

    /* 2 ms slices against 40 x 0.5 ms of decode plus the reads. */
    while( !ToriRS_TaskJob_Done(&job) )
    {
        enum TaskRunnerStat stat = TaskRunner_RunUntil(&runner, clock.now_us + 2000);
        TEST_CHECK(stat != TASK_RUNNER_BLOCKED || ToriRS_TaskJob_Done(&job));
        slices++;
        TEST_CHECK(slices < 100);
    }
    TEST_CHECK(finished == 40);
    TEST_CHECK(slices > 5);
    TEST_CHECK(runner.queue->head == NULL);
    printf("ok - a 40-load fan-out joined on the given-time queue completes in %d 2 ms slices\n",
        slices);

    /* Promotion: the same job finished by one unlimited run. */
    finished = -1;
    gather = new_gather(runner.queue, &clock, 40, &finished);
    ToriRS_TaskJob_Start(&job, &runner, &gather->task, clock.now_us + 1);
    TaskRunner_RunUntil(&runner, clock.now_us + 2000);
    TEST_CHECK(!ToriRS_TaskJob_Done(&job));
    TEST_CHECK(clock.now_us >= job.must_finish_by_us);
    job.promoted = 1;
    TaskRunner_RunUntil(&runner, 0);
    TEST_CHECK(ToriRS_TaskJob_Done(&job));
    TEST_CHECK(finished == 40);
    printf("ok - a job past its deadline is finished by one unlimited run\n");
    runner_free(&runner);
}

/* --- a loader that really queues a read (the cap counts io->active) -------- */

struct Reader
{
    struct ToriRS_Task task;
    struct pt pt;
    int* landed;
};

static int
Reader_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Reader* self = (struct Reader*)base;
    struct ToriRS_IOItem* item;
    PT_BEGIN(&self->pt);
    ToriRS_IO_QueueFileRead(io, 0, "x");
    PT_YIELD(&self->pt);
    item = ToriRS_IO_TaskSlot(io, 0);
    ToriRS_IO_ClearItem(item);
    (*self->landed)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_reader_vtable = { .run = Reader_Run, .free = test_task_free_libc };

/* 4b. */
static void
test_a_sliced_pass_caps_the_batch_it_queues(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    int landed = 0;
    int passes = 0;
    enum TaskRunnerStat stat;

    runner_init(&runner, &px, &clock);
    for( int i = 0; i < 300; i++ )
    {
        struct Reader* t = calloc(1, sizeof(*t));
        assert(t);
        t->task.vtable = &k_reader_vtable;
        strcpy(t->task.name, "reader");
        t->landed = &landed;
        PT_INIT(&t->pt);
        ToriRS_TaskQueue_Add(runner.queue, &t->task);
    }
    /* No time passes on this clock, so only the cap can stop the walk -- and
     * it is not the deadline, so the pass reports progress, not SPENT. */
    stat = TaskRunner_StepUntil(&runner, clock.now_us + 2000);
    TEST_CHECK(stat == TASK_RUNNER_PROGRESSED);
    TEST_CHECK(px.process_calls == 1);
    {
        int queued = 0;
        for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
            if( t->io.kind != TORIRS_IOK_NONE )
                queued++;
        TEST_CHECK(queued == TASK_RUNNER_SLICE_READS_MAX);
    }
    /* RunUntil keeps giving passes while time remains: every read is out and
     * answered, a batch at a time. */
    while( (stat = TaskRunner_RunUntil(&runner, clock.now_us + 2000)) != TASK_RUNNER_IDLE )
    {
        passes++;
        TEST_CHECK(passes < 10);
    }
    TEST_CHECK(landed == 300);
    /* And a drained pass is not capped: one batch of all 300. */
    px.process_calls = 0;
    landed = 0;
    for( int i = 0; i < 300; i++ )
    {
        struct Reader* t = calloc(1, sizeof(*t));
        assert(t);
        t->task.vtable = &k_reader_vtable;
        strcpy(t->task.name, "reader");
        t->landed = &landed;
        PT_INIT(&t->pt);
        ToriRS_TaskQueue_Add(runner.queue, &t->task);
    }
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PROGRESSED);
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    TEST_CHECK(landed == 300);
    TEST_CHECK(px.process_calls == 2);
    printf("ok - a sliced pass stops at %d queued reads and the next pass lands them; a drained "
           "pass sends all 300 at once\n",
        TASK_RUNNER_SLICE_READS_MAX);
    runner_free(&runner);
}

/* 5. */
static void
test_a_cancelled_parent_detaches_its_loaders(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct ToriRS_TaskJob job = { 0 };
    struct Gather* gather;
    int finished = -1;
    int landed_after;

    runner_init(&runner, &px, &clock);
    gather = new_gather(runner.queue, &clock, 10, &finished);
    ToriRS_TaskJob_Start(&job, &runner, &gather->task, clock.now_us + 1000000);
    /* The fan-out is queued and every loader has issued its read. */
    TaskRunner_Step(&runner);
    TEST_CHECK(gather->pending == 10);

    /* Cancel: detach, then take the parent off (its end decrements the job). */
    ToriRS_TaskQueue_DetachJoin(runner.queue, &gather->pending);
    landed_after = gather->landed;
    ToriRS_TaskQueue_Remove(runner.queue, &gather->task);
    TEST_CHECK(ToriRS_TaskJob_Done(&job));
    for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
        TEST_CHECK(t->join == NULL);
    /* The loaders run out on their own; nothing counts them. Their `landed`
     * pointer was the parent's, so give them somewhere live to land. */
    {
        int landed = 0;
        for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
            ((struct Loader*)t)->landed = &landed;
        TaskRunner_RunUntil(&runner, 0);
        TEST_CHECK(landed == 10);
    }
    (void)landed_after;
    TEST_CHECK(finished == -1);
    TEST_CHECK(runner.queue->head == NULL);
    printf("ok - a parent cancelled mid-fan-out leaves its loaders to end uncounted\n");
    runner_free(&runner);
}

/* 8. */
static void
test_a_settle_with_a_deadline_stops_between_tasks_and_resumes(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    int ran = 0;

    runner_init(&runner, &px, &clock);
    for( int i = 0; i < 10; i++ )
        TaskRunner_AddRenderBlockingSerialTask(&runner, new_burst(&clock, 1000, &ran));

    /* 2.5 ms of 1 ms tasks: three run, and the settle says it is not done
     * the way a parked read would -- not IDLE, so its caller keeps the frame. */
    TEST_CHECK(TaskRunner_SettleFrameUntil(&runner, clock.now_us + 2500) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 3);
    TEST_CHECK(TaskRunner_RenderBlockingRemains(&runner));
    TEST_CHECK(runner.io->slice.deadline_us == 0);

    /* A deadline already past still moves the queue by one task. */
    TEST_CHECK(TaskRunner_SettleFrameUntil(&runner, clock.now_us) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 4);

    /* No deadline is an ordinary settle: to the end, never reading the clock. */
    clock.reads = 0;
    TEST_CHECK(TaskRunner_SettleFrameUntil(&runner, 0) == TASK_RUNNER_IDLE);
    TEST_CHECK(ran == 10);
    TEST_CHECK(clock.reads == 0);
    TEST_CHECK(!TaskRunner_RenderBlockingRemains(&runner));
    printf("ok - a settle given 2.5 ms runs 3 of 10 tasks and reports SPENT; without a deadline it finishes\n");
    runner_free(&runner);
}

int
main(void)
{
    test_deadline_stops_the_walk_and_the_next_pass_resumes();
    test_yield_if_spent_is_not_rerun_within_the_slice();
    test_a_drained_pass_never_reads_the_clock();
    test_a_joined_fanout_completes_across_slices();
    test_a_sliced_pass_caps_the_batch_it_queues();
    test_a_cancelled_parent_detaches_its_loaders();
    test_a_settle_with_a_deadline_stops_between_tasks_and_resumes();
    printf("task-budget: all tests passed\n");
    return 0;
}
