// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include <vector>

void
metal_add_model_draw_scenery(
    Pass3DBuilder2Metal& builder,
    const GPU3DCache2<GPU3DMeshVertexMetal>& cache,
    uint16_t model_id,
    bool use_animation,
    uint8_t animation_index,
    uint8_t frame_index,
    const uint32_t* sorted_indices,
    uint32_t index_count)
{
    if( !builder.IsBuilding() )
        return;
    if( sorted_indices == nullptr || index_count == 0u )
        return;

    const GPUModelPosedData pose =
        cache.GetModelPoseForDraw(model_id, use_animation, (int)animation_index, (int)frame_index);
    if( !pose.valid || pose.gpu_batch_id == 0u )
        return;
    if( pose.scene_batch_id >= kGPU3DCache2MaxSceneBatches )
        return;

    GPUResourceHandle mesh_vbo = pose.vbo;
    {
        const GPU3DCache2SceneBatchEntry* be = cache.SceneBatchGet(pose.scene_batch_id);
        if( be && be->resource.valid )
            mesh_vbo = be->resource.vbo;
    }

    builder.AppendSortedDraw(mesh_vbo, pose.vbo_offset, sorted_indices, index_count);
}
