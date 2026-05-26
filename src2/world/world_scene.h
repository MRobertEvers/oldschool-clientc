#ifndef WORLD_SCENE_H
#define WORLD_SCENE_H

#include "toridraw/toridraw_types.h"
#include "world_scene_events.h"

#include <stdbool.h>

#define WORLD_SCENE_INVALID_BATCH_ID (-1)
#define WORLD_SCENE_INVALID_ELEMENT_ID (-1)

struct WorldScene;

struct WorldSceneElement
{
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Animation* secondary_animation;
};

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
world_scene_new(void);

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

struct WorldSceneElement*
world_scene_element_get(
    struct WorldScene* scene,
    int element_id);

struct WorldSceneElement*
world_scene_element_get_handle(struct WorldSceneElementHandle handle);

void
world_scene_element_set_model(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model);

void
world_scene_element_set_animation(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary);

struct WorldScene_EventQueue*
world_scene_get_event_queue(struct WorldScene* scene);

#endif
