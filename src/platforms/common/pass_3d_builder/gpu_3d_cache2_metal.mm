#include "gpu_3d_cache2.h"
#import <Metal/Metal.h>

void
GPU3DCache2BatchSubmitMetal(
    GPU3DCache2& cache,
    id<MTLDevice> metal_device)
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
    GPUResourceHandle vbo_handle = (__bridge_retained void*)batched_vbo;
    GPUResourceHandle ebo_handle = (__bridge_retained void*)batched_ebo;

    // 2. Link the Cache
    // Iterate through every model we added to this batch and update the global cache
    const auto& tracking_data = cache.BatchGetTrackingData();

    GPUModelData model_updated;
    for( const BatchedQueueModel& batched_model : tracking_data )
    {
        model_updated.vbo = vbo_handle;
        model_updated.ebo = ebo_handle;

        // Note: These are index offsets, not byte offsets.
        // The draw loop will multiply them by sizeof(uint16_t).
        model_updated.vbo_offset = batched_model.vbo_start;
        model_updated.ebo_offset = batched_model.ebo_start;

        // Faces * 3 = Indices
        model_updated.element_count = batched_model.face_count * 3;
        model_updated.valid = true;

        // Assign to the global cache
        cache.SetModelData(batched_model.model_id, model_updated);
    }

    // 3. Cleanup CPU Memory
    // Now that the GPU has the data and the cache is wired up, we can destroy the std::vectors
    cache.BatchEnd();
}