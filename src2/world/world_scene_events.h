#ifndef WORLD_SCENE_EVENTS_H
#define WORLD_SCENE_EVENTS_H

#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Animation;

#define WORLD_SCENE_EVENT_QUEUE_MAX_SIZE 65536

enum WorldScene_EventKind
{
    WSE_NONE = 0,
    WSE_MODEL_LOAD,
    WSE_MODEL_UNLOAD,
    WSE_ANIM_LOAD,
    WSE_ANIM_UNLOAD,
    WSE_BATCH_BEGIN,
    WSE_BATCH_MODEL_ADD,
    WSE_BATCH_ANIM_ADD,
    WSE_BATCH_END,
    WSE_BATCH_CLEAR,
    WSE_SCENE_RESET,
};

struct WorldScene_Event
{
    enum WorldScene_EventKind kind;
    int batch_id;
    /* For WSE_MODEL_*, WSE_ANIM_*, WSE_BATCH_MODEL_ADD, WSE_BATCH_ANIM_ADD: element id. */
    int element_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
};

struct WorldScene_EventQueue
{
    struct WorldScene_Event events[WORLD_SCENE_EVENT_QUEUE_MAX_SIZE];
    int count;
};

struct WorldScene_EventQueue*
worldscene_eventqueue_new(void);

void
worldscene_eventqueue_free(struct WorldScene_EventQueue* queue);

void
worldscene_eventqueue_clear(struct WorldScene_EventQueue* queue);

bool
worldscene_eventqueue_is_empty(struct WorldScene_EventQueue* queue);

bool
worldscene_eventqueue_push(
    struct WorldScene_EventQueue* queue,
    const struct WorldScene_Event* event);

#endif
