// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_metal.h"
#include "platforms/metal/metal_internal.h"

void
metal_frame_event_model_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->device )
        return;

    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    if( !model || model_id <= 0 )
        return;

    if( dashmodel__is_ground_va(model) )
        return;
    if(
        !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const uint32_t solo_bid = kMetalSoloModelBatchBase + (uint32_t)model_id;
    auto& batch_map = ctx->renderer->model_cache2_batch_map;
    auto prev = batch_map.find(solo_bid);
    if( prev != batch_map.end() )
    {
        if( prev->second.vbo )
            CFRelease(prev->second.vbo);
        if( prev->second.ebo )
            CFRelease(prev->second.ebo);
        batch_map.erase(prev);
    }

    batch_map[solo_bid] = Platform2_SDL2_Renderer_Metal::MetalCache2BatchEntry{};
    ctx->renderer->model_cache2.BatchBegin();
    metal_cache2_batch_push_model_mesh(
        ctx, model, model_id, solo_bid, GPU_MODEL_ANIMATION_NONE_IDX, 0u);

    BatchBuffers v2bufs{};
    GPU3DCache2BatchSubmitMetal(ctx->renderer->model_cache2, ctx->device, v2bufs, solo_bid);

    auto it = batch_map.find(solo_bid);
    if( it != batch_map.end() )
    {
        if( it->second.vbo )
            CFRelease(it->second.vbo);
        if( it->second.ebo )
            CFRelease(it->second.ebo);
        /* v2bufs already holds +1 from GPU3DCache2BatchSubmitMetal; move, do not retain again. */
        it->second.vbo = v2bufs.vbo;
        it->second.ebo = v2bufs.ebo;
        v2bufs.vbo = nullptr;
        v2bufs.ebo = nullptr;
    }
}

void
metal_frame_event_model_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
        return;
    const uint32_t solo_bid = kMetalSoloModelBatchBase + (uint32_t)mid;
    auto& batch_map = ctx->renderer->model_cache2_batch_map;
    auto it = batch_map.find(solo_bid);
    if( it != batch_map.end() )
    {
        if( it->second.vbo )
            CFRelease(it->second.vbo);
        if( it->second.ebo )
            CFRelease(it->second.ebo);
        batch_map.erase(it);
    }
    ctx->renderer->model_cache2.ClearModel((uint16_t)mid);
}

void
metal_frame_event_model_animation_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_animation_load.model_gpu_id;
    const int fidx = cmd->_animation_load.frame_index;
    if( !cmd->_animation_load.model || mid <= 0 )
        return;

    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( bid == 0 )
        return;

    struct DashModel* model = cmd->_animation_load.model;
    dashmodel_animate(model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t gpu_seg = cmd->_animation_load.animation_index == 1
                                ? GPU_MODEL_ANIMATION_SECONDARY_IDX
                                : GPU_MODEL_ANIMATION_PRIMARY_IDX;
    metal_cache2_batch_push_model_mesh(ctx, model, mid, bid, gpu_seg, (uint16_t)fidx);
}
