#ifndef GTASK_INIT_SCENE_U_C
#define GTASK_INIT_SCENE_U_C

#include "gtask.h"
#include "osrs/gio_assets.h"
#include "osrs/tables/model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum StepInitScene
{
    STEP_INIT_SCENE_INITIAL = 0,
    STEP_INIT_SCENE_LOAD_MODEL,
    STEP_INIT_SCENE_DONE,
};

struct GTaskInitScene
{
    enum StepInitScene step;
    struct GIOQueue* io;
    int chunk_x;
    int chunk_y;

    uint32_t reqid_model;
};

struct GTaskInitScene*
gtask_init_scene_new(struct GIOQueue* io, int chunk_x, int chunk_y)
{
    struct GTaskInitScene* task = malloc(sizeof(struct GTaskInitScene));
    memset(task, 0, sizeof(struct GTaskInitScene));
    task->step = STEP_INIT_SCENE_INITIAL;
    task->io = io;
    task->chunk_x = chunk_x;
    task->chunk_y = chunk_y;
    return task;
}

void
gtask_init_scene_free(struct GTaskInitScene* task)
{
    free(task);
}

enum GTaskStatus
gtask_init_scene_step(struct GTaskInitScene* task)
{
    struct GIOMessage message;
    struct CacheModel* model = NULL;

    switch( task->step )
    {
    case STEP_INIT_SCENE_INITIAL:
        goto step_one_initial;
    case STEP_INIT_SCENE_LOAD_MODEL:
        goto step_two_load_model;
    case STEP_INIT_SCENE_DONE:
        goto bad_step;
    default:
        goto bad_step;
    }

step_one_initial:;
    if( task->reqid_model == 0 )
        task->reqid_model = gio_assets_model_load(task->io, 0);

    if( !gioq_poll(task->io, &message) )
        return GTASK_STATUS_PENDING;
    assert(message.message_id == task->reqid_model);

    task->step = STEP_INIT_SCENE_LOAD_MODEL;
step_two_load_model:;

    model = (struct CacheModel*)gioq_adopt(task->io, &message, NULL);

    gioq_release(task->io, &message);

    task->step = STEP_INIT_SCENE_DONE;
    return GTASK_STATUS_COMPLETED;

bad_step:;
    assert(false && "Bad step in gtask_init_scene_step");
    return GTASK_STATUS_FAILED;
}

#endif