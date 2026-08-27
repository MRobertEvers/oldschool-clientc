/*
 * A task can ask for the screen, and say what should be on it.
 *
 * The default is the opposite and stays the opposite: a cooperative yield is
 * not a frame boundary, and the runner settles as many of them as it likes
 * before publishing anything. That is what keeps one frame from being torn
 * into pieces by whatever incidental IO a task happens to do -- and it is also
 * why a long load used to finish entirely inside a single frame, with the
 * progress bar it was updating never drawn below 100.
 *
 * So the interruption is opt-in, and the request carries the picture: the task
 * knows which stage it is at, and the render step obeys rather than guessing.
 * These tests pin both halves -- that an ordinary yield still settles straight
 * through, and that a render yield stops the settle with its request intact.
 */
#include "asyncio.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Three stages, each announcing itself before doing its (imaginary) work. */
static char const* const k_captions[] = { "Loading config", "Loading interfaces", "Preparing" };
static int const k_percents[] = { 10, 60, 90 };

struct StagedTask
{
    struct ToriRS_Task task;
    struct pt pt;
    int stage;
    /* How many stages actually ran, so a test can tell "stopped" from
     * "silently ran to the end". */
    int completed;
};

static int
StagedTask_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct StagedTask* self = (struct StagedTask*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    for( self->stage = 0; self->stage < 3; self->stage++ )
    {
        PT_TASK_YIELD_TO_RENDER(
            TORIRS_RENDER_BOOT_BAR, k_percents[self->stage], k_captions[self->stage]);
        self->completed++;
    }
    PT_END(&self->pt);
}

/* The same shape without the opt-in: yields, but asks for nothing. */
struct QuietTask
{
    struct ToriRS_Task task;
    struct pt pt;
    int completed;
};

static int
QuietTask_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct QuietTask* self = (struct QuietTask*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    for( self->completed = 0; self->completed < 3; )
    {
        PT_YIELD(&self->pt);
        self->completed++;
    }
    PT_END(&self->pt);
}

static void
task_free_noop(struct ToriRS_Task* task)
{
    (void)task;
}

static struct ToriRS_TaskVTable k_staged_vtable = {
    .run = StagedTask_Run,
    .free = task_free_noop,
};

static struct ToriRS_TaskVTable k_quiet_vtable = {
    .run = QuietTask_Run,
    .free = task_free_noop,
};

/*
 * A render yield stops the queue and names its picture.
 *
 * Asserted on the queue rather than the runner so the contract is pinned where
 * it is implemented; the runner test below covers the hand-off.
 */
static void
test_render_yield_stops_the_queue(void)
{
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct ToriRS_IO* io = ToriRS_IO_New();
    static struct StagedTask staged;

    memset(&staged, 0, sizeof(staged));
    staged.task.vtable = &k_staged_vtable;
    snprintf(staged.task.name, sizeof(staged.task.name), "staged");
    ToriRS_TaskQueue_Add(queue, &staged.task);

    for( int i = 0; i < 3; i++ )
    {
        int stat = ToriRS_TaskQueue_Run(queue, io);

        TEST_ASSERT(stat == TORIRS_ASYNCIO_STAT_RENDER, "a render yield unwinds the queue");
        TEST_ASSERT(queue->head == &staged.task, "the task stays queued across the request");
        TEST_ASSERT(staged.task.wants_render != 0, "the request is still set on the task");
        TEST_ASSERT(
            staged.task.render.percent == k_percents[i], "the request carries this stage's bar");
        TEST_ASSERT(
            staged.task.render.caption == k_captions[i], "and this stage's words");
        TEST_ASSERT(
            staged.task.render.intent == TORIRS_RENDER_BOOT_BAR, "and what to draw them on");
        TEST_ASSERT(staged.completed == i, "the stage's work has not run yet");
    }

    TEST_ASSERT(
        ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_DONE, "then the task finishes");
    TEST_ASSERT(staged.completed == 3, "every stage ran");

    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
}

/*
 * A plain yield is still not a frame boundary.
 *
 * This is the half that must NOT change: without the opt-in the queue drains
 * straight through, whatever the task does in between.
 */
static void
test_a_plain_yield_still_settles_through(void)
{
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct ToriRS_IO* io = ToriRS_IO_New();
    static struct QuietTask quiet;
    int stat;
    int passes = 0;

    memset(&quiet, 0, sizeof(quiet));
    quiet.task.vtable = &k_quiet_vtable;
    snprintf(quiet.task.name, sizeof(quiet.task.name), "quiet");
    ToriRS_TaskQueue_Add(queue, &quiet.task);

    do
    {
        stat = ToriRS_TaskQueue_Run(queue, io);
        passes++;
        TEST_ASSERT(stat != TORIRS_ASYNCIO_STAT_RENDER, "a task that asks for nothing gets nothing");
    } while( stat == TORIRS_ASYNCIO_STAT_YIELD && passes < 8 );

    TEST_ASSERT(quiet.completed == 3, "the quiet task ran every step");

    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
}

/*
 * The request is cleared on resume, exactly like `blocked`.
 *
 * It describes one yield, never a standing mode: a task that asks for a frame
 * once and then works on must not keep interrupting every pass afterwards.
 */
static void
test_the_request_does_not_persist(void)
{
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct ToriRS_IO* io = ToriRS_IO_New();
    static struct StagedTask staged;

    memset(&staged, 0, sizeof(staged));
    staged.task.vtable = &k_staged_vtable;
    snprintf(staged.task.name, sizeof(staged.task.name), "staged");
    ToriRS_TaskQueue_Add(queue, &staged.task);

    ToriRS_TaskQueue_Run(queue, io);
    TEST_ASSERT(staged.task.wants_render != 0, "set by the yield");
    /* Resuming clears it before the task body runs again. */
    ToriRS_TaskQueue_Run(queue, io);
    TEST_ASSERT(
        staged.task.render.percent == k_percents[1],
        "the second stage overwrote the first's request rather than inheriting it");

    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    test_render_yield_stops_the_queue();
    test_a_plain_yield_still_settles_through();
    test_the_request_does_not_persist();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("task_render_yield_test: ok (%d checks)\n", g_checks);
    return 0;
}
