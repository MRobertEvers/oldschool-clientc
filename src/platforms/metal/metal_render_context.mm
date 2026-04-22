// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

static void
metal_flush_2d_fonts(MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->encoder || !ctx->bft2d || !ctx->fontVbo || !ctx->fontPipeState )
        return;
    ctx->bft2d->close_open_segment();
    const std::vector<FontDrawGroup>& groups = ctx->bft2d->groups();
    if( groups.empty() )
    {
        ctx->bft2d->begin_pass();
        return;
    }

    metal_ensure_pipe(ctx, kMTLPipeFont);

    const std::vector<float>& fv = ctx->bft2d->verts();
    const float* all = fv.data();
    const NSUInteger capBytes = ctx->fontVbo.length;
    const size_t total_bytes = fv.size() * sizeof(float);

    auto draw_one_group = [&](const FontDrawGroup& g, NSUInteger vbo_byte_offset) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>)g.atlas_tex;
        if( !tex || g.float_count == 0 )
            return;
        const NSUInteger vcount = (NSUInteger)(g.float_count / 8u);
        if( vcount == 0 )
            return;
        [ctx->encoder setVertexBuffer:ctx->fontVbo offset:vbo_byte_offset atIndex:0];
        [ctx->encoder setFragmentTexture:tex atIndex:0];
        [ctx->encoder setFragmentSamplerState:ctx->fontSampler atIndex:0];
        [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vcount];
    };

    if( total_bytes > 0 && total_bytes <= (size_t)capBytes )
    {
        memcpy(ctx->fontVbo.contents, all, total_bytes);
        for( const FontDrawGroup& g : groups )
            draw_one_group(g, (NSUInteger)g.first_float * sizeof(float));
    }
    else if( total_bytes > (size_t)capBytes )
    {
        static bool s_warned_font_2d_ovf;
        if( !s_warned_font_2d_ovf )
        {
            fprintf(
                stderr,
                "[metal] metal_flush_2d_fonts: batched font verts (%zu bytes) exceed buffer "
                "(%llu), packing prefix groups\n",
                total_bytes,
                (unsigned long long)capBytes);
            s_warned_font_2d_ovf = true;
        }
        size_t cursor = 0;
        for( const FontDrawGroup& g : groups )
        {
            const size_t nbytes = (size_t)g.float_count * sizeof(float);
            if( nbytes == 0 )
                continue;
            if( cursor + nbytes > (size_t)capBytes )
                break;
            memcpy((uint8_t*)ctx->fontVbo.contents + cursor, all + g.first_float, nbytes);
            draw_one_group(g, (NSUInteger)cursor);
            cursor += nbytes;
        }
    }

    ctx->bft2d->begin_pass();
}

void
metal_frame_event_clear_rect(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->encoder || !cmd || !ctx->renderer )
        return;

    /* Encode any batched 2D before this clear so order matches other renderers. */
    metal_flush_2d(ctx);

    if( !ctx->clearRectPipeState || !ctx->spriteQuadBuf )
    {
        ctx->current_pipe = kMTLPipeNone;
        return;
    }

    void* sqb_cpu = ctx->spriteQuadBuf.contents;
    if( !sqb_cpu )
    {
        ctx->current_pipe = kMTLPipeNone;
        return;
    }

    const int rx = cmd->_clear_rect.x;
    const int ry = cmd->_clear_rect.y;
    const int rw = cmd->_clear_rect.w;
    const int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
    {
        ctx->current_pipe = kMTLPipeNone;
        return;
    }

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
    c0x = 2.0f * x0 / fbw - 1.0f;
    c0y = 1.0f - 2.0f * y0 / fbh;
    c1x = 2.0f * x1 / fbw - 1.0f;
    c1y = 1.0f - 2.0f * y0 / fbh;
    c2x = 2.0f * x1 / fbw - 1.0f;
    c2y = 1.0f - 2.0f * y1 / fbh;
    c3x = 2.0f * x0 / fbw - 1.0f;
    c3y = 1.0f - 2.0f * y1 / fbh;

    const float quad[6 * 4] = {
        c0x, c0y, 0.0f, 0.0f, c1x, c1y, 0.0f, 0.0f, c2x, c2y, 0.0f, 0.0f,
        c0x, c0y, 0.0f, 0.0f, c2x, c2y, 0.0f, 0.0f, c3x, c3y, 0.0f, 0.0f,
    };

    memcpy(sqb_cpu, quad, sizeof(quad));

    [ctx->encoder setViewport:ctx->spriteVp];
    [ctx->encoder setScissorRect:sc];
    [ctx->encoder setRenderPipelineState:ctx->clearRectPipeState];
    if( ctx->dsState )
        [ctx->encoder setDepthStencilState:ctx->dsState];
    [ctx->encoder setCullMode:MTLCullModeNone];
    [ctx->encoder setVertexBuffer:ctx->spriteQuadBuf offset:0 atIndex:0];
    [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

    MTLScissorRect scMax = {
        0,
        0,
        (NSUInteger)(ctx->renderer->width > 0 ? ctx->renderer->width : 1),
        (NSUInteger)(ctx->renderer->height > 0 ? ctx->renderer->height : 1),
    };
    [ctx->encoder setScissorRect:scMax];

    ctx->current_pipe = kMTLPipeNone;
}

