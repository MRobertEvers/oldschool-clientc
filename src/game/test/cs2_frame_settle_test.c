/*
 * CS2 frame-settle barrier regression.
 *
 * A CS2 task may change several widget fields around one or more cache yields.
 * Those changes are one visual transaction: the display list must never be
 * rebuilt from the intermediate tree.  Ready work is therefore drained to
 * completion without a per-frame step cap.  A genuine asynchronous wait is
 * the only reason to return to the frame loop, and that path retains both the
 * last published frame and the pending redraw request.
 *
 * This test is deliberately cache- and renderer-free.  Its tiny frame model
 * makes publication observable, while the real TaskRunner settle helper drives
 * fake protothread tasks through synchronous and externally-pending yields.
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

enum
{
    READY_YIELDS = 129,
};

/* platform_x_io.h intentionally keeps this type opaque.  A test-local backend
 * lets us distinguish a ready protothread yield from a genuine external wait
 * without starting SDL, JS5, or a cache server. */
struct PlatformX_IO
{
    int pending;
    int pending_checks;
    int pending_watchdog;
    int process_calls;
};

int
PlatformX_IO_Pending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
    assert(px);
    assert(io);
    if( px->pending )
    {
        px->pending_checks++;
        /* Turn a broken blocking drain into an assertion failure instead of a
         * hung test process.  A correct settle call returns on its first true
         * pending observation and never reaches this watchdog. */
        if( px->pending_watchdog > 0 && px->pending_checks > px->pending_watchdog )
            px->pending = 0;
    }
    return px->pending;
}

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
    assert(px);
    assert(io);
    px->process_calls++;
    ToriRS_IO_ResetActive(io);
    return 0;
}

struct WidgetState
{
    int equipment_visible;
    int familiar_visible;
    int mutation_count;
    int completed;
};

struct PublishedFrame
{
    int equipment_visible;
    int familiar_visible;
    int commit_count;
    int need_redraw;
};

struct MutateTask
{
    struct ToriRS_Task task;
    struct pt pt;
    struct WidgetState* widgets;
    struct PlatformX_IO* px;
    int ready_yields;
    int wait_external;
};

static int
MutateTask_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct MutateTask* self = (struct MutateTask*)base;
    (void)io;

    PT_BEGIN(&self->pt);

    /* Intermediate summoning-sidebar state: the equipment elements are gone,
     * but the replacement familiar elements do not exist yet. */
    self->widgets->equipment_visible = 0;
    self->widgets->mutation_count++;

    if( self->wait_external )
    {
        self->px->pending = 1;
        self->px->pending_checks = 0;
        self->px->pending_watchdog = 16;
        PT_YIELD(&self->pt);
    }
    else
    {
        while( self->ready_yields < READY_YIELDS )
        {
            self->ready_yields++;
            PT_YIELD(&self->pt);
        }
    }

    /* Final state: only now is the alternate sidebar complete. */
    self->widgets->familiar_visible = 1;
    self->widgets->mutation_count++;
    self->widgets->completed = 1;

    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_mutate_vtable = {
    .run = MutateTask_Run,
    .free = NULL,
};

static struct ToriRS_Task*
new_mutate_task(
    struct WidgetState* widgets,
    struct PlatformX_IO* px,
    int wait_external)
{
    struct MutateTask* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &k_mutate_vtable;
    strcpy(task->task.name, "cs2-frame-settle");
    task->widgets = widgets;
    task->px = px;
    task->wait_external = wait_external;
    PT_INIT(&task->pt);
    return &task->task;
}

/* App_RunOnce's publication contract reduced to the part under test.  A
 * pending CS2 transaction returns no redraw, leaving the retained frame and
 * redraw latch exactly as they were. */
static int
settle_and_commit(
    struct TaskRunner* runner,
    struct WidgetState const* widgets,
    struct PublishedFrame* frame)
{
    if( TaskRunner_SettleFrame(runner) != TASK_RUNNER_IDLE )
        return 0;
    if( !frame->need_redraw )
        return 0;

    frame->equipment_visible = widgets->equipment_visible;
    frame->familiar_visible = widgets->familiar_visible;
    frame->commit_count++;
    frame->need_redraw = 0;
    return 1;
}

static void
fixture_init(
    struct TaskRunner* runner,
    struct PlatformX_IO* px,
    struct WidgetState* widgets,
    struct PublishedFrame* frame)
{
    memset(px, 0, sizeof(*px));
    memset(widgets, 0, sizeof(*widgets));
    memset(frame, 0, sizeof(*frame));

    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IO_New();
    runner->px = px;

    widgets->equipment_visible = 1;
    frame->equipment_visible = 1;
    frame->commit_count = 1; /* The stable frame already on screen. */
    frame->need_redraw = 1;
}

static void
fixture_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IO_Free(runner->io);
}

