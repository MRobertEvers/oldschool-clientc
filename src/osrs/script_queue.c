#include "script_queue.h"
#include <stdlib.h>
#include <string.h>

void
script_queue_init(struct ScriptQueue* q)
{
    q->head = NULL;
    q->tail = NULL;
}

void
script_queue_clear(struct ScriptQueue* q)
{
    struct ScriptQueueItem* it = q->head;
    while( it )
    {
        struct ScriptQueueItem* next = it->next;
        free(it);
        it = next;
    }
    q->head = NULL;
    q->tail = NULL;
}

struct ScriptQueueItem*
script_queue_push(struct ScriptQueue* q, const struct ScriptArgs* args)
{
    struct ScriptQueueItem* item = malloc(sizeof(struct ScriptQueueItem));
    if( !item )
        return NULL;
    memset(item, 0, sizeof(*item));
    if( args )
        item->args = *args;
    item->next = NULL;

    if( !q->tail )
    {
        q->head = item;
        q->tail = item;
    }
    else
    {
        q->tail->next = item;
        q->tail = item;
    }
    return item;
}

struct ScriptQueueItem*
script_queue_pop(struct ScriptQueue* q)
{
    struct ScriptQueueItem* item = q->head;
    if( !item )
        return NULL;
    q->head = item->next;
    if( q->tail == item )
        q->tail = NULL;
    item->next = NULL;
    return item;
}

int
script_queue_empty(const struct ScriptQueue* q)
{
    return q->head == NULL;
}

struct ScriptQueueItem*
script_queue_peek(const struct ScriptQueue* q)
{
    return q->head;
}

void
script_queue_free_item(struct ScriptQueueItem* item)
{
    free(item);
}
