#include "ui_resource_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct UIResourceQueue*
ui_resource_queue_new(void)
{
    return calloc(1, sizeof(struct UIResourceQueue));
}

void
ui_resource_queue_free(struct UIResourceQueue* queue)
{
    if( !queue )
        return;
    ui_resource_queue_clear(queue);
    free(queue);
}

void
ui_resource_queue_clear(struct UIResourceQueue* queue)
{
    if( !queue )
        return;

    for( int i = 0; i < queue->count; i++ )
    {
        struct UIResourceQueueItem* item = &queue->items[i];
        if( item->result_sprites )
        {
            for( int j = 0; j < item->result_count; j++ )
            {
                if( item->result_sprites[j] )
                    ToriDraw_SpriteFree(item->result_sprites[j]);
            }
            free(item->result_sprites);
            item->result_sprites = NULL;
            item->result_count = 0;
        }
    }
    queue->count = 0;
}

bool
ui_resource_queue_push_sprite(
    struct UIResourceQueue* queue,
    const struct UIResourceQueueItem* item)
{
    if( !queue || !item || queue->count >= UI_RESOURCE_QUEUE_MAX_SIZE )
        return false;

    struct UIResourceQueueItem* new_item = &queue->items[queue->count];
    *new_item = *item;
    new_item->status = UIRES_PENDING;
    new_item->state = UIRES_STATE_NEED_ARCHIVE;
    new_item->source_archive_index = -1;
    new_item->result_sprites = NULL;
    new_item->result_count = 0;
    new_item->error_code = 0;
    new_item->component_id = -1;
    queue->count++;
    return true;
}

bool
ui_resource_queue_push_rs_component(
    struct UIResourceQueue* queue,
    int component_id)
{
    if( !queue || queue->count >= UI_RESOURCE_QUEUE_MAX_SIZE )
        return false;

    struct UIResourceQueueItem* new_item = &queue->items[queue->count];
    memset(new_item, 0, sizeof(*new_item));
    new_item->kind = UIRES_KIND_RS_COMPONENT;
    new_item->status = UIRES_PENDING;
    new_item->state = UIRES_STATE_NEED_ARCHIVE;
    new_item->source_archive_index = -1;
    new_item->component_id = component_id;
    snprintf(new_item->name, sizeof(new_item->name), "rscomp:%d", component_id);
    queue->count++;
    return true;
}

struct UIResourceQueueItem*
ui_resource_queue_find_by_name(
    struct UIResourceQueue* queue,
    const char* name)
{
    if( !queue || !name )
        return NULL;

    for( int i = 0; i < queue->count; i++ )
    {
        if( strcmp(queue->items[i].name, name) == 0 )
            return &queue->items[i];
    }
    return NULL;
}
