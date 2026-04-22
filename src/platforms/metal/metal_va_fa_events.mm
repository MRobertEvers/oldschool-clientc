// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include "tori_rs_render.h"

#include <algorithm>
#include <climits>

/** Terrain shares one FA across many VA tile models; each model's first_face_index is the PNM
 *  reference face for all faces in that range. Those MODEL_LOAD commands follow FACE_ARRAY_LOAD in
 *  the same render command buffer, so we recover the map here without DashFaceArray side storage.
 */
static void
metal_fill_pnm_from_upcoming_va_models(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* buf,
    struct DashFaceArray* fa,
    int* pnm_out)
{
    if( !game || !buf || !fa || !pnm_out || fa->count <= 0 )
        return;
    const int ncmd = LibToriRS_RenderCommandBufferCount(buf);
    for( int i = game->at_render_command_index; i < ncmd; ++i )
    {
        struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferAt(buf, i);
        struct DashModel* m = nullptr;
        if( c->kind == TORIRS_GFX_MODEL_LOAD || c->kind == TORIRS_GFX_BATCH3D_MODEL_LOAD )
            m = c->_model_load.model;
        else
            continue;
        if( !m || !dashmodel__is_ground_va(m) )
            continue;
        if( dashmodel_va_face_array_const(m) != fa )
            continue;
        const uint32_t first = dashmodel_va_first_face_index(m);
        const int fcnt = dashmodel_face_count(m);
        if( fcnt <= 0 )
            continue;
        for( int k = 0; k < fcnt; ++k )
        {
            const int gi = (int)first + k;
            if( gi >= 0 && gi < fa->count )
                pnm_out[gi] = (int)first;
        }
    }
}

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
metal_frame_event_vertex_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashVertexArray* va = cmd->_vertex_array_load.array;
    const int va_id = cmd->_vertex_array_load.array_id;
    if( !va || va_id < 0 || va->vertex_count <= 0 )
        return;
    ctx->renderer->mtl_va_staging[va_id] = va;
    ctx->renderer->model_cache.register_vertex_array(
        va_id, nullptr, false, (uint32_t)va->vertex_count, (int)sizeof(MetalVertex));
}

void
metal_frame_event_vertex_array_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int va_id = cmd->_vertex_array_load.array_id;
    ctx->renderer->mtl_va_staging.erase(va_id);
    ctx->renderer->model_cache.unload_vertex_array(va_id);
}

static void
metal_bake_and_register_face_array_standalone(
    MetalRenderCtx* ctx,
    int fa_id,
    struct DashVertexArray* va,
    struct DashFaceArray* fa)
{
    const int fc = fa->count;
    std::vector<int> pnm((size_t)fc, 0);
    metal_fill_pnm_from_upcoming_va_models(ctx->game, ctx->render_commands, fa, pnm.data());
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

    const size_t vbytes = verts.size() * sizeof(MetalVertex);
    id<MTLBuffer> vbo = [ctx->device newBufferWithBytes:verts.data()
                                                 length:(NSUInteger)vbytes
                                                options:MTLResourceStorageModeShared];
    std::vector<uint32_t> idx((size_t)fc * 3u);
    for( size_t i = 0; i < idx.size(); ++i )
        idx[i] = (uint32_t)i;
    id<MTLBuffer> ibo = [ctx->device newBufferWithBytes:idx.data()
                                                 length:idx.size() * sizeof(uint32_t)
                                                options:MTLResourceStorageModeShared];
    ctx->renderer->model_cache.register_face_array(
        fa_id,
        vbo ? (__bridge_retained void*)vbo : nullptr,
        ibo ? (__bridge_retained void*)ibo : nullptr,
        true,
        true,
        fc,
        per_face_tex.data());
}

void
metal_frame_event_face_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashFaceArray* fa = cmd->_face_array_load.array;
    const int fa_id = cmd->_face_array_load.array_id;
    if( !fa || fa_id < 0 || fa->count <= 0 )
        return;

    struct DashVertexArray* va = metal_find_staged_va_for_face_array(ctx->renderer, fa);
    if( !va )
    {
        fprintf(stderr, "[metal] FACE_ARRAY_LOAD: no matching staged vertex array\n");
        return;
    }

    Gpu3DCache<void*>::FaceArrayEntry* existing = ctx->renderer->model_cache.get_face_array(fa_id);
    if( existing && existing->owns_vbuf && existing->vbuf )
        CFRelease(existing->vbuf);
    if( existing && existing->owns_ibuf && existing->ibuf )
        CFRelease(existing->ibuf);
    ctx->renderer->model_cache.unload_face_array(fa_id);

    metal_bake_and_register_face_array_standalone(ctx, fa_id, va, fa);
}

void
metal_frame_event_face_array_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int fa_id = cmd->_face_array_load.array_id;
    Gpu3DCache<void*>::FaceArrayEntry* e = ctx->renderer->model_cache.get_face_array(fa_id);
    if( e && e->owns_vbuf && e->vbuf )
        CFRelease(e->vbuf);
    if( e && e->owns_ibuf && e->ibuf )
        CFRelease(e->ibuf);
    ctx->renderer->model_cache.unload_face_array(fa_id);
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
    metal_fill_pnm_from_upcoming_va_models(ctx->game, ctx->render_commands, fa, pnm.data());
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
