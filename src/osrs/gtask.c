#include "gtask.h"

#include "gtask_init_io.h"
#include "gtask_init_scene.h"

#include <stdlib.h>
#include <string.h>

struct GTask*
gtask_new_init_io(struct GIOQueue* io)
{
    struct GTask* task = malloc(sizeof(struct GTask));
    memset(task, 0, sizeof(struct GTask));
    task->status = GTASK_STATUS_PENDING;
    task->kind = GTASK_KIND_INIT_IO;
    task->_init_io = gtask_init_io_new(io);
    return task;
}

enum GTaskStatus
gtask_step(struct GTask* task)
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

struct GTask*
gtask_new_init_scene(struct GIOQueue* io, int chunk_x, int chunk_y)
{
    struct GTask* task = malloc(sizeof(struct GTask));
    memset(task, 0, sizeof(struct GTask));
    task->status = GTASK_STATUS_PENDING;
    task->kind = GTASK_KIND_INIT_SCENE;
    task->_init_scene = gtask_init_scene_new(io, chunk_x, chunk_y, 104);
    return task;
}

void
gtask_free(struct GTask* task)
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