#pragma once

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/batch_buffer_webgl1.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#ifdef __EMSCRIPTEN__

#include <GLES2/gl2.h>

#include <cstdint>

/** Out-parameters from `GPU3DCache2BatchSubmitWebGL1`: GL buffer names owned by caller / batch map.
 */
struct BatchBuffersWebGL1
{
    GLuint vbo = 0;
    GLuint ebo = 0;
};

void
GPU3DCache2BatchSubmitWebGL1(
    GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    const WebGL1BatchBuffer& batch,
    BatchBuffersWebGL1& out_batch_buffers,
    SceneBatchId scene_batch_id,
    ToriRS_UsageHint usage_hint);

/** Creates or replaces RGBA atlas texture from cache pixel data. Returns texture name or 0. */
GLuint
GPU3DCache2SubmitAtlasWebGL1(GPU3DCache2<GPU3DMeshVertexWebGL1>& cache);

#endif
