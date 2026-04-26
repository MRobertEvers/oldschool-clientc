// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include <cstdio>
#include <cstring>

void
metal_frame_event_begin_3d(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !ctx->renderer || !default_logical_vp )
        return;
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)ctx->renderer->pass.encoder;
    if( !encoder )
        return;

    struct MetalRendererCore* renderer = ctx->renderer;

    LogicalViewportRect pass_lr = *default_logical_vp;
    if( cmd && cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        pass_lr.x = cmd->_begin_3d.x;
        pass_lr.y = cmd->_begin_3d.y;
        pass_lr.width = cmd->_begin_3d.w;
        pass_lr.height = cmd->_begin_3d.h;
    }

    const ToriGlViewportPixels gl_vp = compute_gl_world_viewport_rect(
        renderer->width,
        renderer->height,
        renderer->pass.win_width,
        renderer->pass.win_height,
        pass_lr);
    const double metal_origin_y = (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
    MTLViewport mvp = { .originX = (double)gl_vp.x,
                        .originY = metal_origin_y,
                        .width = (double)gl_vp.width,
                        .height = (double)gl_vp.height,
                        .znear = 0.0,
                        .zfar = 1.0 };
    metal_pass_set_metal_vp(renderer, mvp);

    [encoder setViewport:mvp];
    MTLScissorRect wsc = metal_clamped_scissor_from_logical_dst_bb(
        renderer->width,
        renderer->height,
        renderer->pass.win_width,
        renderer->pass.win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);
    [encoder setScissorRect:wsc];

    metal_pass_set_pass_3d_dst_logical(renderer, pass_lr);

    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_STATE_CLEAR_RECT;
    world_clear._clear_rect.x = pass_lr.x;
    world_clear._clear_rect.y = pass_lr.y;
    world_clear._clear_rect.w = pass_lr.width;
    world_clear._clear_rect.h = pass_lr.height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        metal_frame_event_clear_rect(ctx, &world_clear);

    [encoder setViewport:mvp];
    [encoder setScissorRect:wsc];

    struct GGame* game = ctx->game;
    if( game )
    {
        renderer->mtl_pass3d_builder.Begin();
    }
    else
    {
        renderer->mtl_pass3d_builder.Begin();
    }
}

void
metal_frame_event_end_3d(MetalRenderCtx* ctx, void* uniforms_buffer_ptr)
{
    id<MTLBuffer> uniforms_buffer = (__bridge id<MTLBuffer>)uniforms_buffer_ptr;
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)ctx->renderer->pass.encoder;
    if( !ctx || !encoder || !ctx->renderer )
        return;

    struct MetalRendererCore* renderer = ctx->renderer;

    if( !renderer->mtl_3d_v2_pipeline || !renderer->mtl_pass3d_index_buf )
        return;

    if( !renderer->mtl_pass3d_builder.HasCommands() )
    {
        renderer->mtl_pass3d_builder.End();
        return;
    }

    const size_t uniform_stride = metal_uniforms_stride_padded();
    const size_t uniform_frame_block = uniform_stride * (size_t)kMetalMax3dPassesPerFrame;
    if( renderer->pass.mtl_uniform_pass_subslot >= (uint32_t)kMetalMax3dPassesPerFrame )
    {
        fprintf(
            stderr,
            "metal_frame_event_end_3d: more than %d 3D passes in one frame; skipping draw\n",
            kMetalMax3dPassesPerFrame);
        renderer->mtl_pass3d_builder.ClearAfterSubmit();
        renderer->mtl_pass3d_builder.End();
        return;
    }

    const NSUInteger uniform_ofs =
        (NSUInteger)((size_t)renderer->mtl_uniform_frame_slot * uniform_frame_block +
                     (size_t)renderer->pass.mtl_uniform_pass_subslot * uniform_stride);

    if( uniforms_buffer && uniform_ofs + sizeof(MetalUniforms) > uniforms_buffer.length )
    {
        fprintf(stderr, "metal_frame_event_end_3d: uniform offset out of range\n");
        renderer->mtl_pass3d_builder.ClearAfterSubmit();
        renderer->mtl_pass3d_builder.End();
        return;
    }

    const LogicalViewportRect pl = metal_pass_get_pass_3d_dst_logical(renderer);
    const float projection_width = (float)pl.width;
    const float projection_height = (float)pl.height;

    MetalUniforms uniforms;
    struct GGame* game = ctx->game;
    const float pitch_rad = game ? metal_dash_yaw_to_radians(game->camera_pitch) : 0.0f;
    const float yaw_rad = game ? metal_dash_yaw_to_radians(game->camera_yaw) : 0.0f;
    metal_compute_view_matrix(
        uniforms.modelViewMatrix,
        game ? (float)game->camera_world_x : 0.0f,
        game ? (float)game->camera_world_y : 0.0f,
        game ? (float)game->camera_world_z : 0.0f,
        pitch_rad,
        yaw_rad);
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

    const NSUInteger idx_base = (NSUInteger)renderer->pass.mtl_pass3d_idx_upload_ofs;
    const size_t dyn_n = renderer->mtl_pass3d_builder.GetDynamicIndices().size();
    const size_t dyn_cap = (size_t)kPass3DBuilder2MetalDynamicIndexCapacity;
    const size_t idx_copy_n =
        renderer->mtl_pass3d_builder.HasDynamicIndices() ? (dyn_n < dyn_cap ? dyn_n : dyn_cap) : 0u;
    const size_t idx_bytes = idx_copy_n * sizeof(uint32_t);

    id<MTLDepthStencilState> dsState =
        (__bridge id<MTLDepthStencilState>)renderer->pass.dsState;

    [encoder
        setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)renderer->mtl_3d_v2_pipeline];
    Pass3DBuilder2SubmitMetal(
        renderer->mtl_pass3d_builder,
        encoder,
        (__bridge id<MTLBuffer>)renderer->mtl_pass3d_index_buf,
        idx_base,
        (__bridge id<MTLTexture>)renderer->mtl_cache2_atlas_tex,
        (__bridge id<MTLBuffer>)renderer->mtl_cache2_atlas_tiles_buf,
        uniforms_buffer,
        uniform_ofs,
        (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state,
        dsState);

    renderer->pass.mtl_pass3d_idx_upload_ofs += idx_bytes;
    renderer->pass.mtl_uniform_pass_subslot += 1u;

    renderer->mtl_pass3d_builder.ClearAfterSubmit();
    renderer->mtl_pass3d_builder.End();
}
