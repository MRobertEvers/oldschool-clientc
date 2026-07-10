#include "libtori_core_task_runner.h"

#include "3rd/minipt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
runner_slot_remove(
    struct LibToriCoreTaskRunner* runner,
    int task_idx)
{
    struct LibToriCoreTaskRunnerSlot* task = &runner->slots[task_idx];

    if( task->prev != -1 )
        runner->slots[task->prev].next = task->next;
    else
        runner->live_head = task->next;

    if( task->next != -1 )
        runner->slots[task->next].prev = task->prev;

    task->task = NULL;
    task->wait_run = -1;
    task->next = runner->free_head;
    task->prev = -1;
    runner->free_head = task_idx;
}

void
LibToriCoreTaskRunner_Init(
    struct LibToriCoreTaskRunner* runner,
    struct LibToriRS_IOQueue* io_queue)
{
    assert(runner);
    memset(runner, 0, sizeof(*runner));

    runner->io_queue = io_queue;
    runner->live_head = -1;
    runner->free_head = -1;

    for( int i = 0; i < LIBTORI_CORE_TASK_RUNNER_MAX_TASKS; i++ )
    {
        runner->slots[i].task = NULL;
        runner->slots[i].wait_run = -1;
        runner->slots[i].prev = -1;
        runner->slots[i].next = runner->free_head;
        runner->free_head = i;
    }
}

void
LibToriCoreTaskRunner_Shutdown(struct LibToriCoreTaskRunner* runner)
{
    if( !runner )
        return;

    while( runner->live_head != -1 )
    {
        struct LibToriCoreTaskRunnerSlot* task = &runner->slots[runner->live_head];
        if( task->task )
            LibToriCoreTask_Free(task->task);
        runner_slot_remove(runner, runner->live_head);
    }
}

struct LibToriCoreTaskRunner*
LibToriCoreTaskRunner_New(struct LibToriRS_IOQueue* io_queue)
{
    struct LibToriCoreTaskRunner* runner = calloc(1, sizeof(struct LibToriCoreTaskRunner));
    if( !runner )
        return NULL;

    LibToriCoreTaskRunner_Init(runner, io_queue);
    return runner;
}

void
LibToriCoreTaskRunner_Free(struct LibToriCoreTaskRunner* runner)
{
    if( !runner )
        return;

    LibToriCoreTaskRunner_Shutdown(runner);
    free(runner);
}

void
LibToriCoreTaskRunner_Add(
    struct LibToriCoreTaskRunner* runner,
    void* task_state,
    LibToriCoreTaskFunction task_function,
    LibToriCoreTaskDestructor destroy)
{
    assert(runner);
    if( runner->free_head == -1 )
    {
        fprintf(stderr, "LibToriCoreTaskRunner: no free task slots\n");
        assert(0);
        return;
    }

    struct LibToriCoreTask* task = LibToriCoreTask_New(task_state, task_function, destroy);
    if( !task )
    {
        fprintf(stderr, "LibToriCoreTaskRunner: failed to create task\n");
        assert(0);
        return;
    }

    int task_idx = runner->free_head;
    struct LibToriCoreTaskRunnerSlot* new_task = &runner->slots[task_idx];
    runner->free_head = new_task->next;

    new_task->task = task;
    new_task->wait_run = -1;
    new_task->next = runner->live_head;
    new_task->prev = -1;

    if( runner->live_head != -1 )
        runner->slots[runner->live_head].prev = task_idx;

    runner->live_head = task_idx;
}

bool
LibToriCoreTaskRunner_Run(struct LibToriCoreTaskRunner* runner)
{
    if( !runner || runner->live_head == -1 )
        return false;

    struct LibToriRS_IOContext ctx = {
        .io = runner->io_queue,
    };

    int task_idx = runner->live_head;
    struct LibToriCoreTaskRunnerSlot* task = &runner->slots[task_idx];

    if( task->wait_run >= 0 && !LibToriRS_IOQueueRunComplete(runner->io_queue, task->wait_run) )
        return true;

    int run = LibToriRS_IOQueueBeginRun(runner->io_queue);
    task->last_res = task->task->task(task->task->state, &ctx);

    switch( task->last_res )
    {
    case PT_YIELDED:
        task->wait_run = run;
        break;
    case PT_EXITED:
    case PT_ENDED:
        LibToriCoreTask_Free(task->task);
        task->task = NULL;
        task->wait_run = -1;
        runner_slot_remove(runner, task_idx);
        break;
    default:
        break;
    }

    return runner->live_head != -1;
}

bool
LibToriCoreTaskRunner_HasLive(struct LibToriCoreTaskRunner* runner)
{
    return runner && runner->live_head != -1;
}

struct LibToriRS_IOQueue*
LibToriCoreTaskRunner_IOQueue(struct LibToriCoreTaskRunner* runner)
{
    return runner ? runner->io_queue : NULL;
}
