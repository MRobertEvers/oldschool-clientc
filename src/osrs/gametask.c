#include "gametask.h"

#include "osrs/async/gametask_scene_load.h"

#include <stdlib.h>
#include <string.h>

void
gametask_scene_load(
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