#include "platform_emscripten_jshost.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

EMSCRIPTEN_KEEPALIVE
double
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSNumber(struct LibToriRS_ScriptValue* value)
{
    if( !value )
        return 0.0;
    switch( value->kind )
    {
    case LIBTORIRS_SCRIPT_VALUE_INT:
        return (double)value->u.intval.value;
    case LIBTORIRS_SCRIPT_VALUE_ANY:
        return (double)(uintptr_t)value->u.anyval.value;
    case LIBTORIRS_SCRIPT_VALUE_VOID:
    default:
        return 0.0;
    }
}

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSString(struct LibToriRS_ScriptValue* value)
{
    if( !value )
        return NULL;
    if( value->kind != LIBTORIRS_SCRIPT_VALUE_ANY )
        return NULL;
    if( !value->u.anyval.value )
        return NULL;
    return (char*)value->u.anyval.value;
}

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptGetName(struct LibToriRS_Script* script)
{
    printf("ToriPlatformEmscripten_JSHost_ScriptGetName: %s\n", script->name);
    if( !script )
        return NULL;
    return script->name;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetNameLength(struct LibToriRS_Script* script)
{
    if( !script )
        return 0;
    return strlen(script->name);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptArgs*
LibToriPlatformEmscripten_JSHost_ScriptGetArgs(struct LibToriRS_Script* script)
{
    return LibToriRS_ScriptGetArgs(script);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptFree(struct LibToriRS_Script* script)
{
    struct LibToriRS_ScriptArgs* args = LibToriRS_ScriptGetArgs(script);
    if( !args )
        return;
    LibToriRS_ScriptArgsReset(args);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptQueue*
LibToriPlatformEmscripten_JSHost_GetScriptQueue(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetScriptQueue(instance);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Instance*
LibToriPlatformEmscripten_JSHost_GetInstancePtr(struct LibToriRS_Instance* instance)
{
    return instance;
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Script*
LibToriPlatformEmscripten_JSHost_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue)
{
    return LibToriRS_ScriptQueuePop(queue);
}