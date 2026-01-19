#include "gametask.h"

#include "gtask_init_io.h"
#include "gtask_init_scene.h"

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
    task->status = GTASK_STATUS_PENDING;
    task->kind = GTASK_KIND_INIT_IO;
    task->_init_io = gtask_init_io_new(io);

    append_task(game, task);

    return task;
}

enum GameTaskStatus
gametask_step(struct GameTask* task)
{
    switch( task->kind )
    {
    case GTASK_KIND_INIT_IO:
        return gtask_init_io_step(task->_init_io);
    case GTASK_KIND_INIT_SCENE:
        return gtask_init_scene_step(task->_init_scene);
    }

    return GTASK_STATUS_FAILED;
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
    task->status = GTASK_STATUS_PENDING;
    task->kind = GTASK_KIND_INIT_SCENE;
    task->_init_scene = gtask_init_scene_new(game, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    append_task(game, task);

    return task;
}

void
gametask_free(struct GameTask* task)
{
    switch( task->kind )
    {
    case GTASK_KIND_INIT_IO:
        gtask_init_io_free(task->_init_io);
        break;
    case GTASK_KIND_INIT_SCENE:
        gtask_init_scene_free(task->_init_scene);
        break;
    }

    free(task);
}