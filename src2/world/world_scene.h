#ifndef WORLD_SCENE_H
#define WORLD_SCENE_H

#include "world_scene_events.h"

#include <stdbool.h>

struct WorldScene;

struct WorldSceneElementHandle
{
    struct WorldScene* scene;
    int id;
};

struct WorldSceneBatchElementHandle
{
    struct WorldScene* scene;
    int batch_id;
    int id;
};

struct WorldScene*
world_scene_new(struct WorldScene_EventQueue* event_queue_ref);

void
world_scene_free(struct WorldScene* scene);

void
world_scene_batch_begin(struct WorldScene* scene);

struct WorldSceneBatchElementHandle
world_scene_batch_add_element(struct WorldScene* scene);

void
world_scene_batch_end(struct WorldScene* scene);

void
world_scene_batch_clear(
    struct WorldScene* scene,
    int batch_id);

int
world_scene_add_element(struct WorldScene* scene);

int
world_scene_remove_element(
    struct WorldScene* scene,
    int element_id);

void
world_scene_clear(struct WorldScene* scene);

#endif
