#ifndef PLATFORM_EMSCRIPTEN_JSHOST_H
#define PLATFORM_EMSCRIPTEN_JSHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"

#include <emscripten.h>

struct LibToriRS_Instance;

EMSCRIPTEN_KEEPALIVE
double
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSNumber(struct LibToriRS_ScriptValue* value);

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSString(struct LibToriRS_ScriptValue* value);

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptGetName(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetNameLength(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptArgs*
LibToriPlatformEmscripten_JSHost_ScriptGetArgs(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptFree(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptQueue*
LibToriPlatformEmscripten_JSHost_GetScriptQueue(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Instance*
LibToriPlatformEmscripten_JSHost_GetInstancePtr(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Script*
LibToriPlatformEmscripten_JSHost_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueue*
LibToriPlatformEmscripten_JSHost_LuaHost_GetIOQueue(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueueItem*
LibToriPlatformEmscripten_JSHost_IOQueuePop(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetArchiveId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetTableId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetFlags(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

#endif