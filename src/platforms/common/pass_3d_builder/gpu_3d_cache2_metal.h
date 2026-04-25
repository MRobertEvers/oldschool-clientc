#pragma once
#ifdef __OBJC__
#include "gpu_3d_cache2.h"
#import <Metal/Metal.h>

/** Out-parameters from `GPU3DCache2BatchSubmitMetal`: `vbo`/`ebo` are CF-retained `MTLBuffer*`. */
struct BatchBuffers
{
    void* vbo = nullptr;
    void* ebo = nullptr;
};

void
GPU3DCache2BatchSubmitMetal(
    GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    id<MTLDevice> metal_device,
    BatchBuffers& out_batch_buffers,
    uint32_t batch_id);

id<MTLTexture>
GPU3DCache2SubmitAtlasMetal(
    GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    id<MTLDevice> metal_device);

#endif // __OBJC__
