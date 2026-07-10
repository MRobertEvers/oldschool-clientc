#include "libtori_core_task.h"

#include <stdlib.h>
#include <string.h>

struct LibToriCoreTask*
LibToriCoreTask_New(
    void* state,
    LibToriCoreTaskFunction task,
    LibToriCoreTaskDestructor destroy)
{
    struct LibToriCoreTask* task_state = malloc(sizeof(struct LibToriCoreTask));
    if( !task_state )
        return NULL;
    memset(task_state, 0, sizeof(struct LibToriCoreTask));
    task_state->state = state;
    task_state->task = task;
    task_state->destroy = destroy;
    return task_state;
}

void
LibToriCoreTask_Free(struct LibToriCoreTask* task)
{
    if( !task )
        return;
    if( task->destroy && task->state )
        task->destroy(task->state);
    free(task);
}
