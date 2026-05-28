#ifndef PLATFORM_SDL2_RENDERER_OPENGL3_H
#define PLATFORM_SDL2_RENDERER_OPENGL3_H

#include <SDL.h>
#include <stdbool.h>

struct LibToriPlatformSDL2_RendererGL3;
struct LibToriRS_Instance;

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

#endif
