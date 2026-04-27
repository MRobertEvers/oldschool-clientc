#ifndef PLATFORM_IMPL2_SDL2_RENDERER_OPENGL3_H
#define PLATFORM_IMPL2_SDL2_RENDERER_OPENGL3_H

#include "platforms/opengl3/opengl3_renderer_core.h"

struct Platform2_SDL2_Renderer_OpenGL3*
PlatformImpl2_SDL2_Renderer_OpenGL3_New(int width, int height);

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Free(struct Platform2_SDL2_Renderer_OpenGL3* renderer);

bool
PlatformImpl2_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
