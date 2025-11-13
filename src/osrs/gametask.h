#ifndef GAMETASK_H
#define GAMETASK_H

#include "osrs/async/gametask_cache_load.h"
#include "osrs/async/gametask_scene_load.h"

enum GameTaskKind
{
    E_GAME_TASK_SCENE_LOAD,
    E_GAME_TASK_CACHE_LOAD,
};

struct GameTask
{
    struct GameTask* next;

    enum GameTaskKind kind;

    union
    {
        struct GameTaskSceneLoad* _scene_load;
        struct GameTaskCacheLoad* _cache_load;
    };
};

void gametask_new_scene_load(
    struct GameTask** list, struct GameIO* input, struct Cache* cache, int chunk_x, int chunk_y);
void gametask_new_cache_load(struct GameTask** list, struct GameIO* input, struct Cache* cache);

enum GameIOStatus gametask_send(struct GameTask* task);
void gametask_free(struct GameTask* task);

#endif