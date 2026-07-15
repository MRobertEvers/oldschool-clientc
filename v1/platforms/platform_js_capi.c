#include "platform_js_capi.h"

// clang-format off
EM_JS(
    void,
    LibToriPlatformJS_CAPI_ASM_EmscriptenHost_LuaMainLoop,
    (void),
    {
        if( typeof window.LibToriPlatformJS.EmscriptenHost_LuaMainLoop === "function" )
        {
            window.LibToriPlatformJS.EmscriptenHost_LuaMainLoop();
        }
    });

EM_JS(
    void,
    LibToriPlatformJS_CAPI_ASM_EmscriptenHost_TasksMainLoop,
    (void),
    {
        if( typeof window.LibToriPlatformJS.EmscriptenHost_TasksMainLoop === "function" )
        {
            window.LibToriPlatformJS.EmscriptenHost_TasksMainLoop();
        }
    });

EM_JS(
    void,
    LibToriPlatformJS_CAPI_ASM_InitializeEmscriptenHost,
    (void* instance_ptr),
    {
         window.LibToriPlatformJS_Init(instance_ptr);
    });
// clang-format on

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformJS_CAPI_EmscriptenHost_LuaMainLoop(void)
{
    LibToriPlatformJS_CAPI_ASM_EmscriptenHost_LuaMainLoop();
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformJS_CAPI_EmscriptenHost_TasksMainLoop(void)
{
    LibToriPlatformJS_CAPI_ASM_EmscriptenHost_TasksMainLoop();
}

void
LibToriPlatformJS_CAPI_InitializeEmscriptenHost(struct LibToriRS_Instance* instance_ptr)
{
    LibToriPlatformJS_CAPI_ASM_InitializeEmscriptenHost(instance_ptr);
}
