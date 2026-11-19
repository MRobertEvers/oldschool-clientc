/*
 * The queue that is given time: a pass with a deadline, a task that stops
 * itself for it, and a job that must be done by one.
 *
 * Pins:
 *
 *   1. THE PUMP GUARANTEE (owner, 2026-11-19): each pump of the given-time
 *      queue steps every task that can run at least once, whatever the
 *      clock says -- with a slice shorter than one step, every task is still
 *      stepped once, and does at least one unit of its work -- and then
 *      keeps stepping until every task is done or the slice is up. The
 *      strict FIFO (the main queue's paced settle) keeps the old rule: the
 *      head always runs, the clock is read before each task after it.
 *   2. A task that yields-if-spent stops for the slice and is NOT run again
 *      within it: RunUntil does not treat SPENT as progress -- but the task
 *      behind it still gets its turn in the same pump.
 *   3. A pass with no deadline -- every pass of a drained queue -- never
 *      reads the clock and never stops for it, even for a task that asks.
 *   4. A fan-out joined on the given-time queue completes across several
 *      slices, and a job past its deadline is finished in one unlimited run.
 *      A timed pass caps the reads it queues, for the tasks that have had
 *      their turn this pump.
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
test_every_task_gets_a_turn_whatever_the_slice(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px = { 0 };
    struct FakeClock clock = { 0 };
    struct Long* longs[6];
    int ran = 0;
    enum TaskRunnerStat stat;

    /* Six long tasks, 1 ms a chunk, and a 0.5 ms slice -- shorter than one
     * step of any of them. Before the guarantee the head ran and the walk
     * stopped: the five behind it waited, pump after pump. */
    runner_init(&runner, &px, &clock);
    runner.role = TASK_RUNNER_ROLE_BACKGROUND;
    for( int i = 0; i < 6; i++ )
    {
        longs[i] = new_long(&clock, 1000, 7);
        ToriRS_TaskQueue_Add(runner.queue, &longs[i]->task);
    }
    stat = TaskRunner_RunUntil(&runner, clock.now_us + 500);
    TEST_CHECK(stat == TASK_RUNNER_SPENT);
    for( int i = 0; i < 6; i++ )
    {
        /* Stepped once each, in one pump, and each did real work: its first
         * ask is answered "not over" (the grace), so a turn is never empty. */
        TEST_CHECK(longs[i]->resumes == 1);
        TEST_CHECK(longs[i]->done == 2);
    }
    /* The overrun is one step per task, and that is the price of the rule. */
    TEST_CHECK(clock.now_us == 12000);
    printf("ok - a 0.5 ms slice still steps all 6 one-chunk-per-ms tasks once (12 ms: one step each)\n");
    runner_free(&runner);

    /* A longer slice: the pump goes on until every task is done or the slice
     * is up. 10 bursts of 1 ms in 30 ms: all done, IDLE. */
    runner_init(&runner, &px, &clock);
    runner.role = TASK_RUNNER_ROLE_BACKGROUND;
    for( int i = 0; i < 10; i++ )
        ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 1000, &ran));
    TEST_CHECK(TaskRunner_RunUntil(&runner, clock.now_us + 30000) == TASK_RUNNER_IDLE);
    TEST_CHECK(ran == 10);
    /* Three long tasks of 30 ms each in a 10 ms slice: the pump continues
     * while time remains (the head works on to the deadline), every task
     * still gets its turn, and it stops when the slice is up. None ends, so
     * none is freed under the checks. */
    for( int i = 0; i < 3; i++ )
    {
        longs[i] = new_long(&clock, 1000, 30);
        ToriRS_TaskQueue_Add(runner.queue, &longs[i]->task);
    }
    {
        uint64_t const t0 = clock.now_us;
        TEST_CHECK(TaskRunner_RunUntil(&runner, t0 + 10000) == TASK_RUNNER_SPENT);
        TEST_CHECK(longs[0]->done == 10);
        for( int i = 1; i < 3; i++ )
        {
            TEST_CHECK(longs[i]->resumes == 1);
            TEST_CHECK(longs[i]->done == 2);
        }
    }
    while( runner.queue->head )
        TaskRunner_RunUntil(&runner, clock.now_us + 10000);
    printf("ok - a longer slice runs on until every task is done (IDLE) or the slice is up (SPENT)\n");
    runner_free(&runner);

    /* The strict FIFO keeps its rule, as the main queue's paced settle uses
     * it: 2.5 ms of 1 ms tasks runs three, a late caller still moves one. */
    runner_init(&runner, &px, &clock);
    runner.parallel = 0;
    ran = 0;
    for( int i = 0; i < 10; i++ )
        ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 1000, &ran));
    TEST_CHECK(TaskRunner_StepUntil(&runner, clock.now_us + 2500) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 3);
    TEST_CHECK(px.process_calls >= 1);
    TEST_CHECK(runner.io->slice.deadline_us == 0);
    TEST_CHECK(TaskRunner_StepUntil(&runner, clock.now_us) == TASK_RUNNER_SPENT);
    TEST_CHECK(ran == 4);
    while( runner.queue->head )
        TaskRunner_StepUntil(&runner, clock.now_us + 2500);
    TEST_CHECK(ran == 10);
    printf("ok - the strict FIFO's timed pass still stops after 3 of 10 and moves one when late\n");
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
    runner.role = TASK_RUNNER_ROLE_BACKGROUND;
    task = new_long(&clock, 1000, 7);
    ToriRS_TaskQueue_Add(runner.queue, &task->task);
    /* A task behind it: the long one's SPENT stop does not end the walk --
     * the one behind still gets its turn in this pump. */
    ToriRS_TaskQueue_Add(runner.queue, new_burst(&clock, 10, &behind));

    stat = TaskRunner_RunUntil(&runner, clock.now_us + 2500);
    TEST_CHECK(stat == TASK_RUNNER_SPENT);
    /* Chunks at 1, 2, 3 ms; the third saw the slice over and stopped. */
    TEST_CHECK(task->done == 3);
    /* Resumed exactly once: RunUntil did not step again within the slice. */
    TEST_CHECK(task->resumes == 1);
    TEST_CHECK(behind == 1);
    /* And it says why it stopped -- a SPENT stop is progress for the
     * caller's accounting, not a block or a wait. */
    TEST_CHECK(runner.progressed == 1);

    /* Next slice: it resumes after chunk 3, not from the start. Its turn's
     * first ask is the grace, so this turn does chunks 4 and 5 (and 6 is
     * where the clock stops it). */
    stat = TaskRunner_RunUntil(&runner, clock.now_us + 1500);
    TEST_CHECK(stat == TASK_RUNNER_SPENT);
    TEST_CHECK(task->done == 5);
    TEST_CHECK(task->resumes == 2);
    slices = 2;
    while( (stat = TaskRunner_RunUntil(&runner, clock.now_us + 2500)) != TASK_RUNNER_IDLE )
    {
        slices++;
        TEST_CHECK(slices < 10);
    }
    /* (It ended and was freed: nothing of it is read past here.) */
    printf("ok - a task that yields-if-spent stops once per slice and resumes where it stopped, and "
           "the task behind it is not starved (%d slices)\n",
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

    /* The same task on a queue given a clock but stepped plainly, as the
     * packet queue and the frame queue are outside a paced catch-up (and on
     * one with no clock at all). */
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
    /* Every loader gets its turn each pump, so the fan-out needs only a few
     * (the read, the decode, the collect), each overrunning by its steps. */
    TEST_CHECK(slices >= 3);
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
    /* Reads queued, counted by whoever wants to know (NULL: nobody). */
    int* asked;
};

