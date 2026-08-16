#ifndef BATCH_BUFFER_METAL_H
#define BATCH_BUFFER_METAL_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <vector>

/** CPU-only merged mesh staging for Metal (`GPU3DMeshVertexMetal` → `Shaders.metal`). */
struct MetalBatchBuffer
{
    using vertex_type = GPU3DMeshVertexMetal;
    using index_type = uint32_t;

    std::vector<GPU3DMeshVertexMetal> vbo;
    std::vector<uint32_t> ebo;
    std::vector<BatchedQueueModel> tracking;
};

#endif
