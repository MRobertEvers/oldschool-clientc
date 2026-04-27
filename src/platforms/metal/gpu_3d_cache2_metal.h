#pragma once
#ifdef __OBJC__
#include "platforms/common/pass_3d_builder/batch_buffer_metal.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
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
    const MetalBatchBuffer& batch,
    id<MTLDevice> metal_device,
    BatchBuffers& out_batch_buffers,
    SceneBatchId scene_batch_id,
    ToriRS_UsageHint usage_hint);

id<MTLTexture>
GPU3DCache2SubmitAtlasMetal(
    GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    id<MTLDevice> metal_device);

#endif // __OBJC__
