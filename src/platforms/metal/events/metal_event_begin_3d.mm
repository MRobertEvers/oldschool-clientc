// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_begin_3d(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !ctx->renderer || !default_logical_vp )
        return;
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)ctx->renderer->pass.encoder;
    if( !encoder )
        return;

    struct MetalRendererCore* renderer = ctx->renderer;

    LogicalViewportRect pass_lr = *default_logical_vp;
    if( cmd && cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        pass_lr.x = cmd->_begin_3d.x;
        pass_lr.y = cmd->_begin_3d.y;
        pass_lr.width = cmd->_begin_3d.w;
        pass_lr.height = cmd->_begin_3d.h;
    }

    const ToriGlViewportPixels gl_vp = compute_gl_world_viewport_rect(
        renderer->width,
        renderer->height,
        renderer->pass.win_width,
        renderer->pass.win_height,
        pass_lr);
    const double metal_origin_y = (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
    MTLViewport mvp = { .originX = (double)gl_vp.x,
                        .originY = metal_origin_y,
                        .width = (double)gl_vp.width,
                        .height = (double)gl_vp.height,
                        .znear = 0.0,
                        .zfar = 1.0 };
    metal_pass_set_metal_vp(renderer, mvp);

    [encoder setViewport:mvp];
    MTLScissorRect wsc = metal_clamped_scissor_from_logical_dst_bb(
        renderer->width,
        renderer->height,
        renderer->pass.win_width,
        renderer->pass.win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);
    [encoder setScissorRect:wsc];

    metal_pass_set_pass_3d_dst_logical(renderer, pass_lr);

    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_STATE_CLEAR_RECT;
    world_clear._clear_rect.x = pass_lr.x;
    world_clear._clear_rect.y = pass_lr.y;
    world_clear._clear_rect.w = pass_lr.width;
    world_clear._clear_rect.h = pass_lr.height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        metal_event_clear_rect(ctx, &world_clear);

    [encoder setViewport:mvp];
    [encoder setScissorRect:wsc];

    struct GGame* game = ctx->game;
    if( game )
    {
        renderer->mtl_pass3d_builder.Begin();
    }
    else
    {
        renderer->mtl_pass3d_builder.Begin();
    }
}
