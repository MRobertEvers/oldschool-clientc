#ifndef PLATFORM_SDL2_RENDERER_SOFT3D_H
#define PLATFORM_SDL2_RENDERER_SOFT3D_H

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2_RendererSoft3D;
struct LibToriRS_Instance;

struct LibToriPlatformSDL2_RendererSoft3D*
LibToriPlatformSDL2_RendererSoft3D_New(
    int width,
    int height);

void
LibToriPlatformSDL2_RendererSoft3D_Free(struct LibToriPlatformSDL2_RendererSoft3D* renderer);

bool
LibToriPlatformSDL2_RendererSoft3D_Init(
    struct LibToriPlatformSDL2_RendererSoft3D* renderer,
    SDL_Window* window);

void
LibToriPlatformSDL2_RendererSoft3D_Render(
    struct LibToriPlatformSDL2_RendererSoft3D* renderer,
    struct LibToriRS_Instance* instance);

#endif
