#include "gpu_3d_cache2.h"
#include "pass_3d_builder2.h"
#import <Metal/Metal.h>

#include <cstring>
void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2& builder,
    const GPU3DCache2& cache,
    id<MTLRenderCommandEncoder> render_command_encoder, // Renamed for brevity
    id<MTLBuffer> dynamic_instance_buffer,
    id<MTLBuffer> dynamic_index_buffer)
{
    if( !builder.HasCommands() )
        return;

    // 1. Single Upload for the entire frame
    std::memcpy(
        [dynamic_instance_buffer contents],
        builder.GetInstancePool().data(),
        builder.GetInstancePool().size() * sizeof(DrawModelInstanceData3D));

    if( builder.HasDynamicIndices() )
    {
        std::memcpy(
            [dynamic_index_buffer contents],
            builder.GetDynamicIndices().data(),
            builder.GetDynamicIndices().size() * sizeof(uint16_t));
    }

    // 2. Bind the Global Atlas (Common for all models in this pass)
    // We assume the atlas was submitted earlier and its handle is stored in the cache
    // id<MTLTexture> atlas = (__bridge id<MTLTexture>)(void*)cache.GetAtlasHandle();
    // [renderE ncoder setFragmentTexture:atlas atIndex:0];

    // State Tracking to prevent redundant binding (buffer + interleaved start offset).
    id<MTLBuffer> last_bound_vbo = nil;
    NSUInteger last_vbo_byte_offset = NSNotFound;

    for( const auto& cmd : builder.GetCommands() )
    {
        const GPUModelPosedData model = cache.GetModelPose(cmd.model_id, cmd.pose_id);
        if( !model.valid )
            continue;

        // 3. Bind Rotation from the Instance Buffer (Fastest path)
        NSUInteger rotation_offset = cmd.instance_offset * sizeof(DrawModelInstanceData3D);
        [render_command_encoder //
            setVertexBuffer:dynamic_instance_buffer
                     offset:rotation_offset
                    atIndex:1];

        // 4. Bind Vertex Data (same MTLBuffer can back multiple poses at different offsets)
        id<MTLBuffer> vbo = (__bridge id<MTLBuffer>)(void*)model.vbo;
        NSUInteger vbo_byte_offset = model.vbo_offset * (VERTEX_ATTRIBUTE_COUNT * sizeof(uint16_t));

        if( vbo != last_bound_vbo || vbo_byte_offset != last_vbo_byte_offset )
        {
            [render_command_encoder //
                setVertexBuffer:vbo
                         offset:vbo_byte_offset
                        atIndex:0];
            last_bound_vbo = vbo;
            last_vbo_byte_offset = vbo_byte_offset;
        }

        // 5. Draw
        if( cmd.dynamic_index_count > 0 )
        {
            // Sorted Indexing (Dynamic Pool)
            NSUInteger index_byte_offset = cmd.dynamic_index_offset * sizeof(uint16_t);
            [render_command_encoder //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:cmd.dynamic_index_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:dynamic_index_buffer
                    indexBufferOffset:index_byte_offset];
        }
        else
        {
            // Static Indexing (Baked Cache)
            id<MTLBuffer> ebo = (__bridge id<MTLBuffer>)(void*)model.ebo;
            NSUInteger ebo_byte_offset = model.ebo_offset * sizeof(uint16_t);
            [render_command_encoder //
                drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:model.element_count
                            indexType:MTLIndexTypeUInt16
                          indexBuffer:ebo
                    indexBufferOffset:ebo_byte_offset];
        }
    }
}