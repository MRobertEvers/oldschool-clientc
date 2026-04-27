// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/torirs_gpu_clipspace.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_clear_rect(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;

    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)ctx->renderer->pass.encoder;
    if( !encoder )
        return;

    id<MTLBuffer> clearQuadBuf = (__bridge id<MTLBuffer>)ctx->renderer->pass.clearQuadBuf;
    if( !clearQuadBuf )
        return;
    id<MTLRenderPipelineState> clearRectDepthPipeState =
        (__bridge id<MTLRenderPipelineState>)ctx->renderer->pass.clearRectDepthPipeState;
    id<MTLDepthStencilState> clearRectDepthDsState =
        (__bridge id<MTLDepthStencilState>)ctx->renderer->pass.clearRectDepthDsState;
    if( !(clearRectDepthPipeState && clearRectDepthDsState && metal_internal_depth_texture()) )
        return;

    void* cqb_cpu = clearQuadBuf.contents;
    if( !cqb_cpu )
        return;

    const int rx = cmd->_clear_rect.x;
    const int ry = cmd->_clear_rect.y;
    const int rw = cmd->_clear_rect.w;
    const int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
        return;

    int slot = ctx->renderer->pass.clear_rect_slot;
    if( slot >= kMetalMaxClearRectsPerFrame )
    {
        static bool s_warned_clear_slot;
        if( !s_warned_clear_slot )
        {
            fprintf(
                stderr,
                "[metal] CLEAR_RECT: more than %d clears in one frame; reusing last slot\n",
                kMetalMaxClearRectsPerFrame);
            s_warned_clear_slot = true;
        }
        slot = kMetalMaxClearRectsPerFrame - 1;
    }
    const NSUInteger slot_off = (NSUInteger)slot * kSpriteSlotBytes;

    MTLScissorRect sc = metal_clamped_scissor_from_logical_dst_bb(
        ctx->renderer->width,
        ctx->renderer->height,
        ctx->renderer->pass.win_width,
        ctx->renderer->pass.win_height,
        rx,
        ry,
        rw,
        rh);

    float fbw = (float)(ctx->renderer->pass.win_width > 0 ? ctx->renderer->pass.win_width
                                                          : ctx->renderer->width);
    float fbh = (float)(ctx->renderer->pass.win_height > 0 ? ctx->renderer->pass.win_height
                                                           : ctx->renderer->height);
    if( fbw <= 0.0f )
        fbw = 1.0f;
    if( fbh <= 0.0f )
        fbh = 1.0f;

    const float x0 = (float)rx;
    const float y0 = (float)ry;
    const float x1 = (float)(rx + rw);
    const float y1 = (float)(ry + rh);
    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
    torirs_logical_pixel_to_ndc(x0, y0, fbw, fbh, &c0x, &c0y);
    torirs_logical_pixel_to_ndc(x1, y0, fbw, fbh, &c1x, &c1y);
    torirs_logical_pixel_to_ndc(x1, y1, fbw, fbh, &c2x, &c2y);
    torirs_logical_pixel_to_ndc(x0, y1, fbw, fbh, &c3x, &c3y);

    const float quad[6 * 4] = {
        c0x, c0y, 0.0f, 0.0f, c1x, c1y, 0.0f, 0.0f, c2x, c2y, 0.0f, 0.0f,
        c0x, c0y, 0.0f, 0.0f, c2x, c2y, 0.0f, 0.0f, c3x, c3y, 0.0f, 0.0f,
    };

    memcpy((uint8_t*)cqb_cpu + slot_off, quad, sizeof(quad));
    ctx->renderer->pass.clear_rect_slot++;

    id<MTLDepthStencilState> dsState =
        (__bridge id<MTLDepthStencilState>)ctx->renderer->pass.dsState;

    [encoder setViewport:metal_pass_get_full_drawable_vp(ctx->renderer)];
    [encoder setScissorRect:sc];
    [encoder setDepthStencilState:clearRectDepthDsState];
    [encoder setRenderPipelineState:clearRectDepthPipeState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBuffer:clearQuadBuf offset:slot_off atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    if( dsState )
        [encoder setDepthStencilState:dsState];

    MTLScissorRect scMax = {
        0,
        0,
        (NSUInteger)(ctx->renderer->width > 0 ? ctx->renderer->width : 1),
        (NSUInteger)(ctx->renderer->height > 0 ? ctx->renderer->height : 1),
    };
    [encoder setScissorRect:scMax];
}
