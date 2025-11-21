#include "gametask_scene_load.h"

#include "osrs/cache.h"
#include "osrs/scene.h"
#include "osrs/tables/maps.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum SceneLoadStep
{
    E_SCENE_LOAD_STEP_INITIAL = 0,
    E_SCENE_LOAD_STEP_MAP_TERRAIN,
    E_SCENE_LOAD_STEP_DONE,
};

struct GameTaskSceneLoad
{
    enum SceneLoadStep step;
    struct GameIORequest* request;
    struct GameIO* io;
    struct Cache* cache;
    struct Scene* scene;
    int chunk_x;
    int chunk_y;
};

struct GameTaskSceneLoad*
gametask_scene_load_new(struct GameIO* io, struct Cache* cache, int chunk_x, int chunk_y)
{
    struct GameTaskSceneLoad* task = malloc(sizeof(struct GameTaskSceneLoad));
    memset(task, 0, sizeof(struct GameTaskSceneLoad));
    task->step = E_SCENE_LOAD_STEP_INITIAL;
    task->cache = cache;
    task->scene = NULL;
    task->io = io;
    task->chunk_x = chunk_x;
    task->chunk_y = chunk_y;
    return task;
}

void
gametask_scene_load_free(struct GameTaskSceneLoad* task)
{
    free(task);
}

enum GameIOStatus
gametask_scene_load_send(struct GameTaskSceneLoad* task)
{
    enum GameIOStatus status = E_GAMEIO_STATUS_ERROR;
    struct CacheArchiveTuple archive_tuple = { 0 };
    struct GameIO* io = task->io;

    switch( task->step )
    {
    case E_SCENE_LOAD_STEP_INITIAL:
        goto initial;
    case E_SCENE_LOAD_STEP_MAP_TERRAIN:
        goto map_terrain;
    default:
        goto error;
    }

initial:;
    map_terrain_io(task->cache, task->chunk_x, task->chunk_y, &archive_tuple);
    status = gameio_request_new_archive_load(
        io, archive_tuple.table_id, archive_tuple.archive_id, &task->request);
    if( !gameio_resolved(status) )
        return status;
    task->step = E_SCENE_LOAD_STEP_MAP_TERRAIN;

map_terrain:

    printf("Scene load request resolved: %d\n", task->request->request_id);
    return E_GAMEIO_STATUS_OK;

error:
    printf("Async Task False Start: %d\n", task->step);
    return E_GAMEIO_STATUS_ERROR;
}

struct Scene*
gametask_scene_value(struct GameTaskSceneLoad* task)
{
    assert(task->step == E_SCENE_LOAD_STEP_DONE);
    assert(task->scene != NULL);
    return task->scene;
}