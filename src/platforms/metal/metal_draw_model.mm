// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

bool
metal_resolve_model_draw_buffers(
    Gpu3DCache<void*>& cache,
    int model_gpu_id,
    Gpu3DCache<void*>::ModelBufferRange* range,
    MetalDrawBuffersResolved* out)
{
    if( !range || !out )
        return false;
    const int stride = (int)sizeof(MetalVertex);
    if( stride <= 0 )
        return false;

    if( range->fa_gpu_id >= 0 )
    {
        Gpu3DCache<void*>::FaceArrayEntry* fa = cache.get_face_array(range->fa_gpu_id);
        if( !fa )
            return false;
        if( fa->batch_id < 0 )
            return false;
        void* vbo = cache.get_batch_vbo_for_chunk((uint32_t)fa->batch_id, fa->batch_chunk_index);
        if( !vbo )
            return false;
        out->vbo = vbo;
        out->batch_chunk_index = fa->batch_chunk_index;
        out->vertex_index_base = (int)(range->vtx_byte_offset / (uint32_t)stride);
        out->gpu_face_count = range->face_count;
        return out->vbo != nullptr;
    }

    Gpu3DCache<void*>::ModelEntry* entry = cache.get_model_entry(model_gpu_id);
    if( entry && entry->is_batched && !range->buffer )
    {
        void* vbo = cache.get_batch_vbo_for_model(model_gpu_id);
        if( !vbo )
            return false;
        out->vbo = vbo;
        out->batch_chunk_index = range->batch_chunk_index;
        out->vertex_index_base = (int)(range->vtx_byte_offset / (uint32_t)stride);
        out->gpu_face_count = range->face_count;
        return true;
    }

    out->vbo = range->buffer;
    out->batch_chunk_index = -1;
    out->vertex_index_base = (int)(range->vtx_byte_offset / (uint32_t)stride);
    out->gpu_face_count = range->face_count;
    return out->vbo != nullptr;
}

void
metal_frame_event_model_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    /* Fire the deferred world-rect clear before the first model of each 3D pass.
       metal_flush_2d is a no-op if 2D was already flushed; calling it here ensures
       any lingering buffered 2D is encoded BEFORE the clear overwrites the world area. */
    if( ctx->world_clear_pending )
    {
        metal_flush_2d(ctx);
        struct ToriRSRenderCommand wc = { 0 };
        wc.kind = TORIRS_GFX_CLEAR_RECT;
        wc._clear_rect.x = ctx->world_clear_x;
        wc._clear_rect.y = ctx->world_clear_y;
        wc._clear_rect.w = ctx->world_clear_w;
        wc._clear_rect.h = ctx->world_clear_h;
        // if( wc._clear_rect.w > 0 && wc._clear_rect.h > 0 )
        //     metal_frame_event_clear_rect(ctx, &wc);
        ctx->world_clear_pending = false;
    }

    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    if( !ctx->bfo3d )
        return;

    const int mid_draw = cmd->_model_draw.model_id;
    if( mid_draw <= 0 )
        return;

    const uint64_t mk = cmd->_model_draw.model_key;
    int anim_id = 0;
    int frame_index = 0;
    torirs_model_cache_key_decode(mk, &anim_id, &frame_index);

    Gpu3DCache<void*>::ModelBufferRange* range =
        ctx->renderer->model_cache.get_instance(mid_draw, anim_id, frame_index);
    if( !range )
    {
        if( !build_model_instance(
                ctx->renderer, ctx->device, model, mid_draw, anim_id, frame_index) )
        {
            if( anim_id == 0 && frame_index == 0 )
                return;
            if( !build_model_instance(ctx->renderer, ctx->device, model, mid_draw, 0, 0) )
                return;
            range = ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
        }
        else
            range = ctx->renderer->model_cache.get_instance(mid_draw, anim_id, frame_index);
    }
    if( !range )
        return;

    MetalDrawBuffersResolved buf = {};
    if( !metal_resolve_model_draw_buffers(ctx->renderer->model_cache, mid_draw, range, &buf) )
        return;

    struct DashPosition draw_position = cmd->_model_draw.position;

    Gpu3DAngleEncoding angle_enc = Gpu3DAngleEncoding::DashR2pi2048;
    if( Gpu3DCache<void*>::ModelEntry* me = ctx->renderer->model_cache.get_model_entry(mid_draw) )
        angle_enc = me->angle_encoding;

    float cos_yaw = 1.0f;
    float sin_yaw = 0.0f;
    switch( angle_enc )
    {
    case Gpu3DAngleEncoding::DashR2pi2048:
    case Gpu3DAngleEncoding::Reserved1:
    case Gpu3DAngleEncoding::Reserved2:
    default:
        metal_prebake_model_yaw_cos_sin(draw_position.yaw, &cos_yaw, &sin_yaw);
        break;
    }

    while( !ctx->bfo3d->append_model(
        ctx->game->sys_dash,
        model,
        &draw_position,
        ctx->game->view_port,
        ctx->game->camera,
        buf.batch_chunk_index,
        buf.vbo,
        buf.vertex_index_base,
        buf.gpu_face_count,
        cos_yaw,
        sin_yaw,
        angle_enc) )
    {
        metal_flush_3d(ctx, ctx->bfo3d);
        ctx->bfo3d->begin_pass();
    }
}
