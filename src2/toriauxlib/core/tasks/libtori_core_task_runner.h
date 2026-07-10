#ifndef CORE_TASKS_LIBTORI_CORE_TASK_RUNNER_H
#define CORE_TASKS_LIBTORI_CORE_TASK_RUNNER_H

#include "libtori_core_task.h"

#include <stdbool.h>

#define LIBTORI_CORE_TASK_RUNNER_MAX_TASKS 64

struct LibToriCoreTaskRunnerSlot
{
    struct LibToriCoreTask* task;
    int last_res;
    int wait_run;
    int next;
    int prev;
};

struct LibToriCoreTaskRunner
{
    struct LibToriRS_IOQueue* io_queue;
    struct LibToriCoreTaskRunnerSlot slots[LIBTORI_CORE_TASK_RUNNER_MAX_TASKS];
    int live_head;
    int free_head;
};

void
LibToriCoreTaskRunner_Init(
    struct LibToriCoreTaskRunner* runner,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriCoreTaskRunner_Shutdown(struct LibToriCoreTaskRunner* runner);

struct LibToriCoreTaskRunner*
LibToriCoreTaskRunner_New(struct LibToriRS_IOQueue* io_queue);

void
LibToriCoreTaskRunner_Free(struct LibToriCoreTaskRunner* runner);

void
LibToriCoreTaskRunner_Add(
    struct LibToriCoreTaskRunner* runner,
    void* task_state,
    LibToriCoreTaskFunction task_function,
    LibToriCoreTaskDestructor destroy);

bool
LibToriCoreTaskRunner_Run(struct LibToriCoreTaskRunner* runner);

bool
LibToriCoreTaskRunner_HasLive(struct LibToriCoreTaskRunner* runner);

struct LibToriRS_IOQueue*
LibToriCoreTaskRunner_IOQueue(struct LibToriCoreTaskRunner* runner);

#endif
