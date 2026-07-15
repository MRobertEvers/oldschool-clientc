#include "ui_scene_events.h"

#include <assert.h>
#include <string.h>

void
uiscene_eventqueue_clear(struct UISceneEventQueue* queue)
{
    assert(queue);
    queue->count = 0;
}

bool
uiscene_eventqueue_push(
    struct UISceneEventQueue* queue,
    const struct UISceneEvent* event)
{
    assert(queue);
    assert(event);
    if( queue->count >= UI_SCENE_EVENT_QUEUE_MAX_SIZE )
        return false;
    queue->events[queue->count++] = *event;
    return true;
}
