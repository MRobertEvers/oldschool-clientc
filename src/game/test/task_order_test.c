/*
 * Serial-FIFO ordering guarantee for the packet-exec pipeline. The whole
 * async-exec design rests on ToriRS_TaskQueue_Run only ever running the HEAD
 * task: a task that yields for IO must fully complete before the next
 * enqueued task runs, even though the second task needs no IO. This models a
 * PLAYER_INFO packet (cold cache -> yields) enqueued between two VARP writes,
 * and asserts the writes land in wire order.
 */
#include "asyncio.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared log the tasks append to, so we can assert execution order. */
static int g_log[16];
static int g_log_count;

static void
log_append(int value)
{
    if( g_log_count < 16 )
        g_log[g_log_count++] = value;
}

/* A task that writes `value` immediately (no IO). */
struct WriteTask
{
    struct ToriRS_Task task;
    struct pt pt;
    int value;
};

static int
WriteTask_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct WriteTask* self = (struct WriteTask*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    log_append(self->value);
    PT_END(&self->pt);
}

/* A task that yields once (simulating a cache IO await) before writing. */
struct YieldTask
{
    struct ToriRS_Task task;
    struct pt pt;
    int value;
    int yielded;
};

static int
YieldTask_Run(struct ToriRS_Task* base, struct ToriRS_IO* io)
{
    struct YieldTask* self = (struct YieldTask*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    self->yielded = 1;
    PT_YIELD(&self->pt);
    log_append(self->value);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_write_vtable = { .run = WriteTask_Run, .free = NULL };
static struct ToriRS_TaskVTable k_yield_vtable = { .run = YieldTask_Run, .free = NULL };

static struct ToriRS_Task*
new_write(int value)
{
    struct WriteTask* t = calloc(1, sizeof(*t));
    t->task.vtable = &k_write_vtable;
    t->value = value;
    PT_INIT(&t->pt);
    return &t->task;
}

static struct ToriRS_Task*
new_yield(int value)
{
    struct YieldTask* t = calloc(1, sizeof(*t));
    t->task.vtable = &k_yield_vtable;
    t->value = value;
    PT_INIT(&t->pt);
    return &t->task;
}

int
main(void)
{
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct ToriRS_IO* io = ToriRS_IO_New();

    /* Enqueue in wire order: VARP(1), PLAYER_INFO(yields), VARP(2). */
    ToriRS_TaskQueue_Add(queue, new_write(1));
    ToriRS_TaskQueue_Add(queue, new_yield(99));
    ToriRS_TaskQueue_Add(queue, new_write(2));

    /* First run: WriteTask 1 completes, YieldTask runs and yields (head-only
     * execution returns YIELD without touching the trailing write). */
    int stat = ToriRS_TaskQueue_Run(queue, io);
    assert(stat == TORIRS_ASYNCIO_STAT_YIELD);
    assert(g_log_count == 1 && g_log[0] == 1);
    /* The trailing VARP(2) must NOT have run while the yield is pending. */
    printf("ok - head-only: trailing task blocked behind the yield\n");

    /* Second run: YieldTask resumes and completes, THEN VARP(2). */
    stat = ToriRS_TaskQueue_Run(queue, io);
    assert(stat == TORIRS_ASYNCIO_STAT_DONE);
    assert(g_log_count == 3);
    assert(g_log[0] == 1 && g_log[1] == 99 && g_log[2] == 2);
    printf("ok - serial FIFO order preserved across the IO yield\n");

    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    printf("task-order: all tests passed\n");
    return 0;
}
