// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

struct TorirsNkMetalUi* s_mtl_nk = nullptr;
Uint64 s_mtl_ui_prev_perf = 0;

void
render_nuklear_overlay(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    id<MTLRenderCommandEncoder> encoder)
{
    if( !s_mtl_nk )
        return;

    double const dt = torirs_nk_ui_frame_delta_sec(&s_mtl_ui_prev_perf);

    TorirsNkDebugPanelParams params = {};
    params.window_title = "Metal Debug";
    params.delta_time_sec = dt;
    params.view_w_cap = 4096;
    params.view_h_cap = 4096;
    params.include_soft3d_extras = 0;
    params.include_gpu_frame_stats = 1;
    params.gpu_model_draws = renderer->debug_model_draws;
    params.gpu_tris = renderer->debug_triangles;

    int win_w = renderer->platform ? renderer->platform->game_screen_width : 0;
    int win_h = renderer->platform ? renderer->platform->game_screen_height : 0;
    if( win_w <= 0 || win_h <= 0 )
    {
        SDL_Window* wnd = renderer->platform ? renderer->platform->window : nullptr;
        if( wnd )
            SDL_GetWindowSize(wnd, &win_w, &win_h);
    }
    if( win_w <= 0 )
        win_w = renderer->width;
    if( win_h <= 0 )
        win_h = renderer->height;

    struct nk_context* nk = torirs_nk_metal_ctx(s_mtl_nk);
    torirs_nk_debug_panel_draw(nk, game, &params);
    torirs_nk_ui_after_frame(nk);
    torirs_nk_metal_resize(s_mtl_nk, win_w, win_h);
    if( renderer->mtl_depth_stencil )
        [encoder
            setDepthStencilState:(__bridge id<MTLDepthStencilState>)renderer->mtl_depth_stencil];
    torirs_nk_metal_render(
        s_mtl_nk,
        (__bridge void*)encoder,
        win_w,
        win_h,
        renderer->width,
        renderer->height,
        NK_ANTI_ALIASING_ON);
}
