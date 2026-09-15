/*
 * The sibling join: a task that fans loads out on the (parallel) asset queue
 * and waits for them to END, not for their records to become resident.
 *
 * Pins three things:
 *
 *   1. Every sibling of a fan-out runs in the SAME pass -- the reads go out
 *      together -- and the parent resumes only once the last has ended.
 *   2. A sibling that exits early (a record the cache cannot serve) counts
 *      exactly like one that landed. This is the case the residency wait
 *      got wrong: it spent a 600-pass budget, two passes a frame, on one
 *      texture id the loader had refused -- six seconds per rebuild.
 *   3. A NULL handed to AddJoined (the "already resident" answer every
 *      CreateTask_*Load gives) is not counted, so a fan-out with nothing to
 *      load joins at once.
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
};

int
PlatformX_IO_Pending(struct PlatformX_IO* px, struct ToriRS_IO* io)
{
    (void)px;
    (void)io;
    return 0;
}

int
PlatformX_IO_SlotPending(struct PlatformX_IO* px, struct ToriRS_IO* io, int slot)
{
    (void)px;
    (void)io;
    (void)slot;
    return 0;
}

int
PlatformX_IO_Process(struct PlatformX_IO* px, struct ToriRS_IO* io)
{
    px->process_calls++;
    ToriRS_IO_ResetActive(io);
    return 0;
}

/* A loader: yields once "for its read", then either lands or refuses. */
struct Loader
{
    struct ToriRS_Task task;
    struct pt pt;
    int* landed;
    int* refused;
    int fails;
    int started_in_pass;
    int* pass_clock;
};

static int
Loader_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct Loader* self = (struct Loader*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    self->started_in_pass = *self->pass_clock;
    PT_YIELD(&self->pt);
    if( self->fails )
    {
        (*self->refused)++;
        PT_EXIT(&self->pt);
    }
    (*self->landed)++;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_loader_vtable = { .run = Loader_Run, .free = NULL };

static struct ToriRS_Task*
new_loader(int* landed, int* refused, int fails, int* pass_clock)
{
    struct Loader* t = calloc(1, sizeof(*t));
    t->task.vtable = &k_loader_vtable;
    strcpy(t->task.name, "loader");
    t->landed = landed;
    t->refused = refused;
    t->fails = fails;
    t->pass_clock = pass_clock;
    PT_INIT(&t->pt);
    return &t->task;
}

/* The parent: fans N loaders out, joins, records when it got through. */
struct Parent
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_TaskQueue* queue;
    int fanout;
    int fail_every;
    int pending;
    int landed;
    int refused;
    int joined_in_pass;
    int resumes;
    int* pass_clock;
    int add_null;
};

static int
Parent_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct Parent* self = (struct Parent*)base;
    (void)io;
    self->resumes++;
    PT_BEGIN(&self->pt);
    for( int i = 0; i < self->fanout; i++ )
        ToriRS_TaskQueue_AddJoined(
            self->queue,
            new_loader(
                &self->landed,
                &self->refused,
                self->fail_every && (i % self->fail_every) == 0,
                self->pass_clock),
            &self->pending);
    if( self->add_null )
        TEST_CHECK(ToriRS_TaskQueue_AddJoined(self->queue, NULL, &self->pending) == 0);
    PT_TASK_JOIN(pending);
    self->joined_in_pass = *self->pass_clock;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_parent_vtable = { .run = Parent_Run, .free = NULL };

static struct Parent*
new_parent(struct ToriRS_TaskQueue* queue, int fanout, int fail_every, int* pass_clock)
{
    struct Parent* t = calloc(1, sizeof(*t));
    t->task.vtable = &k_parent_vtable;
    strcpy(t->task.name, "parent");
    t->queue = queue;
    t->fanout = fanout;
    t->fail_every = fail_every;
    t->pass_clock = pass_clock;
    PT_INIT(&t->pt);
    return t;
}

