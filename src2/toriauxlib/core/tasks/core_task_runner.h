#ifndef CORE_TASKS_CORE_TASK_RUNNER_H
#define CORE_TASKS_CORE_TASK_RUNNER_H

#include "core_task.h"

#include <stdbool.h>

#define CORE_TASK_RUNNER_MAX_TASKS 64

struct CoreTaskRunner;

struct CoreTaskRunner*
core_task_runner_new(struct LibToriRS_IOQueue* io_queue);

void
core_task_runner_free(struct CoreTaskRunner* runner);

void
core_task_runner_add(
    struct CoreTaskRunner* runner,
    void* task_state,
    CoreTaskFunction task_function,
    CoreTaskDestructor destroy);

bool
core_task_runner_run(struct CoreTaskRunner* runner);

bool
core_task_runner_has_live(struct CoreTaskRunner* runner);

struct LibToriRS_IOQueue*
core_task_runner_io_queue(struct CoreTaskRunner* runner);

#endif
