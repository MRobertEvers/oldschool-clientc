#include "world_scene_events.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct WorldScene_EventQueue*
worldscene_eventqueue_new(void)
{
    struct WorldScene_EventQueue* queue = malloc(sizeof(struct WorldScene_EventQueue));
    if( !queue )
        return NULL;
    memset(queue, 0, sizeof(struct WorldScene_EventQueue));
    return queue;
}

void
worldscene_eventqueue_free(struct WorldScene_EventQueue* queue)
{
    if( !queue )
        return;
    free(queue);
}

void
worldscene_eventqueue_clear(struct WorldScene_EventQueue* queue)
{
    if( !queue )
        return;
    queue->count = 0;
}

bool
worldscene_eventqueue_is_empty(struct WorldScene_EventQueue* queue)
{
    if( !queue )
        return true;
    return queue->count == 0;
}

bool
worldscene_eventqueue_push(
    struct WorldScene_EventQueue* queue,
    const struct WorldScene_Event* event)
{
    if( !queue || !event )
        return false;
    if( queue->count >= WORLD_SCENE_EVENT_QUEUE_MAX_SIZE )
        return false;

    queue->events[queue->count] = *event;
    queue->count++;
    return true;
}
