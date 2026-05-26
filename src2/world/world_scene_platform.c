#include "world_scene_platform.h"

#include "world_scene_events.h"

#include <stdio.h>

static const char*
worldscene_event_kind_name(enum WorldScene_EventKind kind)
{
    switch( kind )
    {
    case WSE_MODEL_LOAD:
        return "MODEL_LOAD";
    case WSE_MODEL_UNLOAD:
        return "MODEL_UNLOAD";
    case WSE_ANIM_LOAD:
        return "ANIM_LOAD";
    case WSE_ANIM_UNLOAD:
        return "ANIM_UNLOAD";
    case WSE_BATCH_BEGIN:
        return "BATCH_BEGIN";
    case WSE_BATCH_MODEL_ADD:
        return "BATCH_MODEL_ADD";
    case WSE_BATCH_ANIM_ADD:
        return "BATCH_ANIM_ADD";
    case WSE_BATCH_END:
        return "BATCH_END";
    case WSE_BATCH_CLEAR:
        return "BATCH_CLEAR";
    case WSE_SCENE_RESET:
        return "SCENE_RESET";
    default:
        return "NONE";
    }
}

void
worldscene_platform_process_events(struct WorldScene_EventQueue* queue)
{
    if( !queue )
        return;

    for( int i = 0; i < queue->count; i++ )
    {
        const struct WorldScene_Event* ev = &queue->events[i];
        printf(
            "WorldScene event: %s batch_id=%d asset_id=%d model_kind=%d animation=%p\n",
            worldscene_event_kind_name(ev->kind),
            ev->batch_id,
            ev->element_id,
            (int)ev->model.kind,
            (void*)ev->animation);
    }

    worldscene_eventqueue_clear(queue);
}
