#ifndef PLATFORMS_OPENGL3_OPENGL3_ADD_MODEL_DRAW_SCENERY_H
#define PLATFORMS_OPENGL3_OPENGL3_ADD_MODEL_DRAW_SCENERY_H

#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "platforms/opengl3/pass_3d_builder2_opengl3.h"

#include <cstdint>

void
opengl3_add_model_draw_scenery(
    Pass3DBuilder2OpenGL3& builder,
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    uint16_t model_id,
    bool use_animation,
    uint8_t animation_index,
    uint8_t frame_index,
    const uint32_t* sorted_indices,
    uint32_t index_count);

#endif
