#ifndef PLATFORM_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_SDL2_RENDERER_WEBGL1_H

#ifdef __EMSCRIPTEN__

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2_RendererWebGL1;
struct LibToriRS_Instance;

struct LibToriPlatformSDL2_RendererWebGL1*
LibToriPlatformSDL2_RendererWebGL1_New(
    int width,
    int height);

void
LibToriPlatformSDL2_RendererWebGL1_Free(struct LibToriPlatformSDL2_RendererWebGL1* renderer);

bool
LibToriPlatformSDL2_RendererWebGL1_Init(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    SDL_Window* window);

void
LibToriPlatformSDL2_RendererWebGL1_Render(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance);

#endif // __EMSCRIPTEN__

#endif