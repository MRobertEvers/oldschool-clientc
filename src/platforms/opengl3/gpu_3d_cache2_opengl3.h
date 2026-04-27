#pragma once

#include "platforms/common/pass_3d_builder/batch_buffer_opengl3.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "platforms/common/gpu_3d.h"

#if defined(__APPLE__)
#    include <OpenGL/gl3.h>
#else
#    define GL_GLEXT_PROTOTYPES
#    include <GL/glcorearb.h>
#endif

#include <cstdint>

/** Out-parameters from `GPU3DCache2BatchSubmitOpenGL3`: GL buffer names owned by caller / batch map. */
struct BatchBuffersOpenGL3
{
    GLuint vbo = 0;
    GLuint ebo = 0;
};

void
GPU3DCache2BatchSubmitOpenGL3(
    GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    const OpenGL3BatchBuffer& batch,
    BatchBuffersOpenGL3& out_batch_buffers,
    SceneBatchId scene_batch_id,
    ToriRS_UsageHint usage_hint);

/** Creates or replaces RGBA atlas texture from cache pixel data. Returns texture name or 0. */
GLuint
GPU3DCache2SubmitAtlasOpenGL3(GPU3DCache2<GPU3DMeshVertexWebGL1>& cache);
