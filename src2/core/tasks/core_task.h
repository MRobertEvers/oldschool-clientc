#ifndef CORE_TASKS_CORE_TASK_H
#define CORE_TASKS_CORE_TASK_H

#include "../ioqueue/libtorirs_ioqueue.h"

typedef int (*CoreTaskFunction)(
    void* state,
    struct LibToriRS_IOContext* ctx);

struct CoreTask
{
    void* state;
    CoreTaskFunction task;
};

struct CoreTask*
core_task_new(
    void* state,
    CoreTaskFunction task);

void
core_task_free(struct CoreTask* task);
#endif