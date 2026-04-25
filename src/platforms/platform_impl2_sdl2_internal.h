#ifndef PLATFORM_IMPL2_SDL2_INTERNAL_H
#define PLATFORM_IMPL2_SDL2_INTERNAL_H

#include <stdbool.h>

struct Platform2_SDL2;
struct GGame;
struct GInput;

#ifdef __EMSCRIPTEN__
bool
PlatformImpl2_SDL2_Port_Emscripten_InitNewFields(struct Platform2_SDL2* platform);
void
PlatformImpl2_SDL2_Port_Emscripten_FreeExtra(struct Platform2_SDL2* platform);
bool
PlatformImpl2_SDL2_Port_Emscripten_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int canvas_width,
    int canvas_height);
void
PlatformImpl2_SDL2_Port_Emscripten_SyncCanvasCssSize(
    struct Platform2_SDL2* platform,
    struct GGame* game_nullable);
void
PlatformImpl2_SDL2_Port_Emscripten_PollEvents(
    struct Platform2_SDL2* platform,
    struct GInput* input,
    int chat_focused);
void
PlatformImpl2_SDL2_Port_Emscripten_RunLuaScripts(
    struct Platform2_SDL2* platform,
    struct GGame* game);
#else

#if defined(__APPLE__)
bool
PlatformImpl2_SDL2_Port_OSX_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
bool
PlatformImpl2_SDL2_Port_OSX_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
bool
PlatformImpl2_SDL2_Port_OSX_InitForMetal(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
bool
PlatformImpl2_SDL2_Port_Linux_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
bool
PlatformImpl2_SDL2_Port_Linux_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
#endif

#if defined(_WIN32)
bool
PlatformImpl2_SDL2_Port_Win_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
bool
PlatformImpl2_SDL2_Port_Win_InitForD3D11(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
#endif

#endif /* !__EMSCRIPTEN__ */

#endif
