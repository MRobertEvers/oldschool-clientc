#include "core_task.h"

#include <stdlib.h>
#include <string.h>

struct CoreTask*
core_task_new(
    void* state,
    CoreTaskFunction task)
{
    struct CoreTask* task_state = malloc(sizeof(struct CoreTask));
    if( !task_state )
        return NULL;
    memset(task_state, 0, sizeof(struct CoreTask));
    task_state->state = state;
    task_state->task = task;
    return task_state;
}

void
core_task_free(struct CoreTask* task)
{
    if( !task )
        return;
    free(task);
}