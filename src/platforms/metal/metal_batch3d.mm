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
    if( dashmodel_face_count(model) <= 0 )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) )
        return;
    if( !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
        !dashmodel_vertices_z_const(model) )
        return;
    if( !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) )
        return;

    const int fc = dashmodel_face_count(model);
    const faceint_t* ftex = dashmodel_face_textures_const(model);
    std::vector<MetalVertex> verts((size_t)fc * 3u);
    std::vector<int> per_face_tex((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = ftex ? (int)ftex[f] : -1;
        per_face_tex[(size_t)f] = raw;
        MetalVertex tri[3];
        if( !fill_model_face_vertices_model_local(model, f, raw, tri) )
        {
            metal_vertex_fill_invisible(&tri[0]);
            metal_vertex_fill_invisible(&tri[1]);
            metal_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
    }
    ctx->renderer->model_cache.accumulate_batch_model(
        bid, mid, verts.data(), (int)sizeof(MetalVertex), fc * 3, fc, per_face_tex.data());
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
    GpuModelCache<void*>::BatchEntry* batch = ctx->renderer->model_cache.get_batch(bid);
    if( !batch || !batch->open )
    {
        ctx->renderer->current_model_batch_id = 0;
        return;
    }
    const int byte_count = ctx->renderer->model_cache.get_batch_pending_byte_count(bid);
    if( byte_count <= 0 )
    {
        ctx->renderer->model_cache.end_batch(bid, nullptr, nullptr);
        ctx->renderer->current_model_batch_id = 0;
        return;
    }
    const void* pending = ctx->renderer->model_cache.get_batch_pending_verts(bid);
    id<MTLBuffer> vbo = [ctx->device newBufferWithBytes:pending
                                                 length:(NSUInteger)byte_count
                                                options:MTLResourceStorageModeShared];
    const size_t idx_count = batch->pending_indices.size();
    const size_t idx_bytes = idx_count * sizeof(uint32_t);
    id<MTLBuffer> ibo = nil;
    if( idx_bytes > 0 )
    {
        ibo = [ctx->device newBufferWithBytes:batch->pending_indices.data()
                                        length:(NSUInteger)idx_bytes
                                       options:MTLResourceStorageModeShared];
    }
    ctx->renderer->model_cache.end_batch(
        bid, vbo ? (__bridge_retained void*)vbo : nullptr, ibo ? (__bridge_retained void*)ibo : nullptr);
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
    if( batch->vbo )
    {
        CFRelease(batch->vbo);
        batch->vbo = nullptr;
    }
    if( batch->ibo )
    {
        CFRelease(batch->ibo);
        batch->ibo = nullptr;
    }
    ctx->renderer->model_cache.unload_batch(bid);
}
