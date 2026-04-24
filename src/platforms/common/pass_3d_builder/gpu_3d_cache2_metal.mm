#include "gpu_3d_cache2_metal.h"
#import <Metal/Metal.h>

void
GPU3DCache2BatchSubmitMetal(
    GPU3DCache2& cache,
    id<MTLDevice> metal_device,
    BatchBuffers& out_batch_buffers)
{
    uint32_t vbo_size = cache.BatchGetVBOSize() * sizeof(uint16_t);
    uint32_t ebo_size = cache.BatchGetEBOSize() * sizeof(uint16_t);

    if( vbo_size == 0 || ebo_size == 0 )
    {
        cache.BatchEnd(); // Empty batch, just cleanup
        return;
    }

    // 1. Upload to Metal
    // Use StorageModeShared for unified memory (Apple Silicon/iOS) or Managed for discrete Macs
    id<MTLBuffer> batched_vbo = //
        [metal_device newBufferWithBytes:cache.BatchGetVBO()
                                  length:vbo_size
                                 options:MTLResourceStorageModeShared];

    id<MTLBuffer> batched_ebo = //
        [metal_device newBufferWithBytes:cache.BatchGetEBO()
                                  length:ebo_size
                                 options:MTLResourceStorageModeShared];

    // Convert ARC pointers to opaque uintptr_t handles
    GPUResourceHandle vbo_handle = (GPUResourceHandle)(__bridge_retained void*)batched_vbo;
    GPUResourceHandle ebo_handle = (GPUResourceHandle)(__bridge_retained void*)batched_ebo;

    // 2. Link the Cache — one GPU pose slot per batched entry (shared VBO/EBO, unique offsets).
    const auto& tracking_data = cache.BatchGetTrackingData();

    GPUModelPosedData pose_data;
    for( const BatchedQueueModel& batched_model : tracking_data )
    {
        if( batched_model.pose_id >= MAX_POSE_COUNT )
            continue;

        pose_data.vbo = vbo_handle;
        pose_data.ebo = ebo_handle;

        // Note: These are index offsets, not byte offsets.
        // The draw loop will multiply them by sizeof(uint16_t).
        pose_data.vbo_offset = batched_model.vbo_start;
        pose_data.ebo_offset = batched_model.ebo_start;

        // Faces * 3 = Indices
        pose_data.element_count = batched_model.face_count * 3;
        pose_data.valid = true;

        cache.SetModelPose(batched_model.model_id, batched_model.pose_id, pose_data);
        cache.SetModelAnimationOffset(
            batched_model.model_id, batched_model.animation_index, batched_model.vbo_start);
    }

    // 3. Cleanup CPU Memory
    // Now that the GPU has the data and the cache is wired up, we can destroy the std::vectors
    cache.BatchEnd();

    out_batch_buffers.vbo = batched_vbo;
    out_batch_buffers.ebo = batched_ebo;
}

id<MTLTexture>
GPU3DCache2SubmitAtlasMetal(
    GPU3DCache2& cache,
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