#ifndef PLATFORM_EMSCRIPTEN_JSHOST_H
#define PLATFORM_EMSCRIPTEN_JSHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"

#include <emscripten.h>

struct LibToriRS_Instance;

// enum BrowserMainLoopState
// {
//     BROWSER_MAIN_LOOP_STATE_BEGIN,
//     BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS,
//     BROWSER_MAIN_LOOP_STATE_END
// };

// struct BrowserMainLoopSentinel
// {
//     enum BrowserMainLoopState state;
// };

// EMSCRIPTEN_KEEPALIVE
// void
// ToriPlatformEmscripten_JSHost_BrowserMainLoopSentinel(struct BrowserMainLoopSentinel* );

EMSCRIPTEN_KEEPALIVE
void
ToriPlatformEmscripten_JSHost_BrowserMainLoopSentinelFree(struct BrowserMainLoopSentinel* sentinel);

EMSCRIPTEN_KEEPALIVE
double
ToriPlatformEmscripten_JSHost_ScriptValueAsJSNumber(struct LibToriRS_ScriptValue* value);

EMSCRIPTEN_KEEPALIVE
char*
ToriPlatformEmscripten_JSHost_ScriptValueAsJSString(struct LibToriRS_ScriptValue* value);

EMSCRIPTEN_KEEPALIVE
char*
ToriPlatformEmscripten_JSHost_ScriptGetName(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptArgs*
ToriPlatformEmscripten_JSHost_ScriptGetArgs(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
void
ToriPlatformEmscripten_JSHost_ScriptFree(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Instance*
ToriPlatformEmscripten_JSHost_GetScriptQueue(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Script*
ToriPlatformEmscripten_JSHost_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue);

#endif