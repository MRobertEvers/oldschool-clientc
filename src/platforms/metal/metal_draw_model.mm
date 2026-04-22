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
        if( fa->batch_id >= 0 )
        {
            void* vbo = cache.get_batch_vbo_for_chunk((uint32_t)fa->batch_id, fa->batch_chunk_index);
            if( !vbo )
                return false;
            out->vbo = vbo;
            out->batch_chunk_index = fa->batch_chunk_index;
        }
        else
        {
            out->vbo = fa->vbuf;
            out->batch_chunk_index = -1;
        }
        out->vertex_index_base = (int)(range->vtx_byte_offset / (uint32_t)stride);
        out->gpu_face_count = range->face_count;
        return out->vbo != nullptr;
    }

    Gpu3DCache<void*>::ModelEntry* entry = cache.get_model_entry(model_gpu_id);
    if( entry && entry->is_batched )
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

    Gpu3DCache<void*>::ModelBufferRange* range =
        ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
    if( !range )
    {
        if( !build_model_instance(ctx->renderer, ctx->device, model, mid_draw) )
            return;
        range = ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
    }
    if( !range )
        return;

    MetalDrawBuffersResolved buf = {};
    if( !metal_resolve_model_draw_buffers(ctx->renderer->model_cache, mid_draw, range, &buf) )
        return;

    struct DashPosition draw_position = cmd->_model_draw.position;

    while( !ctx->bfo3d->append_model(
        ctx->game->sys_dash,
        model,
        &draw_position,
        ctx->game->view_port,
        ctx->game->camera,
        buf.batch_chunk_index,
        buf.vbo,
        buf.vertex_index_base,
        buf.gpu_face_count) )
    {
        metal_flush_3d(ctx, ctx->bfo3d);
        ctx->bfo3d->begin_pass();
    }
}
