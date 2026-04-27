#ifndef PLATFORM_IMPL2_SDL2_RENDERER_METAL_H
#define PLATFORM_IMPL2_SDL2_RENDERER_METAL_H

#if defined(__APPLE__)

#include "platforms/ToriRSPlatformKit/src/backends/metal/trspk_metal.h"
#include "platforms/platform_impl2_sdl2.h"

#include <SDL.h>
#include <stdint.h>
#include <vector>

struct Platform2_SDL2_Renderer_Metal
{
    struct Platform2_SDL2* platform = nullptr;
    SDL_MetalView metal_view = nullptr;
    TRSPK_MetalRenderer* trspk = nullptr;
    int width = 0;
    int height = 0;
    bool ready = false;
    uint32_t current_model_batch_id = 0;
    bool current_model_batch_active = false;
    std::vector<uint8_t> rgba_scratch;
    std::vector<uint32_t> sorted_indices;
};

Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_Metal_Free(Platform2_SDL2_Renderer_Metal* renderer);

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif

#endif
