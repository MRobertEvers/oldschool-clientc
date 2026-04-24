// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
metal_frame_event_model_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    if( !model )
        return;
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
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

    if( !ctx->renderer->model_cache.get_instance(mid, 0, 0) )
        metal_dispatch_model_load(ctx->renderer, ctx->device, mid, model, false, 0u);
}

void
metal_frame_event_model_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
        return;
    auto* entry = ctx->renderer->model_cache.get_model_entry(mid);
    if( entry && !entry->is_batched )
    {
        for( auto& anim : entry->anims )
            for( auto& frame : anim.frames )
            {
                if( frame.loaded && frame.owns_buffer && frame.buffer )
                {
                    CFRelease(frame.buffer);
                    frame.buffer = nullptr;
                }
                if( frame.loaded && frame.owns_index_buffer && frame.index_buffer )
                {
                    CFRelease(frame.index_buffer);
                    frame.index_buffer = nullptr;
                }
            }
    }
    ctx->renderer->model_cache.unload_model(mid);
}

void
metal_frame_event_model_animation_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_animation_load.model_gpu_id;
    const int aid = cmd->_animation_load.anim_id;
    const int fidx = cmd->_animation_load.frame_index;
    const int anim_slot = cmd->_animation_load.animation_index;
    (void)anim_slot; /* Reserved for GPU3DCache2 animation_offsets; v1 cache keys on anim_id. */
    if( !cmd->_animation_load.model || mid <= 0 )
        return;
    if( !ctx->renderer->model_cache.get_instance(mid, aid, fidx) )
        build_model_instance(
            ctx->renderer, ctx->device, cmd->_animation_load.model, mid, aid, fidx);
}