void
metal_flush_2d(MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->encoder )
        return;

    if( ctx->bsp2d && ctx->spriteQuadBuf && ctx->uiPipeState &&
        !ctx->bsp2d->groups().empty() )
    {
        metal_ensure_pipe(ctx, kMTLPipeUI);

        const std::vector<SpriteDrawGroup>& groups = ctx->bsp2d->groups();
        const std::vector<SpriteQuadVertex>& verts = ctx->bsp2d->verts();
        const SpriteQuadVertex* vbase = verts.data();
        const NSUInteger buf_len = ctx->spriteQuadBuf.length;
        const size_t vert_bytes = verts.size() * sizeof(SpriteQuadVertex);

        if( buf_len == 0 )
        {
            ctx->bsp2d->begin_pass();
        }
        else if( vert_bytes > 0 && vert_bytes <= (size_t)buf_len )
        {
            memcpy(ctx->spriteQuadBuf.contents, vbase, vert_bytes);
            for( const SpriteDrawGroup& g : groups )
            {
                id<MTLTexture> tex = (__bridge id<MTLTexture>)g.atlas_tex;
                if( !tex || g.vertex_count == 0 )
                    continue;

                [ctx->encoder setFragmentTexture:tex atIndex:0];
                [ctx->encoder setFragmentSamplerState:ctx->uiSamplerNearest atIndex:0];

                if( g.has_scissor )
                {
                    MTLScissorRect sc = {
                        g.scissor.x,
                        g.scissor.y,
                        g.scissor.width,
                        g.scissor.height,
                    };
                    [ctx->encoder setScissorRect:sc];
                }

                const NSUInteger byte_off =
                    (NSUInteger)g.first_vertex * sizeof(SpriteQuadVertex);
                [ctx->encoder setVertexBuffer:ctx->spriteQuadBuf offset:byte_off atIndex:0];
                [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                 vertexStart:0
                                 vertexCount:(NSUInteger)g.vertex_count];

                if( g.has_scissor )
                {
                    MTLScissorRect scMax = {
                        0,
                        0,
                        (NSUInteger)ctx->renderer->width,
                        (NSUInteger)ctx->renderer->height,
                    };
                    [ctx->encoder setScissorRect:scMax];
                }
            }
            ctx->bsp2d->begin_pass();
        }
        else if( vert_bytes > (size_t)buf_len )
        {
            static bool s_warned_sprite_2d_ovf;
            if( !s_warned_sprite_2d_ovf )
            {
                fprintf(
                    stderr,
                    "[metal] metal_flush_2d: batched sprite verts (%zu bytes) exceed buffer "
                    "(%llu), packing prefix groups\n",
                    vert_bytes,
                    (unsigned long long)buf_len);
                s_warned_sprite_2d_ovf = true;
            }
            size_t cursor = 0;
            for( const SpriteDrawGroup& g : groups )
            {
                id<MTLTexture> tex = (__bridge id<MTLTexture>)g.atlas_tex;
                if( !tex || g.vertex_count == 0 )
                    continue;

                [ctx->encoder setFragmentTexture:tex atIndex:0];
                [ctx->encoder setFragmentSamplerState:ctx->uiSamplerNearest atIndex:0];

                if( g.has_scissor )
                {
                    MTLScissorRect sc = {
                        g.scissor.x,
                        g.scissor.y,
                        g.scissor.width,
                        g.scissor.height,
                    };
                    [ctx->encoder setScissorRect:sc];
                }

                const size_t nbytes = (size_t)g.vertex_count * sizeof(SpriteQuadVertex);
                if( cursor + nbytes > (size_t)buf_len )
                    break;
                memcpy(
                    (uint8_t*)ctx->spriteQuadBuf.contents + cursor,
                    vbase + g.first_vertex,
                    nbytes);
                [ctx->encoder setVertexBuffer:ctx->spriteQuadBuf offset:(NSUInteger)cursor
                                       atIndex:0];
                [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                 vertexStart:0
                                 vertexCount:(NSUInteger)g.vertex_count];
                cursor += nbytes;

                if( g.has_scissor )
                {
                    MTLScissorRect scMax = {
                        0,
                        0,
                        (NSUInteger)ctx->renderer->width,
                        (NSUInteger)ctx->renderer->height,
                    };
                    [ctx->encoder setScissorRect:scMax];
                }
            }
            ctx->bsp2d->begin_pass();
        }
        else
            ctx->bsp2d->begin_pass();
    }
    else if( ctx->bsp2d )
        ctx->bsp2d->begin_pass();

    metal_flush_2d_fonts(ctx);
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

    /* Consume the batch so a later pipe switch (e.g. 3D→UI) does not submit the same stream twice. */
    bfo->begin_pass();
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

    if( desired == kMTLPipe3D )
    {
        [ctx->encoder setViewport:ctx->metalVp];
        [ctx->encoder setRenderPipelineState:ctx->pipeState];
        [ctx->encoder setDepthStencilState:ctx->dsState];
        [ctx->encoder setCullMode:MTLCullModeNone];
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
