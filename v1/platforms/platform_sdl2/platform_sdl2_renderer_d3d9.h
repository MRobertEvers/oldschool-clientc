#ifndef PLATFORM_SDL2_RENDERER_D3D9_H
#define PLATFORM_SDL2_RENDERER_D3D9_H

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2_RendererD3D9;
struct LibToriRS_Instance;

struct LibToriPlatformSDL2_RendererD3D9*
LibToriPlatformSDL2_RendererD3D9_New(
    int width,
    int height);


void
LibToriPlatformSDL2_RendererD3D9_Free(struct LibToriPlatformSDL2_RendererD3D9* renderer);

bool
LibToriPlatformSDL2_RendererD3D9_Init(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    SDL_Window* window);


void
LibToriPlatformSDL2_RendererD3D9_Render(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance);
#endif