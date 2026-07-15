#include "runescape_cs2_queue.h"

#include <stdlib.h>
#include <string.h>

static void
rs_cs2_invoke_free_strs(struct GameRunescapeCS2Invoke* inv)
{
    if( !inv )
        return;
    for( int i = 0; i < inv->str_arg_count; i++ )
    {
        free(inv->str_args[i]);
        inv->str_args[i] = NULL;
    }
    inv->str_arg_count = 0;
}

void
GameRunescape_CS2QueueInit(struct GameRunescapeCS2Queue* q)
{
    if( !q )
        return;
    memset(q, 0, sizeof(*q));
}

void
GameRunescape_CS2QueueFree(struct GameRunescapeCS2Queue* q)
{
    if( !q )
        return;
    while( q->count > 0 )
        GameRunescape_CS2QueuePop(q);
    memset(q, 0, sizeof(*q));
}

bool
GameRunescape_CS2QueueEnqueue(
    struct GameRunescapeCS2Queue* q,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count)
{
    if( !q || q->count >= RS_CS2_QUEUE_MAX )
        return false;
    if( int_arg_count < 0 || int_arg_count > RS_CS2_QUEUE_INT_ARG_MAX )
        return false;
    if( str_arg_count < 0 || str_arg_count > RS_CS2_QUEUE_STR_ARG_MAX )
        return false;

    struct GameRunescapeCS2Invoke* inv = &q->entries[q->tail];
    memset(inv, 0, sizeof(*inv));
    inv->script_id = script_id;
    inv->component_id = component_id;
    inv->int_arg_count = int_arg_count;
    if( int_arg_count > 0 && int_args )
        memcpy(inv->int_args, int_args, (size_t)int_arg_count * sizeof(int));

    for( int i = 0; i < str_arg_count; i++ )
    {
        char const* src = (str_args && str_args[i]) ? str_args[i] : "";
        inv->str_args[i] = strdup(src);
        if( !inv->str_args[i] )
        {
            rs_cs2_invoke_free_strs(inv);
            return false;
        }
        inv->str_arg_count++;
    }

    q->tail = (q->tail + 1) % RS_CS2_QUEUE_MAX;
    q->count++;
    return true;
}

struct GameRunescapeCS2Invoke const*
GameRunescape_CS2QueuePeek(struct GameRunescapeCS2Queue const* q)
{
    if( !q || q->count <= 0 )
        return NULL;
    return &q->entries[q->head];
}

bool
GameRunescape_CS2QueuePop(struct GameRunescapeCS2Queue* q)
{
    if( !q || q->count <= 0 )
        return false;
    rs_cs2_invoke_free_strs(&q->entries[q->head]);
    memset(&q->entries[q->head], 0, sizeof(q->entries[q->head]));
    q->head = (q->head + 1) % RS_CS2_QUEUE_MAX;
    q->count--;
    return true;
}

bool
GameRunescape_CS2QueueIsEmpty(struct GameRunescapeCS2Queue const* q)
{
    return !q || q->count <= 0;
}

int
GameRunescape_CS2QueueCount(struct GameRunescapeCS2Queue const* q)
{
    return q ? q->count : 0;
}
