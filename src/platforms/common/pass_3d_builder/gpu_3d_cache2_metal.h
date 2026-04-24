#pragma once
#ifdef __OBJC__
#include "gpu_3d_cache2.h"
#import <Metal/Metal.h>

struct BatchBuffers
{
    id<MTLBuffer> vbo;
    id<MTLBuffer> ebo;
};

void
GPU3DCache2BatchSubmitMetal(
    GPU3DCache2& cache,
    id<MTLDevice> metal_device,
    BatchBuffers& out_batch_buffers);

id<MTLTexture>
GPU3DCache2SubmitAtlasMetal(
    GPU3DCache2& cache,
    id<MTLDevice> metal_device);

#endif // __OBJC__
