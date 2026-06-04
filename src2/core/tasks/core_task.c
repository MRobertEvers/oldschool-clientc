#include "core_task.h"

#include "../../games/model_viewer.h"
#include "dat1_model_load.h"

#include <stdlib.h>
#include <string.h>

struct CoreTask*
core_task_new_dat1_model_load(
    struct GameHandle game,
    int model_id)
{
    dat1_model_load_task_state_t* state = malloc(sizeof(dat1_model_load_task_state_t));
    if( !state )
        return NULL;

    PT_STATE_INIT(state);
    state->game = game;
    state->model_id = model_id;

    struct CoreTask* task = malloc(sizeof(struct CoreTask));
    if( !task )
    {
        free(state);
        return NULL;
    }

    task->kind = CORE_TASK_KIND_DAT1_MODEL_LOAD;
    task->state = state;
    return task;
}

PT_Status
core_task_step(
    struct CoreTask* task,
    struct LibToriRS_IOContext* ctx)
{
    if( !task )
        return PT_FINISHED;

    switch( task->kind )
    {
    case CORE_TASK_KIND_DAT1_MODEL_LOAD:
        return dat1_model_load_task((dat1_model_load_task_state_t*)task->state, ctx);
    default:
        return PT_FINISHED;
    }
}

void
core_task_free(struct CoreTask* task)
{
    if( !task )
        return;

    switch( task->kind )
    {
    case CORE_TASK_KIND_DAT1_MODEL_LOAD:
        free(task->state);
        break;
    default:
        break;
    }

    free(task);
}
