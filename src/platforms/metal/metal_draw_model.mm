// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
metal_frame_event_model_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    metal_ensure_pipe(ctx, kMTLPipe3D);

    const int mid_draw = cmd->_model_draw.model_id;
    if( mid_draw <= 0 )
        return;

    GpuModelCache<void*>::ModelBufferRange* range =
        ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
    if( !range )
    {
        if( !build_model_instance(ctx->renderer, ctx->device, model, mid_draw) )
            return;
        range = ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
    }
    if( !range )
        return;

    void* vbo_h = nullptr;
    int vertex_index_base = 0;
    auto* model_entry = ctx->renderer->model_cache.get_model_entry(mid_draw);
    if( model_entry && model_entry->is_batched )
    {
        vbo_h = ctx->renderer->model_cache.get_batch_vbo_for_model(mid_draw);
        vertex_index_base = (int)(range->vtx_byte_offset / sizeof(MetalVertex));
    }
    else
    {
        vbo_h = range->buffer;
    }
    if( !vbo_h )
        return;

    const int gpu_face_count = range->face_count;

    struct DashPosition draw_position = cmd->_model_draw.position;
    SortedFaceOrder sorted;
    sorted.prepare(
        ctx->game->sys_dash, model, &draw_position, ctx->game->view_port, ctx->game->camera);
    if( sorted.count() <= 0 || !sorted.faces() )
        return;

    const float yaw_rad = (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
    const float cos_yaw = cosf(yaw_rad);
    const float sin_yaw = sinf(yaw_rad);

    const int slot = ctx->encode_slot;
    const size_t worst_ix = (size_t)gpu_face_count * 3u * sizeof(uint32_t);
    metal_ensure_ring_bytes(
        ctx->renderer,
        ctx->device,
        slot,
        &ctx->renderer->mtl_index_ring[slot],
        &ctx->renderer->mtl_index_ring_size[slot],
        &ctx->renderer->mtl_index_ring_write_offset[slot],
        ctx->renderer->mtl_index_ring_write_offset[slot] + worst_ix);
    metal_ensure_ring_bytes(
        ctx->renderer,
        ctx->device,
        slot,
        &ctx->renderer->mtl_instance_uniform_ring[slot],
        &ctx->renderer->mtl_instance_uniform_ring_size[slot],
        &ctx->renderer->mtl_instance_uniform_ring_write_offset[slot],
        ctx->renderer->mtl_instance_uniform_ring_write_offset[slot] + kMetalInstanceUniformStride);

    id<MTLBuffer> instRingBuf = (__bridge id<MTLBuffer>)ctx->renderer->mtl_instance_uniform_ring[slot];
    const NSUInteger instOff = ctx->renderer->mtl_instance_uniform_ring_write_offset[slot];
    {
        char* base = (char*)instRingBuf.contents + instOff;
        memset(base, 0, kMetalInstanceUniformStride);
        MetalInstanceUniform inst = {
            cos_yaw,
            sin_yaw,
            (float)draw_position.x,
            (float)draw_position.y,
            (float)draw_position.z,
            { 0.0f, 0.0f, 0.0f }
        };
        memcpy(base, &inst, sizeof(inst));
    }
    ctx->renderer->mtl_instance_uniform_ring_write_offset[slot] += kMetalInstanceUniformStride;

    id<MTLBuffer> gpuVbo = (__bridge id<MTLBuffer>)vbo_h;
    id<MTLBuffer> idxRingBuf = (__bridge id<MTLBuffer>)ctx->renderer->mtl_index_ring[slot];
    const NSUInteger ixOff = ctx->renderer->mtl_index_ring_write_offset[slot];
    uint32_t* dst = (uint32_t*)((char*)idxRingBuf.contents + ixOff);

    size_t nidx = 0;
    for( int i = 0; i < sorted.count(); ++i )
    {
        const int f = sorted.faces()[i];
        if( f < 0 || f >= gpu_face_count )
            continue;
        dst[nidx++] = (uint32_t)(vertex_index_base + f * 3 + 0);
        dst[nidx++] = (uint32_t)(vertex_index_base + f * 3 + 1);
        dst[nidx++] = (uint32_t)(vertex_index_base + f * 3 + 2);
    }
    if( nidx == 0 )
        return;

    ctx->renderer->mtl_index_ring_write_offset[slot] += nidx * sizeof(uint32_t);

    ctx->renderer->debug_model_draws++;
    [ctx->encoder setVertexBuffer:gpuVbo offset:0 atIndex:0];
    [ctx->encoder setVertexBuffer:ctx->unifBuf offset:0 atIndex:1];
    [ctx->encoder setVertexBuffer:instRingBuf offset:instOff atIndex:2];
    [ctx->encoder setFragmentBuffer:ctx->unifBuf offset:0 atIndex:1];
    [ctx->encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:(NSUInteger)nidx
                              indexType:MTLIndexTypeUInt32
                            indexBuffer:idxRingBuf
                      indexBufferOffset:ixOff];
    ctx->renderer->debug_triangles += (unsigned int)(nidx / 3u);
}
