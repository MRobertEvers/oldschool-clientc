#pragma once

#ifdef __EMSCRIPTEN__

#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#    include "platforms/webgl1/pass_3d_builder2_webgl1.h"

void
webgl1_add_model_draw_scenery(
    Pass3DBuilder2WebGL1& builder,
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    uint16_t model_id,
    bool use_animation,
    uint8_t animation_index,
    uint8_t frame_index,
    const uint16_t* sorted_indices,
    uint32_t index_count);

#endif
