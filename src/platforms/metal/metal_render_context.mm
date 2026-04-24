// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"
#include "platforms/common/torirs_gpu_clipspace.h"

void
metal_frame_event_clear_rect(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->encoder || !cmd || !ctx->renderer )
        return;

    if( !ctx->clearQuadBuf )
        return;
    if( !(ctx->clearRectDepthPipeState && ctx->clearRectDepthDsState &&
          metal_internal_depth_texture()) )
        return;

    void* cqb_cpu = ctx->clearQuadBuf.contents;
    if( !cqb_cpu )
        return;

    const int rx = cmd->_clear_rect.x;
    const int ry = cmd->_clear_rect.y;
    const int rw = cmd->_clear_rect.w;
    const int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
        return;

    int slot = ctx->clear_rect_slot;
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
        ctx->win_width,
        ctx->win_height,
        rx,
        ry,
        rw,
        rh);

    float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : ctx->renderer->width);
    float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : ctx->renderer->height);
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
    ctx->clear_rect_slot++;

    [ctx->encoder setViewport:ctx->fullDrawableVp];
    [ctx->encoder setScissorRect:sc];
    [ctx->encoder setDepthStencilState:ctx->clearRectDepthDsState];
    [ctx->encoder setRenderPipelineState:ctx->clearRectDepthPipeState];
    [ctx->encoder setCullMode:MTLCullModeNone];
    [ctx->encoder setVertexBuffer:ctx->clearQuadBuf offset:slot_off atIndex:0];
    [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    if( ctx->dsState )
        [ctx->encoder setDepthStencilState:ctx->dsState];

    MTLScissorRect scMax = {
        0,
        0,
        (NSUInteger)(ctx->renderer->width > 0 ? ctx->renderer->width : 1),
        (NSUInteger)(ctx->renderer->height > 0 ? ctx->renderer->height : 1),
    };
    [ctx->encoder setScissorRect:scMax];
}
