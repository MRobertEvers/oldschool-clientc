// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_metal.h"
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_frame_event_model_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->renderer->mtl_device )
        return;

    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    if( !model || model_id <= 0 )
        return;

    if( dashmodel__is_ground_va(model) )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    GPU3DCache2Resource prev =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)model_id);
    if( prev.vbo )
        CFRelease((CFTypeRef)(void*)prev.vbo);
    if( prev.ebo )
        CFRelease((CFTypeRef)(void*)prev.ebo);

    id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->renderer->mtl_device;
    MetalBatchBuffer solo{};
    const GPU3DBakeTransform identity_bake{};
    metal_cache2_batch_push_model_mesh(
        ctx,
        solo,
        model,
        model_id,
        kSceneBatchSlotNone,
        GPU_MODEL_ANIMATION_NONE_IDX,
        0u,
        identity_bake);

    BatchBuffers v2bufs{};
    GPU3DCache2BatchSubmitMetal(
        ctx->renderer->model_cache2,
        solo,
        device,
        v2bufs,
        kSceneBatchSlotNone,
        (ToriRS_UsageHint)cmd->_model_load.usage_hint);

    ctx->renderer->model_cache2.SetStandaloneRetainedBuffers(
        (uint16_t)model_id,
        (GPUResourceHandle)(uintptr_t)v2bufs.vbo,
        (GPUResourceHandle)(uintptr_t)v2bufs.ebo);
    v2bufs.vbo = nullptr;
    v2bufs.ebo = nullptr;
}

void
metal_frame_event_model_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
        return;
    GPU3DCache2Resource solo =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)mid);
    if( solo.vbo )
        CFRelease((CFTypeRef)(void*)solo.vbo);
    if( solo.ebo )
        CFRelease((CFTypeRef)(void*)solo.ebo);
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

    if( !ctx->renderer->pass.current_model_batch_active )
        return;
    const uint32_t bid = ctx->renderer->pass.current_model_batch_id;

    struct DashModel* model = cmd->_animation_load.model;
    dashmodel_animate(model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t gpu_seg = cmd->_animation_load.animation_index == 1
                                ? GPU_MODEL_ANIMATION_SECONDARY_IDX
                                : GPU_MODEL_ANIMATION_PRIMARY_IDX;
    const GPU3DBakeTransform anim_bake =
        ctx->renderer->model_cache2.GetModelBakeTransform((uint16_t)mid);
    metal_cache2_batch_push_model_mesh(
        ctx, ctx->renderer->batch3d_staging, model, mid, bid, gpu_seg, (uint16_t)fidx, anim_bake);
    (void)cmd->_animation_load.usage_hint;
}
