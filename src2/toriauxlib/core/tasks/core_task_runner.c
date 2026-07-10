#include "core_task_runner.h"

#include "3rd/minipt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CoreTaskRunnerSlot
{
    struct CoreTask* task;
    int last_res;
    int wait_run;
    int next;
    int prev;
};

struct CoreTaskRunner
{
    struct LibToriRS_IOQueue* io_queue;
    struct CoreTaskRunnerSlot slots[CORE_TASK_RUNNER_MAX_TASKS];
    int live_head;
    int free_head;
};

static void
runner_slot_remove(
    struct CoreTaskRunner* runner,
    int task_idx)
{
    struct CoreTaskRunnerSlot* task = &runner->slots[task_idx];

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

struct CoreTaskRunner*
core_task_runner_new(struct LibToriRS_IOQueue* io_queue)
{
    struct CoreTaskRunner* runner = calloc(1, sizeof(struct CoreTaskRunner));
    if( !runner )
        return NULL;

    runner->io_queue = io_queue;
    runner->live_head = -1;
    runner->free_head = -1;

    for( int i = 0; i < CORE_TASK_RUNNER_MAX_TASKS; i++ )
    {
        runner->slots[i].task = NULL;
        runner->slots[i].wait_run = -1;
        runner->slots[i].prev = -1;
        runner->slots[i].next = runner->free_head;
        runner->free_head = i;
    }

    return runner;
}

void
core_task_runner_free(struct CoreTaskRunner* runner)
{
    if( !runner )
        return;

    while( runner->live_head != -1 )
    {
        struct CoreTaskRunnerSlot* task = &runner->slots[runner->live_head];
        if( task->task )
            core_task_free(task->task);
        runner_slot_remove(runner, runner->live_head);
    }

    free(runner);
}

void
core_task_runner_add(
    struct CoreTaskRunner* runner,
    void* task_state,
    CoreTaskFunction task_function,
    CoreTaskDestructor destroy)
{
    assert(runner);
    if( runner->free_head == -1 )
    {
        fprintf(stderr, "core_task_runner: no free task slots\n");
        assert(0);
        return;
    }

    struct CoreTask* task = core_task_new(task_state, task_function, destroy);
    if( !task )
    {
        fprintf(stderr, "core_task_runner: failed to create task\n");
        assert(0);
        return;
    }

    int task_idx = runner->free_head;
    struct CoreTaskRunnerSlot* new_task = &runner->slots[task_idx];
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
core_task_runner_run(struct CoreTaskRunner* runner)
{
    if( !runner || runner->live_head == -1 )
        return false;

    struct LibToriRS_IOContext ctx = {
        .io = runner->io_queue,
    };

    int task_idx = runner->live_head;
    struct CoreTaskRunnerSlot* task = &runner->slots[task_idx];

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
        core_task_free(task->task);
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
core_task_runner_has_live(struct CoreTaskRunner* runner)
{
    return runner && runner->live_head != -1;
}

struct LibToriRS_IOQueue*
core_task_runner_io_queue(struct CoreTaskRunner* runner)
{
    return runner ? runner->io_queue : NULL;
}
