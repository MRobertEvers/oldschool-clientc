// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_metal.h"
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
    // v2: begin a new batch queue and reset pose-assignment counters
    ctx->renderer->model_cache2.BatchBegin();
    ctx->renderer->model_cache2_batch_map[bid] =
        Platform2_SDL2_Renderer_Metal::MetalCache2BatchEntry{};
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
    const int model_id = cmd->_model_load.model_id;
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( !model || model_id <= 0 || bid == 0 )
        return;

    const vertexint_t* vertices_x = dashmodel_vertices_x_const(model);
    const vertexint_t* vertices_y = dashmodel_vertices_y_const(model);
    const vertexint_t* vertices_z = dashmodel_vertices_z_const(model);

    const hsl16_t* face_colors_a = dashmodel_face_colors_a_const(model);
    const hsl16_t* face_colors_b = dashmodel_face_colors_b_const(model);
    const hsl16_t* face_colors_c = dashmodel_face_colors_c_const(model);
    assert(face_colors_a && face_colors_b && face_colors_c);

    const faceint_t* face_indices_a = dashmodel_face_indices_a_const(model);
    const faceint_t* face_indices_b = dashmodel_face_indices_b_const(model);
    const faceint_t* face_indices_c = dashmodel_face_indices_c_const(model);
    assert(face_indices_a && face_indices_b && face_indices_c);

    const int face_count = dashmodel_face_count(model);
    const int vertex_count = dashmodel_vertex_count(model);

    auto it_batch = ctx->renderer->model_cache2_batch_map.find(bid);
    if( it_batch != ctx->renderer->model_cache2_batch_map.end() )
        it_batch->second.model_ids.push_back((uint16_t)model_id);

    if( dashmodel_has_textures(model) )
    {
        const faceint_t* faces_textures = dashmodel_face_textures_const(model);
        const faceint_t* textured_faces = dashmodel_face_texture_coords(model);
        const faceint_t* textured_faces_a = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* textured_faces_b = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* textured_faces_c = dashmodel_textured_n_coordinate_const(model);

        ctx->renderer->model_cache2.BatchAddModelTexturedi16(
            (uint16_t)model_id,
            (uint16_t)cmd->_model_load.pose_id,
            0u,
            0u,
            (uint32_t)vertex_count,
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_x)),
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_y)),
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_z)),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            reinterpret_cast<uint8_t*>(const_cast<faceint_t*>(faces_textures)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_c)));
    }
    else
    {
        ctx->renderer->model_cache2.BatchAddModeli16(
            (uint16_t)model_id,
            (uint16_t)cmd->_model_load.pose_id,
            0u,
            0u,
            (uint32_t)vertex_count,
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_x)),
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_y)),
            reinterpret_cast<uint16_t*>(const_cast<vertexint_t*>(vertices_z)),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c));
    }
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

    // Helper lambda to submit the v2 batch (called on all exit paths)
    auto submit_v2 = [&]() {
        BatchBuffers v2bufs{};
        GPU3DCache2BatchSubmitMetal(ctx->renderer->model_cache2, ctx->device, v2bufs);
        auto it = ctx->renderer->model_cache2_batch_map.find(bid);
        if( it != ctx->renderer->model_cache2_batch_map.end() )
        {
            it->second.vbo = v2bufs.vbo ? (__bridge_retained void*)v2bufs.vbo : nullptr;
            it->second.ebo = v2bufs.ebo ? (__bridge_retained void*)v2bufs.ebo : nullptr;
        }
    };

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
        submit_v2();
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
        submit_v2();
        return;
    }
    ctx->renderer->model_cache.end_batch(bid);
    ctx->renderer->current_model_batch_id = 0;

    // v2: submit GPU3DCache2 batch and retain the resulting MTLBuffers
    submit_v2();
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

    // v2: release retained MTLBuffers and clear poses for all models in this batch
    {
        auto it = ctx->renderer->model_cache2_batch_map.find(bid);
        if( it != ctx->renderer->model_cache2_batch_map.end() )
        {
            for( uint16_t mid : it->second.model_ids )
                ctx->renderer->model_cache2.ClearModel(mid);
            if( it->second.vbo )
                CFRelease(it->second.vbo);
            if( it->second.ebo )
                CFRelease(it->second.ebo);
            ctx->renderer->model_cache2_batch_map.erase(it);
        }
    }
}
