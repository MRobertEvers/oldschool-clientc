#include "gametask.h"

#include "osrs/async/gametask_scene_load.h"

#include <stdlib.h>
#include <string.h>

void
gametask_new_scene_load(
    struct GameTask** list, struct GameIO* input, struct Cache* cache, int chunk_x, int chunk_y)
{
    struct GameTask* task = NULL;
    struct GameTask* iter = NULL;

    struct GameTaskSceneLoad* task_scene_load =
        gametask_scene_load_new(input, cache, chunk_x, chunk_y);

    task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->next = NULL;
    task->kind = E_GAME_TASK_SCENE_LOAD;
    task->_scene_load = task_scene_load;

    if( !(*list) )
        *list = task;
    else
    {
        iter = (*list);
        while( iter->next )
            iter = iter->next;
        iter->next = task;
    }
}

void
gametask_new_cache_load(struct GameTask** list, struct GameIO* input, struct Cache* cache)
{
    struct GameTask* task = NULL;
    struct GameTask* iter = NULL;

    struct GameTaskCacheLoad* task_cache_load = gametask_cache_load_new(input, cache);

    task = malloc(sizeof(struct GameTask));
    memset(task, 0, sizeof(struct GameTask));
    task->next = NULL;
    task->kind = E_GAME_TASK_CACHE_LOAD;
    task->_cache_load = task_cache_load;

    if( !(*list) )
        *list = task;
    else
    {
        iter = (*list);
        while( iter->next )
            iter = iter->next;
        iter->next = task;
    }
}

enum GameIOStatus
gametask_send(struct GameTask* task)
{
    switch( task->kind )
    {
    case E_GAME_TASK_SCENE_LOAD:
        return gametask_scene_load_send(task->_scene_load);
    case E_GAME_TASK_CACHE_LOAD:
        return gametask_cache_load_send(task->_cache_load);
    }

    return E_GAMEIO_STATUS_ERROR;
}

void
gametask_free(struct GameTask* task)
{
    switch( task->kind )
    {
    case E_GAME_TASK_SCENE_LOAD:
        gametask_scene_load_free(task->_scene_load);
        break;
    case E_GAME_TASK_CACHE_LOAD:
        gametask_cache_load_free(task->_cache_load);
        break;
    }

    free(task);
}