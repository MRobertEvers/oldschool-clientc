#include "task_load_models_dat.h"

#include "datastruct/vec.h"
#include "osrs/gio.h"
#include "osrs/gio_assets.h"
#include "osrs/rscache/tables/model.h"

#include <stdlib.h>
#include <string.h>

enum TaskStepKind
{
    TS_GATHER,
    TS_POLL,
    TS_PROCESS,
    TS_DONE,
};

struct TaskStep
{
    enum TaskStepKind step;
};

static void
vec_push_unique(
    struct Vec* vec,
    int* element)
{
    struct VecIter* iter = vec_iter_new(vec);
    int* element_iter = NULL;

    while( (element_iter = (int*)vec_iter_next(iter)) )
    {
        if( *element_iter == *element )
            goto done;
    }
    vec_push(vec, element);

done:;
    vec_iter_free(iter);
}

struct TaskLoadDat
{
    enum TaskLoadDatStep step;
    struct GGame* game;
    struct GIOQueue* io;
    struct TaskStep task_steps[STEP_LOAD_DAT_STEP_COUNT];

    int* model_ids;
    int model_count;

    struct Vec* queued_texture_ids_vec;
    struct Vec* reqid_queue_vec;
    int reqid_queue_inflight_count;

    struct FileListDat* config_jagfile;
    struct FileListDat* versionlist_jagfile;
};

struct TaskLoadDat*
task_load_dat_new(
    struct GGame* game,
    int* model_ids,
    int model_count)
{
    struct TaskLoadDat* task = malloc(sizeof(struct TaskLoadDat));
    memset(task, 0, sizeof(struct TaskLoadDat));
    task->game = game;

    task->model_ids = malloc(sizeof(int) * model_count);
    memcpy(task->model_ids, model_ids, sizeof(int) * model_count);
    task->model_count = model_count;

    task->reqid_queue_vec = vec_new(sizeof(int), 512);
    task->queued_texture_ids_vec = vec_new(sizeof(int), 512);

    return task;
}

void
task_load_dat_free(struct TaskLoadDat* task)
{
    free(task);
}

void
step_models_load_gather(struct TaskLoadDat* task)
{
    int reqid = 0;
    vec_clear(task->reqid_queue_vec);
    for( int i = 0; i < task->model_count; i++ )
    {
        reqid = gio_assets_dat_models_load(task->game->io, task->model_ids[i]);
        vec_push(task->reqid_queue_vec, &reqid);
    }
}

void
step_models_load_poll(
    struct TaskLoadDat* task,
    struct GIOMessage* message)
{
    struct CacheModel* model = model_new_decode(message->data, message->data_size);
    assert(model != NULL && "Model must be decoded");

    buildcachedat_add_model(task->game->buildcachedat, message->param_b, model);
}

static enum GameTaskStatus
step_models_load(struct TaskLoadDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_LOAD_DAT_LOAD_MODELS];

    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        step_models_load_gather(task);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);
        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->game->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            step_models_load_poll(task, &message);

            gioq_release(task->game->io, &message);
        }
        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;
        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }
    return GAMETASK_STATUS_COMPLETED;
}

enum GameTaskStatus
task_load_dat_step(struct TaskLoadDat* task)
{
    switch( task->step )
    {
    case STEP_LOAD_DAT_INITIAL:
    {
        task->step = STEP_LOAD_DAT_LOAD_MODELS;
    }
    case STEP_LOAD_DAT_LOAD_MODELS:
    {
        if( step_models_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_LOAD_DAT_DONE;
    }
    case STEP_LOAD_DAT_DONE:
    {
        task->game->awaiting_models--;
        if( task->game->awaiting_models == 0 )
        {
            task->game->build_player = 1;
        }
        return GAMETASK_STATUS_COMPLETED;
    }
    }
    return GAMETASK_STATUS_COMPLETED;
}