static void
test_ready_work_drains_without_cap(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px;
    struct WidgetState widgets;
    struct PublishedFrame frame;

    fixture_init(&runner, &px, &widgets, &frame);
    ToriRS_TaskQueue_Add(runner.queue, new_mutate_task(&widgets, &px, 0));

    /* One initiating frame must cross every ready yield, including the old
     * 64-step boundary, and may publish only the completed tree. */
    TEST_CHECK(settle_and_commit(&runner, &widgets, &frame) == 1);
    assert(px.process_calls == READY_YIELDS);
    assert(runner.queue->head == NULL);
    assert(widgets.completed == 1);
    assert(widgets.mutation_count == 2);
    assert(frame.commit_count == 2);
    assert(frame.equipment_visible == 0);
    assert(frame.familiar_visible == 1);
    assert(frame.need_redraw == 0);

    fixture_free(&runner);
    printf("ok - >64 ready CS2 yields settle before one final frame commit\n");
}

static void
test_external_wait_retains_last_frame(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px;
    struct WidgetState widgets;
    struct PublishedFrame frame;

    fixture_init(&runner, &px, &widgets, &frame);
    ToriRS_TaskQueue_Add(runner.queue, new_mutate_task(&widgets, &px, 1));

    /* The task has already hidden Equipment when the external wait begins.
     * That partial state must not replace the stable published frame. */
    TEST_CHECK(settle_and_commit(&runner, &widgets, &frame) == 0);
    assert(px.pending == 1);
    assert(px.pending_checks > 0 && px.pending_checks < px.pending_watchdog);
    assert(runner.queue->head != NULL);
    assert(widgets.equipment_visible == 0);
    assert(widgets.familiar_visible == 0);
    assert(widgets.completed == 0);
    assert(frame.commit_count == 1);
    assert(frame.equipment_visible == 1);
    assert(frame.familiar_visible == 0);
    assert(frame.need_redraw == 1);

    /* Delivery resumes the same transaction.  Its first settled frame is the
     * final familiar view, published once; no mixed state was committed. */
    px.pending = 0;
    TEST_CHECK(settle_and_commit(&runner, &widgets, &frame) == 1);
    assert(runner.queue->head == NULL);
    assert(widgets.completed == 1);
    assert(widgets.mutation_count == 2);
    assert(frame.commit_count == 2);
    assert(frame.equipment_visible == 0);
    assert(frame.familiar_visible == 1);
    assert(frame.need_redraw == 0);

    fixture_free(&runner);
    printf("ok - external wait retains the prior frame until final CS2 state\n");
}

/*
 * Cross-queue wait: the layout-switch deadlock.
 *
 * IF_OPENTOP starts a tree rebuild on app->runner and flips the app into
 * APP_STATE_BOOTING; the IF_OPENSUB mounts that follow it in the same packet
 * burst land on app->exec_runner and must wait for that rebuild. Only the
 * frame loop steps app->runner, so the exec settle has to END rather than keep
 * stepping — a ready-yield loop here is a busy-wait no amount of stepping can
 * break, and it froze the client on every Display-panel layout change.
 *
 * TASK_AWAIT_STATE is what makes the two distinguishable, and process_calls is
 * what proves the settle stopped: before the fix this test would not fail, it
 * would hang.
 */
struct BootState
{
    int booting;
    int mounted;
};

struct AwaitStateTask
{
    struct ToriRS_Task task;
    struct pt pt;
    struct BootState* boot;
};

static int
AwaitStateTask_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct AwaitStateTask* self = (struct AwaitStateTask*)base;
    (void)io;

    PT_BEGIN(&self->pt);
    TASK_AWAIT_STATE(base, &self->pt, !self->boot->booting);
    self->boot->mounted = 1;
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_await_state_vtable = {
    .run = AwaitStateTask_Run,
    .free = NULL,
};

static void
test_cross_queue_wait_ends_the_settle(void)
{
    struct TaskRunner runner;
    struct PlatformX_IO px;
    struct WidgetState widgets;
    struct PublishedFrame frame;
    struct BootState boot = { .booting = 1, .mounted = 0 };
    struct AwaitStateTask* task;

    fixture_init(&runner, &px, &widgets, &frame);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &k_await_state_vtable;
    strcpy(task->task.name, "await-state");
    task->boot = &boot;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(runner.queue, &task->task);

    /* Blocked, not pending: no read is outstanding, so PENDING would send the
     * settle loop straight back into the same task. */
    TEST_CHECK(TaskRunner_SettleFrame(&runner) == TASK_RUNNER_BLOCKED);
    assert(px.pending == 0);
    /* Exactly one pass. This is the anti-spin assertion. */
    assert(px.process_calls == 1);
    assert(runner.queue->head == &task->task);
    assert(boot.mounted == 0);

    /* A blocked task stays blocked while the state holds, and re-testing it
     * costs one pass per frame — not one per step. */
    TEST_CHECK(TaskRunner_SettleFrame(&runner) == TASK_RUNNER_BLOCKED);
    assert(px.process_calls == 2);
    assert(boot.mounted == 0);

    /* The other queue got its turn back and finished the rebuild. */
    boot.booting = 0;
    TEST_CHECK(TaskRunner_SettleFrame(&runner) == TASK_RUNNER_IDLE);
    assert(runner.queue->head == NULL);
    assert(boot.mounted == 1);

    fixture_free(&runner);
    printf("ok - cross-queue wait ends the settle instead of spinning\n");
}

int
main(void)
{
    test_ready_work_drains_without_cap();
    test_external_wait_retains_last_frame();
    test_cross_queue_wait_ends_the_settle();
    printf("cs2-frame-settle: all tests passed\n");
    return 0;
}