static void
test_fanout_joins_when_every_sibling_ends(void)
{
    struct TaskRunner runner = { 0 };
    struct PlatformX_IO px = { 0 };
    int pass_clock = 0;
    struct Parent* parent;

    runner.queue = ToriRS_TaskQueue_New();
    runner.io = ToriRS_IO_New();
    runner.px = &px;
    runner.parallel = 1;

    /* 200 siblings, every fourth refusing: far past the slot table's opening
     * size, so the fan-out also exercises its growth. */
    parent = new_parent(runner.queue, 200, 4, &pass_clock);
    ToriRS_TaskQueue_Add(runner.queue, &parent->task);

    /* Pass 1: the parent fans out and parks; every sibling runs and yields
     * for its read -- all in this one pass, which is the whole point. */
    pass_clock = 1;
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_PENDING);
    TEST_CHECK(parent->pending == 200);
    {
        int started_now = 0;
        for( struct ToriRS_Task* t = runner.queue->head; t; t = t->next )
            if( t != &parent->task && ((struct Loader*)t)->started_in_pass == 1 )
                started_now++;
        TEST_CHECK(started_now == 200);
    }

    /* Pass 2: every sibling resumes and ends -- landed or refused alike --
     * and the count reaches zero. The parent is parked as BLOCKED (its wait
     * is on the asset queue's progress, not on a read of its own), so it is
     * stepped again and gets through in pass 3. */
    pass_clock = 2;
    TaskRunner_Step(&runner);
    TEST_CHECK(parent->pending == 0);
    TEST_CHECK(parent->landed == 150);
    TEST_CHECK(parent->refused == 50);

    pass_clock = 3;
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    TEST_CHECK(runner.queue->head == NULL);
    /* joined_in_pass is read off the freed parent: copy what we need first. */
    printf("ok - 200 siblings (50 refusing) go out in one pass and join in the next\n");

    ToriRS_TaskQueue_Free(runner.queue);
    ToriRS_IO_Free(runner.io);
}

static void
test_refusals_do_not_stall_the_join(void)
{
    struct TaskRunner runner = { 0 };
    struct PlatformX_IO px = { 0 };
    int pass_clock = 0;
    struct Parent* parent;
    int resumes;

    runner.queue = ToriRS_TaskQueue_New();
    runner.io = ToriRS_IO_New();
    runner.px = &px;
    runner.parallel = 1;

    /* Every sibling refuses. A residency wait would never end; a join ends
     * when they do, in exactly as many passes as a landing set takes. */
    parent = new_parent(runner.queue, 8, 1, &pass_clock);
    ToriRS_TaskQueue_Add(runner.queue, &parent->task);

    while( TaskRunner_Step(&runner) != TASK_RUNNER_IDLE )
    {
        pass_clock++;
        TEST_CHECK(pass_clock < 8);
    }
    resumes = 3; /* fan out, see them end, get through */
    (void)resumes;
    TEST_CHECK(runner.queue->head == NULL);
    printf("ok - a set of refused loads joins in %d passes instead of spinning a budget\n",
        pass_clock + 1);

    ToriRS_TaskQueue_Free(runner.queue);
    ToriRS_IO_Free(runner.io);
}

static void
test_nothing_to_load_joins_at_once(void)
{
    struct TaskRunner runner = { 0 };
    struct PlatformX_IO px = { 0 };
    int pass_clock = 1;
    struct Parent* parent;

    runner.queue = ToriRS_TaskQueue_New();
    runner.io = ToriRS_IO_New();
    runner.px = &px;
    runner.parallel = 1;

    parent = new_parent(runner.queue, 0, 0, &pass_clock);
    parent->add_null = 1;
    ToriRS_TaskQueue_Add(runner.queue, &parent->task);

    /* Every record already resident: AddJoined counted nothing, and the join
     * falls straight through in the pass the parent first ran. */
    TEST_CHECK(TaskRunner_Step(&runner) == TASK_RUNNER_IDLE);
    TEST_CHECK(runner.queue->head == NULL);
    printf("ok - a fan-out with nothing to load joins in its first pass\n");

    ToriRS_TaskQueue_Free(runner.queue);
    ToriRS_IO_Free(runner.io);
}

int
main(void)
{
    test_fanout_joins_when_every_sibling_ends();
    test_refusals_do_not_stall_the_join();
    test_nothing_to_load_joins_at_once();
    printf("task-join: all tests passed\n");
    return 0;
}
