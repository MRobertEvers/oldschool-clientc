// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_metal.h"
#include "platforms/metal/metal_internal.h"

void
metal_cache2_batch_push_model_mesh(
    MetalRenderCtx* ctx,
    struct DashModel* model,
    int model_id,
    uint32_t batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !model || model_id <= 0 )
        return;
    if( dashmodel__is_ground_va(model) )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const vertexint_t* vertices_x = dashmodel_vertices_x_const(model);
    const vertexint_t* vertices_y = dashmodel_vertices_y_const(model);
    const vertexint_t* vertices_z = dashmodel_vertices_z_const(model);

    const hsl16_t* face_colors_a = dashmodel_face_colors_a_const(model);
    const hsl16_t* face_colors_b = dashmodel_face_colors_b_const(model);
    const hsl16_t* face_colors_c = dashmodel_face_colors_c_const(model);

    const faceint_t* face_indices_a = dashmodel_face_indices_a_const(model);
    const faceint_t* face_indices_b = dashmodel_face_indices_b_const(model);
    const faceint_t* face_indices_c = dashmodel_face_indices_c_const(model);

    const int face_count = dashmodel_face_count(model);
    const int vertex_count = dashmodel_vertex_count(model);

    if( batch_id != 0 )
    {
        auto it_batch = ctx->renderer->model_cache2_batch_map.find(batch_id);
        if( it_batch != ctx->renderer->model_cache2_batch_map.end() )
            it_batch->second.model_ids.push_back((uint16_t)model_id);
    }

    if( dashmodel_has_textures(model) )
    {
        const faceint_t* faces_textures = dashmodel_face_textures_const(model);
        const faceint_t* textured_faces = dashmodel_face_texture_coords(model);
        const faceint_t* textured_faces_a = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* textured_faces_b = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* textured_faces_c = dashmodel_textured_n_coordinate_const(model);

        ctx->renderer->model_cache2.BatchAddModelTexturedi16(
            (uint16_t)model_id,
            gpu_segment_slot,
            frame_index,
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
            gpu_segment_slot,
            frame_index,
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
// TORIRS_GFX_BATCH3D_LOAD_START
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_model_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->current_model_batch_id = bid;
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

    metal_cache2_batch_push_model_mesh(ctx, model, model_id, bid, GPU_MODEL_ANIMATION_NONE_IDX, 0u);
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

    BatchBuffers v2bufs{};
    GPU3DCache2BatchSubmitMetal(ctx->renderer->model_cache2, ctx->device, v2bufs);
    auto it = ctx->renderer->model_cache2_batch_map.find(bid);
    if( it != ctx->renderer->model_cache2_batch_map.end() )
    {
        it->second.vbo = v2bufs.vbo ? (__bridge_retained void*)v2bufs.vbo : nullptr;
        it->second.ebo = v2bufs.ebo ? (__bridge_retained void*)v2bufs.ebo : nullptr;
    }

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
    auto it = ctx->renderer->model_cache2_batch_map.find(bid);
    if( it == ctx->renderer->model_cache2_batch_map.end() )
        return;
    for( uint16_t mid : it->second.model_ids )
        ctx->renderer->model_cache2.ClearModel(mid);
    if( it->second.vbo )
        CFRelease(it->second.vbo);
    if( it->second.ebo )
        CFRelease(it->second.ebo);
    ctx->renderer->model_cache2_batch_map.erase(it);
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BEGIN_3D / TORIRS_GFX_END_3D
// ---------------------------------------------------------------------------
void
metal_frame_event_begin_3d(MetalRenderCtx* ctx, const LogicalViewportRect* logical_vp)
{
    if( !ctx || !ctx->encoder || !ctx->renderer || !logical_vp )
        return;

    struct Platform2_SDL2_Renderer_Metal* renderer = ctx->renderer;
    id<MTLRenderCommandEncoder> encoder = ctx->encoder;

    [encoder setViewport:ctx->metalVp];
    MTLScissorRect wsc = metal_clamped_scissor_from_logical_dst_bb(
        renderer->width,
        renderer->height,
        ctx->win_width,
        ctx->win_height,
        logical_vp->x,
        logical_vp->y,
        logical_vp->width,
        logical_vp->height);
    [encoder setScissorRect:wsc];

    /* Depth clear inside the 3D world clip (logical viewport). Color buffer in
       that region is unchanged from the pass load clear. */
    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_CLEAR_RECT;
    world_clear._clear_rect.x = logical_vp->x;
    world_clear._clear_rect.y = logical_vp->y;
    world_clear._clear_rect.w = logical_vp->width;
    world_clear._clear_rect.h = logical_vp->height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        metal_frame_event_clear_rect(ctx, &world_clear);

    renderer->mtl_pass3d_builder.Begin3D();
}

void
metal_frame_event_end_3d(MetalRenderCtx* ctx, id<MTLBuffer> uniforms_buffer)
{
    if( !ctx || !ctx->encoder || !ctx->renderer )
        return;

    struct Platform2_SDL2_Renderer_Metal* renderer = ctx->renderer;
    id<MTLRenderCommandEncoder> encoder = ctx->encoder;

    if( !renderer->mtl_3d_v2_pipeline || !renderer->mtl_pass3d_instance_buf ||
        !renderer->mtl_pass3d_index_buf || !renderer->mtl_pass3d_builder.HasCommands() )
        return;

    [encoder
        setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)renderer->mtl_3d_v2_pipeline];
    Pass3DBuilder2SubmitMetal(
        renderer->mtl_pass3d_builder,
        renderer->model_cache2,
        encoder,
        (__bridge id<MTLBuffer>)renderer->mtl_pass3d_instance_buf,
        (__bridge id<MTLBuffer>)renderer->mtl_pass3d_index_buf,
        (__bridge id<MTLTexture>)renderer->mtl_cache2_atlas_tex,
        (__bridge id<MTLBuffer>)renderer->mtl_cache2_atlas_tiles_buf,
        uniforms_buffer,
        (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state);
    renderer->mtl_pass3d_builder.ClearAfterSubmit();
}
