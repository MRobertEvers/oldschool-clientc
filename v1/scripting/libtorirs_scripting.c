#include "libtorirs_scripting.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LibToriRS_ScriptQueue*
LibToriRS_ScriptQueueNew(void)
{
    struct LibToriRS_ScriptQueue* queue = malloc(sizeof(struct LibToriRS_ScriptQueue));
    if( !queue )
        return NULL;
    memset(queue, 0, sizeof(struct LibToriRS_ScriptQueue));
    return queue;
}

void
LibToriRS_ScriptQueueFree(struct LibToriRS_ScriptQueue* queue)
{
    if( !queue )
        return;
    free(queue);
    queue = NULL;
}

struct LibToriRS_Script*
LibToriRS_ScriptQueueEmplace(
    struct LibToriRS_ScriptQueue* queue,
    const char* name)
{
    assert(queue);
    if( queue->count >= LIBTORIRS_SCRIPT_QUEUE_MAX_SIZE )
        return NULL;
    if( !name )
        return NULL;
    if( strlen(name) >= LIBTORIRS_SCRIPT_NAME_MAX_SIZE )
        return NULL;

    struct LibToriRS_Script* script = &queue->values[queue->tail];
    strncpy(script->name, name, LIBTORIRS_SCRIPT_NAME_MAX_SIZE);
    queue->tail = (queue->tail + 1) % LIBTORIRS_SCRIPT_QUEUE_MAX_SIZE;
    queue->count++;
    script->args.count = 0;
    return script;
}

struct LibToriRS_Script*
LibToriRS_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue)
{
    assert(queue);
    if( queue->count == 0 )
        return NULL;
    struct LibToriRS_Script* script = &queue->values[queue->head];
    queue->head = (queue->head + 1) % LIBTORIRS_SCRIPT_QUEUE_MAX_SIZE;
    queue->count--;
    return script;
}

bool
LibToriRS_ScriptQueueIsEmpty(struct LibToriRS_ScriptQueue* queue)
{
    assert(queue);
    return queue->count == 0;
}

void
LibToriRS_ScriptQueueClear(struct LibToriRS_ScriptQueue* queue)
{
    assert(queue);
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
}

struct LibToriRS_ScriptArgs*
LibToriRS_ScriptGetArgs(struct LibToriRS_Script* script)
{
    assert(script);
    return &script->args;
}

void
LibToriRS_ScriptArgsReset(struct LibToriRS_ScriptArgs* args)
{
    assert(args);
    memset(args, 0, sizeof(struct LibToriRS_ScriptArgs));
    args->count = 0;
}

void
LibToriRS_ScriptPushArg_Int(
    struct LibToriRS_Script* script,
    int value)
{
    assert(script);
    if( script->args.count >= LIBTORIRS_SCRIPT_MAX_ARGS )
        return;
    struct LibToriRS_ScriptValue* script_value = &script->args.values[script->args.count];

    script_value->kind = LIBTORIRS_SCRIPT_VALUE_INT;
    script_value->u.intval.value = value;

    script->args.count++;
}
