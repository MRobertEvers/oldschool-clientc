#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include <SDL.h>
#include <stdbool.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;

#define TORIRS_GL3_BG 0xFF202428

struct ToriRS_GL3;

struct ToriRS_GL3*
ToriRS_GL3_New(int width, int height);

void
ToriRS_GL3_Free(struct ToriRS_GL3* gl3);

bool
ToriRS_GL3_Init(
    struct ToriRS_GL3* gl3,
    SDL_Window* window,
    struct ToriDraw_Scene* scene);

void
ToriRS_GL3_SetPick(struct ToriRS_GL3* gl3, int mouse_x, int mouse_y);

void
ToriRS_GL3_RenderFrame(struct ToriRS_GL3* gl3, struct ToriRS_Frame* frame);

void
ToriRS_GL3_Execute(
    struct ToriRS_GL3* gl3,
    struct ToriRS_RenderCommand const* cmd);

void
ToriRS_GL3_DrawBootBar(struct ToriRS_GL3* gl3, int progress);

struct ToriRS_PickHits const*
ToriRS_GL3_PickHits(struct ToriRS_GL3 const* gl3);

#endif
