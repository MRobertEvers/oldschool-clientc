#ifndef GAMETASK_SCENE_LOAD_H
#define GAMETASK_SCENE_LOAD_H

#include "osrs/cache.h"
#include "osrs/gameio.h"

struct GameTaskSceneLoad;

struct GameTaskSceneLoad*
gametask_scene_load_new(struct GameIO* io, struct Cache* cache, int chunk_x, int chunk_y);

void gametask_scene_load_free(struct GameTaskSceneLoad* task);

enum GameIOStatus gametask_scene_load_send(struct GameTaskSceneLoad* task);

#endif