#ifndef BATCH_BUFFER_OPENGL3_H
#define BATCH_BUFFER_OPENGL3_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <vector>

/** CPU-only merged mesh staging for OpenGL3 (stride-48 interleaved vertex, uint32 indices for GL_UNSIGNED_INT). */
struct OpenGL3BatchBuffer
{
    using vertex_type = GPU3DMeshVertexWebGL1;
    using index_type = uint32_t;

    std::vector<GPU3DMeshVertexWebGL1> vbo;
    std::vector<uint32_t> ebo;
    std::vector<BatchedQueueModel> tracking;
};

#endif
