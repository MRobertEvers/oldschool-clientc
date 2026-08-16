#ifndef PLATFORM_JSC_H
#define PLATFORM_JSC_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"

#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformJS_CAPI_EmscriptenHost_LuaMainLoop(void);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformJS_CAPI_EmscriptenHost_TasksMainLoop(void);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformJS_CAPI_InitializeEmscriptenHost(struct LibToriRS_Instance* instance_ptr);

#endif