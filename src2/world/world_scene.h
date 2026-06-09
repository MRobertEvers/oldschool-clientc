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
    /** When true, WorldScene frees model.u.model on replace/remove/clear. */
    bool owns_model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Animation* secondary_animation;
    struct ToriDraw_Position world_position;
    /** Sequence id from loc config; -1 when not animated. */
    int anim_seq_id;
    int anim_frame;
    int anim_cycle;
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

void
world_scene_batch_element_add_pose(
    struct WorldScene* scene,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle baked);

void
world_scene_free_pending_pose_copies(struct WorldScene* scene);

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

/** Transfers ownership of model.u.model to the scene (freed on replace/remove/clear). */
void
world_scene_element_set_model_owned(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model);

void
world_scene_element_set_animation(
    struct WorldScene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary);

void
world_scene_element_set_animation_seq(
    struct WorldScene* scene,
    int element_id,
    int seq_id);

void
world_scene_element_set_position(
    struct WorldScene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int yaw);

int
world_scene_get_slot_count(struct WorldScene* scene);

bool
world_scene_element_is_live(
    struct WorldScene* scene,
    int element_id);

struct WorldScene_EventQueue*
world_scene_get_event_queue(struct WorldScene* scene);

#endif