static int
Reader_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Reader* self = (struct Reader*)base;
    struct ToriRS_IOItem* item;
    PT_BEGIN(&self->pt);
    ToriRS_IO_QueueFileRead(io, 0, "x");
    if( self->asked )
        (*self->asked)++;
    PT_YIELD(&self->pt);
    item = ToriRS_IO_TaskSlot(io, 0);
    ToriRS_IO_ClearItem(item);
    /* A second read, issued as the first lands: what a chain does. */
    ToriRS_IO_QueueFileRead(io, 0, "y");
    if( self->asked )
        (*self->asked)++;
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
    int asked = 0;
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
        t->asked = &asked;
        PT_INIT(&t->pt);
        ToriRS_TaskQueue_Add(runner.queue, &t->task);
    }
    /* The first pass of a pump gives every task its turn, cap or not: all
     * 300 readers queue their read. */
    runner.role = TASK_RUNNER_ROLE_BACKGROUND;
    TaskRunner_PumpBegin(&runner);
    stat = TaskRunner_StepUntil(&runner, clock.now_us + 2000);
    TEST_CHECK(stat == TASK_RUNNER_PROGRESSED);
    TEST_CHECK(px.process_calls == 1);
    TEST_CHECK(asked == 300);
    TEST_CHECK(landed == 0);
    /* The second pass of the same pump: every reader has had its turn, so
     * with no time passing on this clock only the cap stops the walk -- each
     * reader lands its first read and queues its second, and the walk passes
     * over the rest once the batch is full. Not the deadline, so the pass
     * reports progress, not SPENT. */
    stat = TaskRunner_StepUntil(&runner, clock.now_us + 2000);
    TEST_CHECK(stat == TASK_RUNNER_PROGRESSED);
    TEST_CHECK(px.process_calls == 2);
    TEST_CHECK(asked == 300 + TASK_RUNNER_SLICE_READS_MAX);
    TEST_CHECK(landed == 0);
    (void)TaskRunner_PumpEnd(&runner, "test");
    /* RunUntil keeps giving passes while time remains: every read is
     * answered. */
    while( (stat = TaskRunner_RunUntil(&runner, clock.now_us + 2000)) != TASK_RUNNER_IDLE )
    {
        passes++;
        TEST_CHECK(passes < 10);
    }
    TEST_CHECK(landed == 300);
    runner.role = TASK_RUNNER_ROLE_NONE;
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
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PROGRESSED);
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    TEST_CHECK(landed == 300);
    /* Two reads each, 300 at a time: three batches, the last one empty. */
    TEST_CHECK(px.process_calls == 3);
    printf("ok - every reader gets its turn in a pump's first pass; later passes cap at %d; a "
           "drained pass sends all 300 at once\n",
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

    /* The main queue's paced settle (app_net.c): a strict FIFO. */
    runner_init(&runner, &px, &clock);
    runner.parallel = 0;
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
    test_every_task_gets_a_turn_whatever_the_slice();
    test_yield_if_spent_is_not_rerun_within_the_slice();
    test_a_drained_pass_never_reads_the_clock();
    test_a_joined_fanout_completes_across_slices();
    test_a_sliced_pass_caps_the_batch_it_queues();
    test_a_cancelled_parent_detaches_its_loaders();
    test_a_settle_with_a_deadline_stops_between_tasks_and_resumes();
    printf("task-budget: all tests passed\n");
    return 0;
}
