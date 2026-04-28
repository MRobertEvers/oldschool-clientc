// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

#include <cstdio>
#include <cstring>

void
metal_event_end_3d(
    MetalRenderCtx* ctx,
    void* uniforms_buffer_ptr)
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
            "metal_event_end_3d: more than %d 3D passes in one frame; skipping draw\n",
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
        fprintf(stderr, "metal_event_end_3d: uniform offset out of range\n");
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
    uniforms.uClock = game ? (float)game->cycle : 0.0f;
    uniforms._pad_uniform[0] = 0.0f;
    uniforms._pad_uniform[1] = 0.0f;
    uniforms._pad_uniform[2] = 0.0f;

    if( uniforms_buffer )
    {
        std::memcpy(
            (uint8_t*)[uniforms_buffer contents] + uniform_ofs, &uniforms, sizeof(uniforms));
    }

    const NSUInteger idx_base = (NSUInteger)renderer->pass.mtl_pass3d_idx_upload_ofs;
    const size_t dyn_n = renderer->mtl_pass3d_builder.IndexCount();
    const size_t dyn_cap = (size_t)kPass3DBuilder2MetalDynamicIndexCapacity;
    const size_t idx_copy_n =
        renderer->mtl_pass3d_builder.HasDynamicIndices() ? (dyn_n < dyn_cap ? dyn_n : dyn_cap) : 0u;
    const size_t idx_bytes = idx_copy_n * sizeof(uint32_t);

    id<MTLDepthStencilState> dsState = (__bridge id<MTLDepthStencilState>)renderer->pass.dsState;

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
