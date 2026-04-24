#include "gpu_3d_cache2.h"
#include "pass_3d_builder2.h"
#import <Metal/Metal.h>

#include <cstring>

void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2& builder,
    const GPU3DCache2& cache,
    id<MTLRenderCommandEncoder> render_command_encoder,
    id<MTLBuffer> dynamic_instance_buffer, // Holds rotations
    id<MTLBuffer> dynamic_index_buffer)    // Holds sorted faces
{
    // 1. Upload Pools to GPU
    std::memcpy(
        [dynamic_instance_buffer contents],
        builder.GetInstancePool().data(),
        builder.GetInstancePoolSize());
    std::memcpy(
        [dynamic_index_buffer contents],
        builder.GetDynamicIndices().data(),
        builder.GetDynamicIndicesSize());

    for( const auto& cmd : builder.GetCommands() )
    {
        const auto& model = cache.GetModel(cmd.model_id);

        // 2. Bind the rotation for THIS specific object
        // We use the instance_offset we saved in the command
        NSUInteger rotation_offset = cmd.instance_offset * sizeof(DrawModelInstanceData3D);
        [render_command_encoder
            setVertexBytes:((uint8_t*)[dynamic_instance_buffer contents] + rotation_offset)
                    length:sizeof(DrawModelInstanceData3D)
                   atIndex:1]; // Buffer index 1 is for rotation

        // 3. Bind the Vertex Data
        [render_command_encoder //
            setVertexBuffer:(__bridge id<MTLBuffer>)(void*)model.vbo
                     offset:model.vbo_offset * 14 // 14 bytes per vertex
                    atIndex:0];

        // 4. Draw using either Sorted or Static Indices
        if( cmd.dynamic_index_count > 0 )
        {
            // DRAW SORTED (Transparency)
            NSUInteger indexByteOffset = cmd.dynamic_index_offset * sizeof(uint16_t);
            [renderEncoder //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:cmd.dynamic_index_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:dynamicIndexBuffer
                    indexBufferOffset:indexByteOffset];
        }
        else
        {
            // DRAW STATIC (Opaque)
            [render_command_encoder                            //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle //
                           indexCount:model.index_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:(__bridge id<MTLBuffer>)(void*)model.ebo
                    indexBufferOffset:model.ebo_offset * sizeof(uint16_t)];
        }
    }
}