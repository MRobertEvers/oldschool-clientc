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
    const int* face_infos = dashmodel_face_infos_const(model);

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
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            const_cast<faceint_t*>(faces_textures),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_c)),
            face_infos,
            dashmodel__is_ground_va(model));
    }
    else
    {
        ctx->renderer->model_cache2.BatchAddModeli16(
            (uint16_t)model_id,
            gpu_segment_slot,
            frame_index,
            (uint32_t)vertex_count,
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            face_infos);
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
    GPU3DCache2BatchSubmitMetal(ctx->renderer->model_cache2, ctx->device, v2bufs, bid);
    auto it = ctx->renderer->model_cache2_batch_map.find(bid);
    if( it != ctx->renderer->model_cache2_batch_map.end() )
    {
        if( it->second.vbo )
            CFRelease(it->second.vbo);
        if( it->second.ebo )
            CFRelease(it->second.ebo);
        it->second.vbo = v2bufs.vbo;
        it->second.ebo = v2bufs.ebo;
        v2bufs.vbo = nullptr;
        v2bufs.ebo = nullptr;
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
metal_frame_event_begin_3d(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !ctx->encoder || !ctx->renderer || !default_logical_vp )
        return;

    struct Platform2_SDL2_Renderer_Metal* renderer = ctx->renderer;
    id<MTLRenderCommandEncoder> encoder = ctx->encoder;

    LogicalViewportRect pass_lr = *default_logical_vp;
    if( cmd && cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        pass_lr.x = cmd->_begin_3d.x;
        pass_lr.y = cmd->_begin_3d.y;
        pass_lr.width = cmd->_begin_3d.w;
        pass_lr.height = cmd->_begin_3d.h;
    }

    const ToriGlViewportPixels gl_vp = compute_gl_world_viewport_rect(
        renderer->width, renderer->height, ctx->win_width, ctx->win_height, pass_lr);
    const double metal_origin_y = (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
    ctx->metalVp = (MTLViewport){ .originX = (double)gl_vp.x,
                                  .originY = metal_origin_y,
                                  .width = (double)gl_vp.width,
                                  .height = (double)gl_vp.height,
                                  .znear = 0.0,
                                  .zfar = 1.0 };

    [encoder setViewport:ctx->metalVp];
    MTLScissorRect wsc = metal_clamped_scissor_from_logical_dst_bb(
        renderer->width,
        renderer->height,
        ctx->win_width,
        ctx->win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);
    [encoder setScissorRect:wsc];

    ctx->pass_3d_dst_logical = pass_lr;

    /* Depth clear inside the 3D world clip (logical viewport). Color buffer in
       that region is unchanged from the pass load clear. */
    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_CLEAR_RECT;
    world_clear._clear_rect.x = pass_lr.x;
    world_clear._clear_rect.y = pass_lr.y;
    world_clear._clear_rect.w = pass_lr.width;
    world_clear._clear_rect.h = pass_lr.height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        metal_frame_event_clear_rect(ctx, &world_clear);

    /* Restore 3D world viewport + scissor — metal_frame_event_clear_rect overwrites both
       with fullDrawableVp / a full-drawable scissor and does not restore them. */
    [encoder setViewport:ctx->metalVp];
    [encoder setScissorRect:wsc];

    renderer->mtl_pass3d_builder.Begin3D();
}

void
metal_frame_event_end_3d(
    MetalRenderCtx* ctx,
    id<MTLBuffer> uniforms_buffer)
{
    if( !ctx || !ctx->encoder || !ctx->renderer )
        return;

    struct Platform2_SDL2_Renderer_Metal* renderer = ctx->renderer;
    id<MTLRenderCommandEncoder> encoder = ctx->encoder;

    if( !renderer->mtl_3d_v2_pipeline || !renderer->mtl_pass3d_instance_buf ||
        !renderer->mtl_pass3d_index_buf )
        return;

    if( !renderer->mtl_pass3d_builder.HasCommands() )
    {
        renderer->mtl_pass3d_builder.End3D();
        return;
    }

    const size_t uniform_stride = metal_uniforms_stride_padded();
    const size_t uniform_frame_block = uniform_stride * (size_t)kMetalMax3dPassesPerFrame;
    if( renderer->mtl_uniform_pass_subslot >= (uint32_t)kMetalMax3dPassesPerFrame )
    {
        fprintf(
            stderr,
            "metal_frame_event_end_3d: more than %d 3D passes in one frame; skipping draw\n",
            kMetalMax3dPassesPerFrame);
        renderer->mtl_pass3d_builder.ClearAfterSubmit();
        renderer->mtl_pass3d_builder.End3D();
        return;
    }

    const NSUInteger uniform_ofs =
        (NSUInteger)((size_t)renderer->mtl_uniform_frame_slot * uniform_frame_block +
                     (size_t)renderer->mtl_uniform_pass_subslot * uniform_stride);

    if( uniforms_buffer && uniform_ofs + sizeof(MetalUniforms) > uniforms_buffer.length )
    {
        fprintf(stderr, "metal_frame_event_end_3d: uniform offset out of range\n");
        renderer->mtl_pass3d_builder.ClearAfterSubmit();
        renderer->mtl_pass3d_builder.End3D();
        return;
    }

    const LogicalViewportRect& pl = ctx->pass_3d_dst_logical;
    const float projection_width = (float)pl.width;
    const float projection_height = (float)pl.height;

    MetalUniforms uniforms;
    struct GGame* game = ctx->game;
    const float pitch_rad = game ? metal_dash_yaw_to_radians(game->camera_pitch) : 0.0f;
    const float yaw_rad = game ? metal_dash_yaw_to_radians(game->camera_yaw) : 0.0f;
    metal_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    metal_compute_projection_matrix(
        uniforms.projectionMatrix,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width > 0.0f ? projection_width : 1.0f,
        projection_height > 0.0f ? projection_height : 1.0f);
    metal_remap_projection_opengl_to_metal_z(uniforms.projectionMatrix);
    uniforms.uClock = (float)(SDL_GetTicks64() / 20);
    uniforms._pad_uniform[0] = 0.0f;
    uniforms._pad_uniform[1] = 0.0f;
    uniforms._pad_uniform[2] = 0.0f;

    if( uniforms_buffer )
    {
        std::memcpy(
            (uint8_t*)[uniforms_buffer contents] + uniform_ofs, &uniforms, sizeof(uniforms));
    }

    const NSUInteger inst_base = (NSUInteger)renderer->mtl_pass3d_inst_upload_ofs;
    const NSUInteger idx_base = (NSUInteger)renderer->mtl_pass3d_idx_upload_ofs;
    const size_t inst_bytes =
        renderer->mtl_pass3d_builder.GetInstancePool().size() * sizeof(InstanceXform);
    const size_t dyn_n = renderer->mtl_pass3d_builder.GetDynamicIndices().size();
    const size_t dyn_cap = (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity;
    const size_t idx_copy_n =
        renderer->mtl_pass3d_builder.HasDynamicIndices() ? (dyn_n < dyn_cap ? dyn_n : dyn_cap) : 0u;
    const size_t idx_bytes = idx_copy_n * sizeof(uint16_t);

    [encoder
        setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)renderer->mtl_3d_v2_pipeline];
    Pass3DBuilder2SubmitMetal(
        renderer->mtl_pass3d_builder,
        renderer->model_cache2,
        renderer,
        encoder,
        (__bridge id<MTLBuffer>)renderer->mtl_pass3d_instance_buf,
        (__bridge id<MTLBuffer>)renderer->mtl_pass3d_index_buf,
        inst_base,
        idx_base,
        (__bridge id<MTLTexture>)renderer->mtl_cache2_atlas_tex,
        (__bridge id<MTLBuffer>)renderer->mtl_cache2_atlas_tiles_buf,
        uniforms_buffer,
        uniform_ofs,
        (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state,
        ctx->dsState);

    renderer->mtl_pass3d_inst_upload_ofs += inst_bytes;
    renderer->mtl_pass3d_idx_upload_ofs += idx_bytes;
    renderer->mtl_uniform_pass_subslot += 1u;

    renderer->mtl_pass3d_builder.ClearAfterSubmit();
    renderer->mtl_pass3d_builder.End3D();
}
