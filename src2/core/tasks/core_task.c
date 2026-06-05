#include "core_task.h"

#include "../../games/model_viewer.h"
#include "dat1_model_load.h"
#include "osrs/revconfig/revconfig.h"
#include "revconfig_load.h"

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

struct CoreTask*
core_task_new_revconfig_load(
    struct GameHandle game,
    char const* filename_1,
    char const* filename_2,
    char const* filename_3,
    char const* filename_4)
{
    revconfig_task_state_t* state = malloc(sizeof(revconfig_task_state_t));
    if( !state )
        return NULL;

    PT_STATE_INIT(state);
    (void)game;

    for( int i = 0; i < REVCONFIG_MAX_FILES; i++ )
        state->revconfig_filenames[i][0] = '\0';

    if( filename_1 )
    {
        strncpy(
            state->revconfig_filenames[0], filename_1, sizeof(state->revconfig_filenames[0]) - 1);
        state->revconfig_filenames[0][sizeof(state->revconfig_filenames[0]) - 1] = '\0';
    }
    if( filename_2 )
    {
        strncpy(
            state->revconfig_filenames[1], filename_2, sizeof(state->revconfig_filenames[1]) - 1);
        state->revconfig_filenames[1][sizeof(state->revconfig_filenames[1]) - 1] = '\0';
    }
    if( filename_3 )
    {
        strncpy(
            state->revconfig_filenames[2], filename_3, sizeof(state->revconfig_filenames[2]) - 1);
        state->revconfig_filenames[2][sizeof(state->revconfig_filenames[2]) - 1] = '\0';
    }
    if( filename_4 )
    {
        strncpy(
            state->revconfig_filenames[3], filename_4, sizeof(state->revconfig_filenames[3]) - 1);
        state->revconfig_filenames[3][sizeof(state->revconfig_filenames[3]) - 1] = '\0';
    }

    struct CoreTask* task = malloc(sizeof(struct CoreTask));
    if( !task )
    {
        free(state);
        return NULL;
    }

    task->kind = CORE_TASK_KIND_REVCONFIG_LOAD;
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
    case CORE_TASK_KIND_REVCONFIG_LOAD:
        return revconfig_task((revconfig_task_state_t*)task->state, ctx);
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
    case CORE_TASK_KIND_REVCONFIG_LOAD:
    {
        revconfig_task_state_t* state = (revconfig_task_state_t*)task->state;
        if( state )
        {
            if( state->revconfig_buffer )
                revconfig_buffer_free(state->revconfig_buffer);
            free(state);
        }
        break;
    }
    default:
        break;
    }

    free(task);
}
