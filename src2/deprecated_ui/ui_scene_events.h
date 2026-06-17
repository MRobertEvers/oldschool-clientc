#ifndef UI_SCENE_EVENTS_H
#define UI_SCENE_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

#define UI_SCENE_EVENT_QUEUE_MAX_SIZE 4096

enum UISceneEventKind
{
    UISCENE_EVENT_NONE = 0,
    UISCENE_EVENT_ELEMENT_ACQUIRED,
    UISCENE_EVENT_ELEMENT_RELEASED,
};

struct UISceneEvent
{
    enum UISceneEventKind kind;
    int element_id;
};

struct UISceneEventQueue
{
    struct UISceneEvent events[UI_SCENE_EVENT_QUEUE_MAX_SIZE];
    int count;
};

void
uiscene_eventqueue_clear(struct UISceneEventQueue* queue);

bool
uiscene_eventqueue_push(
    struct UISceneEventQueue* queue,
    const struct UISceneEvent* event);

#endif
