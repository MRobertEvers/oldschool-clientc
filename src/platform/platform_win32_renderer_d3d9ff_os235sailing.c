/*
 * "Direct3D 9 (Sailing OSRS)" on XP, --d3d9-os235sailing: the rev-241 client's depth rules
 * through the fixed-function rev-241 world pass
 * (platform_win32_renderer_d3d9ff_rev241.c). Its own dispatch and frame; the
 * rest of the commands go through the core's (d3d9_dispatch). On win64 the
 * same renderer is platform_win64_renderer_d3d9sm3_os235sailing.c, with shaders.
 */

#include "platform/platform_win32_renderer_d3d9ff_rev241.h"

#include <assert.h>

static void
d3d9ff_os235_dispatch(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    struct D3D9ModelPlacement placement;
    if( command->kind == TORIRSRC_BEGIN_3D )
        d3d9ff_rev241_begin_3d(renderer, &command->u.begin_3d);
    else if( command->kind == TORIRSRC_END_3D )
        d3d9ff_rev241_end_3d(renderer);
    else if( command->kind == TORIRSRC_DRAW_MODEL )
    {
        if( d3d9_rev241_place(renderer, &command->u.model, &placement) )
            d3d9_rev241_emit_model(renderer, &command->u.model, &placement);
    }
    else
        d3d9_dispatch(renderer, command);
}

bool
ToriPlatformWin32_Renderer_D3D9_Os235SailingInit(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene)
{
    return d3d9ff_rev241_init(renderer, native_window, scene);
}

void
ToriPlatformWin32_Renderer_D3D9_Os235SailingExecute(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct ToriRS_RenderCommand const* command)
{
    assert(renderer);
    assert(command);
    d3d9ff_os235_dispatch(renderer, command);
}

void
ToriPlatformWin32_Renderer_D3D9_Os235SailingRenderFrame(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    uint64_t render_start_us;
    if( !d3d9_frame_begin(renderer, frame, &render_start_us) )
        return;
    while( ToriRS_FrameNextCommand(frame, &command) )
        d3d9ff_os235_dispatch(renderer, &command);
    ToriRS_FrameEnd(frame);
    if( renderer->in3d )
        d3d9ff_rev241_end_3d(renderer);
    d3d9_frame_finish(renderer, render_start_us);
}
