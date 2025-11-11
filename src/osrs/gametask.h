#ifndef GAMETASK_H
#define GAMETASK_H

#include "osrs/async/gametask_scene_load.h"

enum GameTaskKind
{
    E_GAME_TASK_SCENE_LOAD,
};

struct GameTask
{
    struct GameTask* next;

    enum GameTaskKind kind;

    union
    {
        struct GameTaskSceneLoad* _scene_load;
    };
};

void gametask_scene_load(
    struct GameTask** list, struct GameIO* input, struct Cache* cache, int chunk_x, int chunk_y);

#endif