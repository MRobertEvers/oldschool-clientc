#include "gpu_3d_cache2_metal.h"

#include "platforms/common/gpu_3d.h"
#import <Metal/Metal.h>

#include <cstdio>

static_assert(
    sizeof(GPU3DMeshVertexMetal) == 44u,
    "GPU3DMeshVertex must match Shaders.metal MetalVertexPacked");

void
GPU3DCache2BatchSubmitMetal(
    GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    id<MTLDevice> metal_device,
    BatchBuffers& out_batch_buffers,
    uint32_t batch_id)
{
    const uint32_t vbo_size =
        cache.BatchGetVBOVertexCount() * (uint32_t)sizeof(GPU3DMeshVertexMetal);
    uint32_t ebo_size = cache.BatchGetEBOSize() * sizeof(uint16_t);

    if( vbo_size == 0 || ebo_size == 0 )
    {
        cache.BatchEnd(); // Empty batch, just cleanup
        return;
    }

    // 1. Upload to Metal
    // Use StorageModeShared for unified memory (Apple Silicon/iOS) or Managed for discrete Macs
    id<MTLBuffer> batched_vbo = //
        [metal_device newBufferWithBytes:(const void*)cache.BatchGetVBO()
                                  length:(NSUInteger)vbo_size
                                 options:MTLResourceStorageModeShared];

    id<MTLBuffer> batched_ebo = //
        [metal_device newBufferWithBytes:cache.BatchGetEBO()
                                  length:ebo_size
                                 options:MTLResourceStorageModeShared];

    if( !batched_vbo || !batched_ebo )
    {
        fprintf(
            stderr,
            "GPU3DCache2BatchSubmitMetal: newBufferWithBytes failed (vbo=%p ebo=%p vbo_size=%u "
            "ebo_size=%u)\n",
            batched_vbo,
            batched_ebo,
            vbo_size,
            ebo_size);
        cache.BatchEnd();
        batched_vbo = nil;
        batched_ebo = nil;
        return;
    }

    const uint32_t vertex_count = cache.BatchGetVBOVertexCount();
    const uint16_t* ebo_cpu = cache.BatchGetEBO();
    const uint32_t ebo_index_count = cache.BatchGetEBOSize();
    uint32_t max_index = 0;
    for( uint32_t i = 0; i < ebo_index_count; ++i )
    {
        max_index = ebo_cpu[i] > max_index ? ebo_cpu[i] : max_index;
    }
    if( vertex_count == 0 || max_index >= vertex_count )
    {
        fprintf(
            stderr,
            "GPU3DCache2BatchSubmitMetal: EBO index out of range (max_index=%u vertex_count=%u)\n",
            max_index,
            vertex_count);
        cache.BatchEnd();
        batched_vbo = nil;
        batched_ebo = nil;
        return;
    }

    // 2. Link the Cache — one GPU pose slot per batched entry. Pose slots store non-owning
    //    `__bridge void*` handles; the batch map / `BatchBuffers` own +1 via `__bridge_retained`
    //    at hand-off below.
    const auto& tracking_data = cache.BatchGetTrackingData();

    const GPUResourceHandle vbo_handle = (GPUResourceHandle)(__bridge void*)batched_vbo;
    const GPUResourceHandle ebo_handle = (GPUResourceHandle)(__bridge void*)batched_ebo;

    GPUModelPosedData pose_data;
    for( const BatchedQueueModel& batched_model : tracking_data )
    {
        pose_data.vbo = vbo_handle;
        pose_data.ebo = ebo_handle;
        pose_data.gpu_batch_id = batch_id;

        // vbo_start is first vertex index of this model slice in the merged VBO.
        pose_data.vbo_offset = batched_model.vbo_start;
        // ebo_start is first index in uint16 index buffer (triangle list).
        pose_data.ebo_offset = batched_model.ebo_start;

        // Faces * 3 = Indices
        pose_data.element_count = batched_model.face_count * 3;
        pose_data.valid = true;

        cache.SetModelPose(batched_model.model_id, batched_model.pose_index, pose_data);
    }

    // 3. Cleanup CPU Memory
    // Now that the GPU has the data and the cache is wired up, we can destroy the std::vectors
    cache.BatchEnd();

    /* Transfer ownership to out_batch_buffers before locals go out of scope (ARC + void*). */
    out_batch_buffers.vbo = (__bridge_retained void*)batched_vbo;
    out_batch_buffers.ebo = (__bridge_retained void*)batched_ebo;
    batched_vbo = nil;
    batched_ebo = nil;
}

id<MTLTexture>
GPU3DCache2SubmitAtlasMetal(
    GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    id<MTLDevice> metal_device)
{
    if( !cache.HasBufferedAtlasData() )
        return nil;

    // 1. Describe the Texture (The "Blueprint")
    MTLTextureDescriptor* texDesc = [MTLTextureDescriptor //
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:cache.GetAtlasWidth()
                                    height:cache.GetAtlasHeight()
                                 mipmapped:NO];

    // We want the GPU to be able to read this during rendering
    texDesc.usage = MTLTextureUsageShaderRead;

    // 2. Create the physical Texture resource
    id<MTLTexture> atlas_texture = [metal_device //
        newTextureWithDescriptor:texDesc];

    // 3. Copy the raw bytes from your cache's std::vector into the Texture regions
    MTLRegion region = MTLRegionMake2D(0, 0, cache.GetAtlasWidth(), cache.GetAtlasHeight());

    [atlas_texture //
        replaceRegion:region
          mipmapLevel:0
            withBytes:cache.GetAtlasPixelData()
          bytesPerRow:cache.GetAtlasWidth() * 4]; // 4 bytes for RGBA

    return atlas_texture;
}