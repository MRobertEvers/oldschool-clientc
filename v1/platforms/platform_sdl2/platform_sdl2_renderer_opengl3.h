#ifndef PLATFORM_SDL2_RENDERER_OPENGL3_H
#define PLATFORM_SDL2_RENDERER_OPENGL3_H

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2_RendererGL3;
struct LibToriRS_Instance;

struct LibToriPlatformSDL2_RendererGL3_MemStats
{
    uint32_t alive_model_slots;
    uint32_t pose_element_count;
    size_t cpu_vbo_static_bytes;
    size_t cpu_vbo_dynamic_bytes;
    size_t gpu_vbo_static_bytes;
    size_t gpu_vbo_dynamic_bytes;
    size_t gpu_ibo_bytes;
    uint32_t font_slots_live;
    uint32_t sprite_slots_live;
};

struct LibToriPlatformSDL2_RendererGL3*
LibToriPlatformSDL2_RendererGL3_New(
    int width,
    int height);

void
LibToriPlatformSDL2_RendererGL3_Free(struct LibToriPlatformSDL2_RendererGL3* renderer);

bool
LibToriPlatformSDL2_RendererGL3_Init(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    SDL_Window* window);

void
LibToriPlatformSDL2_RendererGL3_Render(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance);

void
LibToriPlatformSDL2_RendererGL3_MemStats(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriPlatformSDL2_RendererGL3_MemStats* out);

#endif
