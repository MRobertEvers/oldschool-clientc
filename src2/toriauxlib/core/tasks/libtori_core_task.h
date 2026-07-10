#ifndef CORE_TASKS_LIBTORI_CORE_TASK_H
#define CORE_TASKS_LIBTORI_CORE_TASK_H

#include "../../../ioqueue/libtorirs_ioqueue.h"

typedef int (*LibToriCoreTaskFunction)(
    void* state,
    struct LibToriRS_IOContext* ctx);

typedef void (*LibToriCoreTaskDestructor)(void* state);

struct LibToriCoreTask
{
    void* state;
    LibToriCoreTaskFunction task;
    LibToriCoreTaskDestructor destroy;
};

struct LibToriCoreTask*
LibToriCoreTask_New(
    void* state,
    LibToriCoreTaskFunction task,
    LibToriCoreTaskDestructor destroy);

void
LibToriCoreTask_Free(struct LibToriCoreTask* task);

#endif
