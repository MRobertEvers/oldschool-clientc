// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

static struct DashVertexArray*
metal_find_staged_va_for_face_array(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    const struct DashFaceArray* fa)
{
    if( !renderer || !fa || fa->count <= 0 )
        return nullptr;

    int max_idx = 0;
    for( int f = 0; f < fa->count; ++f )
    {
        const int ia = (int)fa->indices_a[f];
        const int ib = (int)fa->indices_b[f];
        const int ic = (int)fa->indices_c[f];
        max_idx = std::max(max_idx, std::max(ia, std::max(ib, ic)));
    }

    struct DashVertexArray* best = nullptr;
    int best_count = INT_MAX;
    for( const auto& kv : renderer->mtl_va_staging )
    {
        struct DashVertexArray* v = kv.second;
        if( !v || v->vertex_count <= max_idx )
            continue;
        if( v->vertex_count < best_count )
        {
            best_count = v->vertex_count;
            best = v;
        }
    }
    return best;
}

void
metal_frame_event_batch_vertex_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    struct DashVertexArray* va = cmd->_vertex_array_load.array;
    const int va_id = cmd->_vertex_array_load.array_id;
    if( !va || va_id < 0 || bid == 0 )
        return;
    ctx->renderer->mtl_va_staging[va_id] = va;
    ctx->renderer->model_cache.register_batch_vertex_array(
        bid, va_id, (uint32_t)va->vertex_count, (int)sizeof(MetalVertex));
}

void
metal_frame_event_batch_face_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    struct DashFaceArray* fa = cmd->_face_array_load.array;
    const int fa_id = cmd->_face_array_load.array_id;
    if( !fa || fa_id < 0 || bid == 0 || fa->count <= 0 )
        return;

    struct DashVertexArray* va = metal_find_staged_va_for_face_array(ctx->renderer, fa);
    if( !va )
    {
        fprintf(stderr, "[metal] BATCH3D_FACE_ARRAY_LOAD: no matching staged vertex array\n");
        return;
    }

    const int fc = fa->count;
    std::vector<int> pnm((size_t)fc, 0);
    std::vector<MetalVertex> verts((size_t)fc * 3u);
    std::vector<int> per_face_tex((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = fa->texture_ids ? (int)fa->texture_ids[f] : -1;
        per_face_tex[(size_t)f] = raw;
        MetalVertex tri[3];
        if( !fill_face_corner_vertices_from_fa(va, fa, f, raw, pnm.data(), tri) )
        {
            metal_vertex_fill_invisible(&tri[0]);
            metal_vertex_fill_invisible(&tri[1]);
            metal_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
    }

    ctx->renderer->model_cache.accumulate_batch_face_array(
        bid, fa_id, verts.data(), (int)sizeof(MetalVertex), fc * 3, fc, per_face_tex.data());
}

void
metal_patch_batched_va_model_verts(MetalRenderCtx* ctx, struct DashModel* model)
{
    if( !ctx || !ctx->renderer || !model || !dashmodel__is_ground_va(model) )
        return;
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( bid == 0 )
        return;
    struct DashModelVAGround* vg = dashmodel__as_ground_va(model);
    if( !vg->vertex_array || !vg->face_array )
        return;
    const int va_id = vg->vertex_array->scene2_gpu_id;
    const int fa_id = vg->face_array->scene2_gpu_id;
    auto it = ctx->renderer->mtl_va_staging.find(va_id);
    if( it == ctx->renderer->mtl_va_staging.end() || !it->second )
        return;
    struct DashVertexArray* va = it->second;
    struct DashFaceArray* fa = vg->face_array;
    const uint32_t first = dashmodel_va_first_face_index(model);
    const int fc = dashmodel_face_count(model);
    if( fc <= 0 )
        return;
    const int pnm_val = (int)first;
    Gpu3DCache<void*>& cache = ctx->renderer->model_cache;
    if( !cache.patch_face_array_pnm_va_range(fa_id, first, fc, pnm_val) )
        return;
    const int* pnm = cache.face_array_pnm_ptr(fa_id);
    if( !pnm )
        return;
    const int vs = (int)sizeof(MetalVertex);
    size_t slice_len = 0;
    uint8_t* dst_base =
        cache.mutable_batch_face_vert_slice(bid, fa_id, first, fc, vs, &slice_len);
    if( !dst_base || slice_len == 0 )
        return;
    MetalVertex tri[3];
    for( int f = (int)first; f < (int)first + fc; ++f )
    {
        int raw = fa->texture_ids ? (int)fa->texture_ids[f] : -1;
        if( !fill_face_corner_vertices_from_fa(va, fa, f, raw, pnm, tri) )
        {
            metal_vertex_fill_invisible(&tri[0]);
            metal_vertex_fill_invisible(&tri[1]);
            metal_vertex_fill_invisible(&tri[2]);
        }
        uint8_t* dst = dst_base + (size_t)(f - (int)first) * 3u * (size_t)vs;
        memcpy(dst, tri, 3u * (size_t)vs);
    }
}
