#include "gpu_3d_cache2.h"
#include "pass_3d_builder2.h"
#include "platforms/metal/metal.h"
#include "platforms/metal/metal_internal.h"
#import <Metal/Metal.h>

#include <cstdio>
#include <cstring>

void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2<GPU3DTransformUniformMetal>& builder,
    const GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    struct Platform2_SDL2_Renderer_Metal* metal_renderer,
    id<MTLRenderCommandEncoder> render_command_encoder, // Renamed for brevity
    id<MTLBuffer> dynamic_instance_buffer,
    id<MTLBuffer> dynamic_index_buffer,
    NSUInteger instance_base_bytes,
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
        [render_command_encoder setDepthStencilState:depth_stencil_state];

    const size_t inst_bytes = builder.GetInstancePool().size() * sizeof(GPU3DTransformUniformMetal);
    const NSUInteger inst_cap = dynamic_instance_buffer ? dynamic_instance_buffer.length : 0u;
    if( instance_base_bytes + inst_bytes > inst_cap )
    {
        fprintf(
            stderr,
            "Pass3DBuilder2SubmitMetal: instance upload %zu + base %zu exceeds buffer %u\n",
            inst_bytes,
            (size_t)instance_base_bytes,
            (unsigned)inst_cap);
        return;
    }

    if( inst_bytes > 0u && dynamic_instance_buffer )
    {
        std::memcpy(
            (uint8_t*)[dynamic_instance_buffer contents] + instance_base_bytes,
            builder.GetInstancePool().data(),
            inst_bytes);
    }

    const size_t dyn_index_count = builder.GetDynamicIndices().size();
    const size_t dyn_capacity = (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity;
    size_t copy_n = 0;
    if( builder.HasDynamicIndices() )
    {
        if( dyn_index_count > dyn_capacity )
        {
            fprintf(
                stderr,
                "Pass3DBuilder2SubmitMetal: dynamic index pool %zu exceeds capacity %zu; "
                "clamping upload, draws past range are skipped\n",
                dyn_index_count,
                dyn_capacity);
        }
        copy_n = dyn_index_count < dyn_capacity ? dyn_index_count : dyn_capacity;
        const size_t idx_bytes = copy_n * sizeof(uint16_t);
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
                builder.GetDynamicIndices().data(),
                idx_bytes);
        }
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
        [render_command_encoder setVertexBuffer:uniforms_buffer
                                         offset:uniforms_buffer_offset_bytes
                                        atIndex:1];
        [render_command_encoder setFragmentBuffer:uniforms_buffer
                                           offset:uniforms_buffer_offset_bytes
                                          atIndex:1];
    }
    if( atlas_tiles_buffer )
        [render_command_encoder setFragmentBuffer:atlas_tiles_buffer offset:0 atIndex:4];
    if( fragment_atlas_texture )
        [render_command_encoder setFragmentTexture:fragment_atlas_texture atIndex:0];
    if( fragment_sampler )
        [render_command_encoder setFragmentSamplerState:fragment_sampler atIndex:0];

    // EBO: `[[vertex_id]]` is index + baseVertex — `vertexShader` reads `verts[vid]` directly.
    id<MTLBuffer> last_bound_vbo = nil;

    for( const auto& cmd : builder.GetCommands() )
    {
        const GPUModelPosedData model = cache.GetModelPoseForDraw(
            cmd.model_id, cmd.use_animation, (int)cmd.animation_index, (int)cmd.frame_index);
        if( !model.valid )
            continue;

        id<MTLBuffer> vbo = nil;
        id<MTLBuffer> ebo = nil;
        if( model.gpu_batch_id != 0u && metal_renderer )
        {
            const auto it = metal_renderer->model_cache2_batch_map.find(model.gpu_batch_id);
            if( it != metal_renderer->model_cache2_batch_map.end() && it->second.vbo &&
                it->second.ebo )
            {
                vbo = (__bridge id<MTLBuffer>)it->second.vbo;
                ebo = (__bridge id<MTLBuffer>)it->second.ebo;
            }
        }
        if( !vbo )
            vbo = (__bridge id<MTLBuffer>)(void*)model.vbo;
        if( !ebo )
            ebo = (__bridge id<MTLBuffer>)(void*)model.ebo;
        if( !vbo || (cmd.dynamic_index_count == 0 && !ebo) )
            continue;

        // 3. Bind Rotation from the Instance Buffer (Fastest path)
        NSUInteger rotation_offset =
            instance_base_bytes + cmd.instance_offset * sizeof(GPU3DTransformUniformMetal);
        [render_command_encoder //
            setVertexBuffer:dynamic_instance_buffer
                     offset:rotation_offset
                    atIndex:2];

        // 4. Merged batch VBO (indices are absolute within this buffer)
        if( vbo != last_bound_vbo )
        {
            [render_command_encoder setVertexBuffer:vbo offset:0 atIndex:0];
            last_bound_vbo = vbo;
        }

        // 5. Draw
        if( cmd.dynamic_index_count > 0 )
        {
            const uint64_t end =
                (uint64_t)cmd.dynamic_index_offset + (uint64_t)cmd.dynamic_index_count;
            if( end > dyn_capacity )
                continue;
            // Sorted Indexing (Dynamic Pool)
            NSUInteger index_byte_offset =
                index_base_bytes + cmd.dynamic_index_offset * sizeof(uint16_t);
            /* Indices are local to this model's merged slice; `GPUModelPosedData::vbo_offset` is
             * the first vertex of that slice in the batch VBO (see metal_frame_event_model_draw).
             */
            [render_command_encoder //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:cmd.dynamic_index_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:dynamic_index_buffer
                    indexBufferOffset:index_byte_offset
                        instanceCount:1
                           baseVertex:(NSInteger)model.vbo_offset
                         baseInstance:0];
        }
        else
        {
            // Static Indexing (Baked Cache)
            NSUInteger ebo_byte_offset = model.ebo_offset * sizeof(uint16_t);
            [render_command_encoder //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:model.element_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:ebo
                    indexBufferOffset:ebo_byte_offset
                        instanceCount:1
                           baseVertex:0
                         baseInstance:0];
        }
    }
}
