// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
metal_flush_font_batch(MetalRenderCtx* ctx)
{
    if( ctx->font_verts.empty() || !ctx->current_font_atlas_tex || !ctx->fontVbo )
        return;
    int vert_count = (int)(ctx->font_verts.size() / 8);
    NSUInteger byte_count = ctx->font_verts.size() * sizeof(float);
    if( byte_count > ctx->fontVbo.length )
        return;
    memcpy(ctx->fontVbo.contents, ctx->font_verts.data(), byte_count);
    [ctx->encoder setVertexBuffer:ctx->fontVbo offset:0 atIndex:0];
    [ctx->encoder setFragmentTexture:ctx->current_font_atlas_tex atIndex:0];
    [ctx->encoder setFragmentSamplerState:ctx->fontSampler atIndex:0];
    [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle
                     vertexStart:0
                     vertexCount:(NSUInteger)vert_count];
    ctx->font_verts.clear();
}

void
metal_flush_batch(MetalRenderCtx* ctx)
{
    (void)ctx;
}

void
metal_ensure_pipe(MetalRenderCtx* ctx, int desired)
{
    if( ctx->current_pipe == desired )
        return;
    if( ctx->current_pipe == kMTLPipe3D )
        metal_flush_batch(ctx);
    if( ctx->current_pipe == kMTLPipeFont )
        metal_flush_font_batch(ctx);
    if( desired == kMTLPipe3D )
    {
        [ctx->encoder setViewport:ctx->metalVp];
        [ctx->encoder setRenderPipelineState:ctx->pipeState];
        [ctx->encoder setDepthStencilState:ctx->dsState];
        [ctx->encoder setCullMode:MTLCullModeNone];
        ctx->current_font_id = -1;
        if( ctx->worldAtlasTex && ctx->worldAtlasTilesBuf )
        {
            [ctx->encoder setFragmentTexture:ctx->worldAtlasTex atIndex:0];
            [ctx->encoder setFragmentBuffer:ctx->worldAtlasTilesBuf offset:0 atIndex:4];
            [ctx->encoder setFragmentSamplerState:ctx->samp atIndex:0];
        }
    }
    else if( desired == kMTLPipeUI )
    {
        if( !ctx->uiPipeState )
        {
            ctx->current_pipe = desired;
            return;
        }
        [ctx->encoder setViewport:ctx->spriteVp];
        [ctx->encoder setRenderPipelineState:ctx->uiPipeState];
        [ctx->encoder setDepthStencilState:ctx->dsState];
        [ctx->encoder setCullMode:MTLCullModeNone];
        ctx->current_font_id = -1;
    }
    else if( desired == kMTLPipeFont )
    {
        if( !ctx->fontPipeState )
        {
            ctx->current_pipe = desired;
            return;
        }
        [ctx->encoder setRenderPipelineState:ctx->fontPipeState];
        [ctx->encoder setDepthStencilState:ctx->dsState];
        [ctx->encoder setCullMode:MTLCullModeNone];
        [ctx->encoder setViewport:ctx->spriteVp];
    }
    ctx->current_pipe = desired;
}
