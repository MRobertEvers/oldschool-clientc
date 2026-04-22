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
metal_flush_3d(MetalRenderCtx* ctx, BufferedFaceOrder* bfo)
{
    if( !ctx || !bfo || !ctx->encoder || !ctx->renderer )
        return;
    bfo->finalize_slices();
    if( bfo->stream().empty() )
        return;

    metal_ensure_pipe(ctx, kMTLPipe3D);

    const int slot = ctx->encode_slot;
    GpuRingBuffer<void*>& ringS = ctx->renderer->mtl_draw_stream_ring;
    GpuRingBuffer<void*>& ringI = ctx->renderer->mtl_instance_xform_ring;

    const size_t sb = bfo->stream().size() * sizeof(DrawStreamEntry);
    const size_t ib = bfo->instances().size() * sizeof(InstanceXform);

    void* ringBufS = nullptr;
    size_t offS = 0;
    void* pS = ringS.reserve(slot, sb, &ringBufS, &offS);
    if( !pS )
        return;
    memcpy(pS, bfo->stream().data(), sb);

    void* ringBufI = nullptr;
    size_t offI = 0;
    void* pI = ringI.reserve(slot, ib, &ringBufI, &offI);
    if( !pI )
        return;
    memcpy(pI, bfo->instances().data(), ib);

    id<MTLBuffer> streamBuf = (__bridge id<MTLBuffer>)ringBufS;
    id<MTLBuffer> instBuf = (__bridge id<MTLBuffer>)ringBufI;

    for( const PassFlushSlice& slice : bfo->slices() )
    {
        if( slice.entry_count == 0 )
            continue;
        id<MTLBuffer> vbo = (__bridge id<MTLBuffer>)slice.vbo_handle;
        if( !vbo )
            continue;
        const NSUInteger streamByteOffset =
            offS + (NSUInteger)slice.entry_offset * sizeof(DrawStreamEntry);
        [ctx->encoder setVertexBuffer:vbo offset:0 atIndex:0];
        [ctx->encoder setVertexBuffer:ctx->unifBuf offset:0 atIndex:1];
        [ctx->encoder setVertexBuffer:instBuf offset:offI atIndex:2];
        [ctx->encoder setVertexBuffer:streamBuf offset:streamByteOffset atIndex:3];
        [ctx->encoder setFragmentBuffer:ctx->unifBuf offset:0 atIndex:1];
        [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle
                         vertexStart:0
                         vertexCount:(NSUInteger)slice.entry_count];
        ctx->renderer->debug_model_draws++;
        ctx->renderer->debug_triangles += (unsigned int)(slice.entry_count / 3u);
    }
}

void
metal_ensure_pipe(MetalRenderCtx* ctx, int desired)
{
    if( ctx->current_pipe == desired )
        return;

    if( ctx->current_pipe == kMTLPipe3D && ctx->bfo3d )
    {
        metal_flush_3d(ctx, ctx->bfo3d);
        ctx->bfo3d->begin_pass();
    }

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
