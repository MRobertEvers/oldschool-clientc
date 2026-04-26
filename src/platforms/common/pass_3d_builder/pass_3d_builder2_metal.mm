#include "pass_3d_builder2_metal.h"

#include "platforms/metal/metal_internal.h"
#import <Metal/Metal.h>

#include <cstdio>
#include <cstring>

void
Pass3DBuilder2Metal::AppendSortedDraw(
    GPUResourceHandle vbo,
    uint32_t pose_vbo_vertex_offset,
    const uint32_t* sorted_indices,
    uint32_t index_count)
{
    if( !is_building_ || sorted_indices == nullptr || index_count == 0u )
        return;
    if( vbo == 0u )
        return;

    if( mesh_vbo_ == 0u )
        mesh_vbo_ = vbo;
    else if( mesh_vbo_ != vbo )
    {
        static bool s_warned_mismatch;
        if( !s_warned_mismatch )
        {
            fprintf(
                stderr,
                "[Pass3DBuilder2Metal] multiple mesh VBOs in one pass; ignoring draws not matching "
                "first VBO\n");
            s_warned_mismatch = true;
        }
        return;
    }

    const uint32_t voff = pose_vbo_vertex_offset;
    for( uint32_t ii = 0; ii < index_count; ++ii )
        indices_pool_.push_back(voff + sorted_indices[ii]);
}

void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2Metal& builder,
    id<MTLRenderCommandEncoder> enc,
    id<MTLBuffer> dynamic_index_buffer,
    NSUInteger index_base_bytes,
    id<MTLTexture> fragment_atlas_texture,
    id<MTLBuffer> atlas_tiles_buffer,
    id<MTLBuffer> uniforms_buffer,
    NSUInteger uniforms_buffer_offset_bytes,
    id<MTLSamplerState> fragment_sampler,
    id<MTLDepthStencilState> depth_stencil_state)
{
    if( !builder.HasCommands() )
        return;

    if( depth_stencil_state )
        [enc setDepthStencilState:depth_stencil_state];

    const std::vector<uint32_t>& dix = builder.Indices();
    const size_t dyn_index_count = dix.size();
    const size_t dyn_capacity = (size_t)kPass3DBuilder2MetalDynamicIndexCapacity;
    if( dyn_index_count > dyn_capacity )
    {
        fprintf(
            stderr,
            "Pass3DBuilder2SubmitMetal: dynamic index pool %zu exceeds capacity %zu; "
            "clamping upload, draws past range are skipped\n",
            dyn_index_count,
            dyn_capacity);
    }
    const size_t copy_n = dyn_index_count < dyn_capacity ? dyn_index_count : dyn_capacity;
    const size_t idx_bytes = copy_n * sizeof(uint32_t);
    const NSUInteger idx_cap = dynamic_index_buffer ? dynamic_index_buffer.length : 0u;
    if( index_base_bytes + idx_bytes > idx_cap )
    {
        fprintf(
            stderr,
            "Pass3DBuilder2SubmitMetal: index upload %zu + base %zu exceeds buffer %u\n",
            idx_bytes,
            (size_t)index_base_bytes,
            (unsigned)idx_cap);
        return;
    }
    if( idx_bytes > 0u && dynamic_index_buffer )
    {
        std::memcpy(
            (uint8_t*)[dynamic_index_buffer contents] + index_base_bytes,
            dix.data(),
            idx_bytes);
    }

    if( uniforms_buffer )
    {
        const NSUInteger uni_len = uniforms_buffer.length;
        if( uniforms_buffer_offset_bytes + sizeof(MetalUniforms) > uni_len )
        {
            fprintf(
                stderr,
                "Pass3DBuilder2SubmitMetal: uniform offset %zu out of range (buf %u)\n",
                (size_t)uniforms_buffer_offset_bytes,
                (unsigned)uni_len);
            return;
        }
        [enc setVertexBuffer:uniforms_buffer offset:uniforms_buffer_offset_bytes atIndex:1];
        [enc setFragmentBuffer:uniforms_buffer offset:uniforms_buffer_offset_bytes atIndex:1];
    }
    if( atlas_tiles_buffer )
        [enc setFragmentBuffer:atlas_tiles_buffer offset:0 atIndex:4];
    if( fragment_atlas_texture )
        [enc setFragmentTexture:fragment_atlas_texture atIndex:0];
    if( fragment_sampler )
        [enc setFragmentSamplerState:fragment_sampler atIndex:0];

    id<MTLBuffer> mesh_vbo = (__bridge id<MTLBuffer>)(void*)builder.MeshVbo();
    if( !mesh_vbo )
        return;

    [enc setVertexBuffer:mesh_vbo offset:0 atIndex:0];

    const NSUInteger total_indices = (NSUInteger)copy_n;
    if( total_indices == 0u )
        return;

    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:total_indices
                     indexType:MTLIndexTypeUInt32
                   indexBuffer:dynamic_index_buffer
             indexBufferOffset:index_base_bytes
                 instanceCount:1
                    baseVertex:0
                  baseInstance:0];
}
