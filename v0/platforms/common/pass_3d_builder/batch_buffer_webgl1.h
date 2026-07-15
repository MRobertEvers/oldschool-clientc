#ifndef BATCH_BUFFER_WEBGL1_H
#define BATCH_BUFFER_WEBGL1_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <vector>

/** CPU-only merged mesh staging for WebGL1 (stride-48 interleaved vertex). */
struct WebGL1BatchBuffer
{
    using vertex_type = GPU3DMeshVertexWebGL1;
    using index_type = uint16_t;

    std::vector<GPU3DMeshVertexWebGL1> vbo;
    std::vector<uint16_t> ebo;
    std::vector<BatchedQueueModel> tracking;
};

#endif
