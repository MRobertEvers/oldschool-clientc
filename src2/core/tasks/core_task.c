#include "core_task.h"

#include <stdlib.h>
#include <string.h>

struct CoreTask*
core_task_new(
    void* state,
    CoreTaskFunction task,
    CoreTaskDestructor destroy)
{
    struct CoreTask* task_state = malloc(sizeof(struct CoreTask));
    if( !task_state )
        return NULL;
    memset(task_state, 0, sizeof(struct CoreTask));
    task_state->state = state;
    task_state->task = task;
    task_state->destroy = destroy;
    return task_state;
}

void
core_task_free(struct CoreTask* task)
{
    if( !task )
        return;
    if( task->destroy && task->state )
        task->destroy(task->state);
    free(task);
}