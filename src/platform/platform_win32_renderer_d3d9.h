#ifndef SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_H
#define SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_H

#include "render/torirs_pick.h"
#include "render/torirs_render.h"

#include <stdbool.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;
struct ToriRS_D3D9;

#define TORIRS_D3D9_BG 0xFF202428

struct ToriRS_D3D9*
ToriRS_D3D9_New(int width, int height);

void
ToriRS_D3D9_Free(struct ToriRS_D3D9* d3d9);

bool
ToriRS_D3D9_Init(
    struct ToriRS_D3D9* d3d9,
    void* native_window,
    struct ToriDraw_Scene* scene);

void
ToriRS_D3D9_SetViewport(
    struct ToriRS_D3D9* d3d9,
    int width,
    int height);

void
ToriRS_D3D9_SetPick(struct ToriRS_D3D9* d3d9, int mouse_x, int mouse_y);

struct ToriRS_PickHits const*
ToriRS_D3D9_PickHits(struct ToriRS_D3D9 const* d3d9);

void
ToriRS_D3D9_Execute(
    struct ToriRS_D3D9* d3d9,
    struct ToriRS_RenderCommand const* cmd);

void
ToriRS_D3D9_DrawBootBar(struct ToriRS_D3D9* d3d9, int progress);

void
ToriRS_D3D9_RenderFrame(struct ToriRS_D3D9* d3d9, struct ToriRS_Frame* frame);

void
ToriRS_D3D9_Present(struct ToriRS_D3D9* d3d9);

#endif
