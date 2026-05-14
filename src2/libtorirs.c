#include "libtorirs.h"

#include "libtorirs_internal.h"

#include <stdlib.h>

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void)
{
    struct LibToriRS_Instance* instance = malloc(sizeof(struct LibToriRS_Instance));
    if( !instance )
        return NULL;
    instance->script_queue = LibToriRS_ScriptQueueNew();
    if( !instance->script_queue )
    {
        free(instance);
        return NULL;
    }
    return instance;
}

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance)
{
    if( instance )
        return;
    if( instance->script_queue )
        LibToriRS_ScriptQueueFree(instance->script_queue);
    free(instance);
    instance = NULL;
}

struct LibToriRS_ScriptQueue*
LibToriRS_GetScriptQueue(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->script_queue;
}