// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH3D_LOAD_START
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_model_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->model_cache.begin_batch(bid);
    ctx->renderer->current_model_batch_id = bid;
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH3D_MODEL_LOAD
// ---------------------------------------------------------------------------
void
metal_frame_event_model_batched_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    const int mid = cmd->_model_load.model_id;
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( !model || mid <= 0 || bid == 0 )
        return;
    if( !metal_dispatch_model_load(ctx->renderer, ctx->device, mid, model, true, bid) )
        return;
    if( dashmodel__is_ground_va(model) )
        metal_patch_batched_va_model_verts(ctx, model);
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH3D_LOAD_END
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_model_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    Gpu3DCache<void*>::BatchEntry* batch = ctx->renderer->model_cache.get_batch(bid);
    if( !batch || !batch->open )
    {
        ctx->renderer->current_model_batch_id = 0;
        return;
    }

    const int nchunks = ctx->renderer->model_cache.get_batch_chunk_count(bid);
    if( nchunks <= 0 )
    {
        ctx->renderer->model_cache.end_batch(bid);
        ctx->renderer->current_model_batch_id = 0;
        return;
    }

    bool any = false;
    for( int c = 0; c < nchunks; ++c )
    {
        const int vb = ctx->renderer->model_cache.get_chunk_pending_bytes(bid, c);
        const int ic = ctx->renderer->model_cache.get_chunk_pending_index_count(bid, c);
        if( vb <= 0 || ic <= 0 )
            continue;
        any = true;
        const void* pv = ctx->renderer->model_cache.get_chunk_pending_verts(bid, c);
        const uint32_t* pi = ctx->renderer->model_cache.get_chunk_pending_indices(bid, c);
        id<MTLBuffer> vbo = [ctx->device newBufferWithBytes:pv
                                                     length:(NSUInteger)vb
                                                    options:MTLResourceStorageModeShared];
        id<MTLBuffer> ibo = [ctx->device newBufferWithBytes:pi
                                                     length:(NSUInteger)ic * sizeof(uint32_t)
                                                    options:MTLResourceStorageModeShared];
        ctx->renderer->model_cache.set_chunk_buffers(
            bid,
            c,
            vbo ? (__bridge_retained void*)vbo : nullptr,
            ibo ? (__bridge_retained void*)ibo : nullptr);
    }
    if( !any )
    {
        ctx->renderer->model_cache.unload_batch(bid);
        ctx->renderer->current_model_batch_id = 0;
        return;
    }
    ctx->renderer->model_cache.end_batch(bid);
    ctx->renderer->current_model_batch_id = 0;
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH3D_CLEAR
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_model_clear(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    auto* batch = ctx->renderer->model_cache.get_batch(bid);
    if( !batch )
        return;
    const int n = ctx->renderer->model_cache.get_batch_chunk_count(bid);
    for( int c = 0; c < n; ++c )
    {
        const Gpu3DCache<void*>::BatchChunk* ch =
            ctx->renderer->model_cache.get_batch_chunk(bid, c);
        if( !ch )
            continue;
        if( ch->vbo )
            CFRelease(ch->vbo);
        if( ch->ibo )
            CFRelease(ch->ibo);
    }
    ctx->renderer->model_cache.unload_batch(bid);
}
