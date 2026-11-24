#ifndef SRC_PLATFORM_PLATFORM_WIN64_RENDERER_D3D9SM3_REV241_H
#define SRC_PLATFORM_PLATFORM_WIN64_RENDERER_D3D9SM3_REV241_H

/* Win64's shader rev-241 world pass, shared by its two OSRS renderers
 * (platform_win64_renderer_d3d9sm3_rev241.c). */

#include "platform/platform_win32_renderer_d3d9_rev241.h"

bool
d3d9sm3_rev241_init(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene);

void
d3d9sm3_rev241_begin_3d(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command);

void
d3d9sm3_rev241_end_3d(struct ToriPlatformWin32_Renderer_D3D9* renderer);

#endif
