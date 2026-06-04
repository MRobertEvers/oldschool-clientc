#include "ui_scene_events.h"

#include <string.h>

void
uiscene_eventqueue_clear(struct UISceneEventQueue* queue)
{
    if( !queue )
        return;
    queue->count = 0;
}

bool
uiscene_eventqueue_push(
    struct UISceneEventQueue* queue,
    const struct UISceneEvent* event)
{
    if( !queue || !event )
        return false;
    if( queue->count >= UI_SCENE_EVENT_QUEUE_MAX_SIZE )
        return false;
    queue->events[queue->count++] = *event;
    return true;
}
