#include "gametask.h"

#include "task_init_io.h"
#include "task_init_scene.h"
#include "task_init_scene_dat.h"
#include "task_load_dat.h"

#include <stdlib.h>
#include <string.h>

static void
append_task(
    struct GGame* game,
    struct GameTask* task)
{
    if( !game->tasks_nullable )
        game->tasks_nullable = task;
    else
    {
        struct GameTask* iter = game->tasks_nullable;
        while( iter && iter->next )
            iter = iter->next;

        if( iter )
            iter->next = task;
    }
}

struct GameTask*
gametask_new_init_io(
    struct GGame* game,
    struct GIOQueue* io)
{
    struct GameTask* task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->status = GAMETASK_STATUS_PENDING;
    task->kind = GAMETASK_KIND_INIT_IO;
    task->_init_io = task_init_io_new(io);

    append_task(game, task);

    return task;
}

enum GameTaskStatus
gametask_step(struct GameTask* task)
{
    switch( task->kind )
    {
    case GAMETASK_KIND_INIT_IO:
        return task_init_io_step(task->_init_io);
    case GAMETASK_KIND_INIT_SCENE:
        return task_init_scene_step(task->_init_scene);
    case GAMETASK_KIND_INIT_SCENE_DAT:
        return task_init_scene_dat_step(task->_init_scene_dat);
    case GAMETASK_KIND_LOAD_DAT:
        return task_load_dat_step(task->_load_dat);
    }

    return GAMETASK_STATUS_FAILED;
}

struct GameTask*
gametask_new_init_scene(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    struct GameTask* task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->status = GAMETASK_STATUS_PENDING;
    task->kind = GAMETASK_KIND_INIT_SCENE;
    task->_init_scene = task_init_scene_new(game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    append_task(game, task);

    return task;
}

struct GameTask*
gametask_new_init_scene_dat(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    struct GameTask* task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->status = GAMETASK_STATUS_PENDING;
    task->kind = GAMETASK_KIND_INIT_SCENE_DAT;
    task->_init_scene_dat = task_init_scene_dat_new(game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    append_task(game, task);

    return task;
}

struct GameTask*
gametask_new_load_dat(
    struct GGame* game,
    int* sequence_ids,
    int sequence_count,
    int* model_ids,
    int model_count)
{
    struct GameTask* task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->status = GAMETASK_STATUS_PENDING;
    task->kind = GAMETASK_KIND_LOAD_DAT;
    task->_load_dat = task_load_dat_new(game, sequence_ids, sequence_count, model_ids, model_count);

    append_task(game, task);

    return task;
}

void
gametask_free(struct GameTask* task)
{
    switch( task->kind )
    {
    case GAMETASK_KIND_INIT_IO:
        task_init_io_free(task->_init_io);
        break;
    case GAMETASK_KIND_INIT_SCENE:
        task_init_scene_free(task->_init_scene);
        break;
    case GAMETASK_KIND_INIT_SCENE_DAT:
        task_init_scene_dat_free(task->_init_scene_dat);
        break;
    case GAMETASK_KIND_LOAD_DAT:
        task_load_dat_free(task->_load_dat);
        break;
    }

    free(task);
}