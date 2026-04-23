#include "platforms/webgl1/webgl1_internal.h"

#include "platforms/common/torirs_sprite_argb_rgba.h"
#include "platforms/common/torirs_sprite_draw_cpu.h"

void
wg1_frame_event_begin_3d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static void
wg1_texture_stat_mark(struct Platform2_SDL2_Renderer_WebGL1* renderer, int tex_id)
{
    if( !renderer || tex_id < 0 || tex_id >= 256 )
        return;
    if( !renderer->texture_id_ever_loaded[tex_id] )
    {
        renderer->texture_id_ever_loaded[tex_id] = true;
        ++renderer->texture_id_ever_loaded_count;
    }
}

void
wg1_frame_event_batch_texture_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    ctx->renderer->texture_cache.begin_batch(cmd->_batch.batch_id);
}

void
wg1_frame_event_batch_texture_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    ctx->renderer->texture_cache.end_batch(cmd->_batch.batch_id);
}

void
wg1_frame_event_texture_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* tex = cmd->_texture_load.texture_nullable;
    if( !r || !tex || !tex->texels || tex_id < 0 || tex_id >= 256 )
        return;

    wg1_texture_stat_mark(r, tex_id);
    wg1_world_atlas_ensure(r);
    if( !r->world_atlas_tex )
        return;

    const int w = tex->width;
    const int h = tex->height;
    int ox = 0, oy = 0;
    if( !wg1_world_atlas_alloc_rect(r, w, h, &ox, &oy) )
    {
        fprintf(stderr, "[webgl1] world atlas full; texture %d skipped\n", tex_id);
        return;
    }

    r->rgba_scratch.resize((size_t)w * (size_t)h * 4u);
    for( int p = 0; p < w * h; ++p )
    {
        int pix = tex->texels[p];
        r->rgba_scratch[(size_t)p * 4u + 0] = (uint8_t)((pix >> 16) & 0xFF);
        r->rgba_scratch[(size_t)p * 4u + 1] = (uint8_t)((pix >> 8) & 0xFF);
        r->rgba_scratch[(size_t)p * 4u + 2] = (uint8_t)(pix & 0xFF);
        r->rgba_scratch[(size_t)p * 4u + 3] = (uint8_t)((pix >> 24) & 0xFF);
    }

    glBindTexture(GL_TEXTURE_2D, r->world_atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        ox,
        oy,
        w,
        h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        r->rgba_scratch.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const float fw = (float)kWebGL1WorldAtlasPageW;
    const float fh = (float)kWebGL1WorldAtlasPageH;
    WebGL1AtlasTile& tile = r->world_tiles_cpu[tex_id];
    tile.u0 = (float)ox / fw;
    tile.v0 = (float)oy / fh;
    tile.du = (float)w / fw;
    tile.dv = (float)h / fh;
    const float s = wg1_texture_animation_signed(tex->animation_direction, tex->animation_speed);
    if( s >= 0.0f )
    {
        tile.anim_u = s;
        tile.anim_v = 0.0f;
    }
    else
    {
        tile.anim_u = 0.0f;
        tile.anim_v = -s;
    }
    tile.opaque = tex->opaque ? 1.0f : 0.0f;
    tile._pad = 0.0f;
    wg1_upload_tile_meta_tex(r);

    r->texture_cache.register_texture(tex_id, r->world_atlas_tex);
}

void
wg1_frame_event_batch_model_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->model_cache.begin_batch(bid);
    ctx->renderer->current_model_batch_id = bid;
}

void
wg1_frame_event_model_batched_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    const int mid = cmd->_model_load.model_id;
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( !ctx || !ctx->renderer || !model || mid <= 0 || bid == 0 )
        return;
    if( !wg1_dispatch_model_load(ctx->renderer, mid, model, true, bid) )
        return;
    if( dashmodel__is_ground_va(model) )
        wg1_patch_batched_va_model_verts(ctx, model);
}

void
wg1_frame_event_batch_model_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    Gpu3DCache<GLuint>::BatchEntry* batch = r->model_cache.get_batch(bid);
    if( !batch || !batch->open )
    {
        r->current_model_batch_id = 0;
        return;
    }

    const int nchunks = r->model_cache.get_batch_chunk_count(bid);
    if( nchunks <= 0 )
    {
        r->model_cache.end_batch(bid);
        r->current_model_batch_id = 0;
        return;
    }

    bool any = false;
    for( int c = 0; c < nchunks; ++c )
    {
        const int vb = r->model_cache.get_chunk_pending_bytes(bid, c);
        const int ic = r->model_cache.get_chunk_pending_index_count(bid, c);
        if( vb <= 0 || ic <= 0 )
            continue;
        any = true;
        const void* pv = r->model_cache.get_chunk_pending_verts(bid, c);
        const uint32_t* pi = r->model_cache.get_chunk_pending_indices(bid, c);

        GLuint vbo = 0, ibo = 0;
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        if( !vbo || !ibo )
        {
            if( vbo )
                glDeleteBuffers(1, &vbo);
            if( ibo )
                glDeleteBuffers(1, &ibo);
            continue;
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vb, pv, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            (GLsizeiptr)ic * sizeof(uint32_t),
            pi,
            GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        const int nv = vb / (int)sizeof(WgVertex);
        if( nv > 0 && pv )
        {
            const WgVertex* wv = (const WgVertex*)pv;
            r->geometry_mirror[vbo] = std::vector<WgVertex>(wv, wv + nv);
        }

        r->model_cache.set_chunk_buffers(bid, c, vbo, ibo);
    }
    if( !any )
    {
        r->model_cache.unload_batch(bid);
        r->current_model_batch_id = 0;
        return;
    }
    r->model_cache.end_batch(bid);
    r->current_model_batch_id = 0;
}

void
wg1_frame_event_batch_model_clear(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    auto* batch = r->model_cache.get_batch(bid);
    if( !batch )
        return;
    const int n = r->model_cache.get_batch_chunk_count(bid);
    for( int c = 0; c < n; ++c )
    {
        const Gpu3DCache<GLuint>::BatchChunk* ch = r->model_cache.get_batch_chunk(bid, c);
        if( !ch )
            continue;
        if( ch->vbo )
        {
            r->geometry_mirror.erase(ch->vbo);
            glDeleteBuffers(1, &ch->vbo);
        }
        if( ch->ibo )
            glDeleteBuffers(1, &ch->ibo);
    }
    r->model_cache.unload_batch(bid);
}

void
wg1_frame_event_model_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    if( !model || !ctx )
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
        wg1_dispatch_model_load(ctx->renderer, mid, model, false, 0u);
}

void
wg1_frame_event_model_unload(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
        return;
    wg1_release_model_gpu_buffers(ctx->renderer, mid);
}

void
wg1_frame_event_model_animation_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_animation_load.model_gpu_id;
    const int aid = cmd->_animation_load.anim_id;
    const int fidx = cmd->_animation_load.frame_index;
    if( !cmd->_animation_load.model || mid <= 0 || !ctx || !ctx->renderer )
        return;
    if( !ctx->renderer->model_cache.get_instance(mid, aid, fidx) )
        wg1_build_model_instance(ctx->renderer, cmd->_animation_load.model, mid, aid, fidx);
}

static struct DashVertexArray*
wg1_find_staged_va_for_face_array(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
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
    for( const auto& kv : renderer->va_staging )
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
wg1_frame_event_batch_vertex_array_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t bid = r->current_model_batch_id;
    struct DashVertexArray* va = cmd->_vertex_array_load.array;
    const int va_id = cmd->_vertex_array_load.array_id;
    if( !va || va_id < 0 || bid == 0 )
        return;
    r->va_staging[va_id] = va;
    r->model_cache.register_batch_vertex_array(
        bid, va_id, (uint32_t)va->vertex_count, (int)sizeof(WgVertex));
}

void
wg1_frame_event_batch_face_array_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t bid = r->current_model_batch_id;
    struct DashFaceArray* fa = cmd->_face_array_load.array;
    const int fa_id = cmd->_face_array_load.array_id;
    if( !fa || fa_id < 0 || bid == 0 || fa->count <= 0 )
        return;

    struct DashVertexArray* va = wg1_find_staged_va_for_face_array(r, fa);
    if( !va )
    {
        fprintf(stderr, "[webgl1] BATCH3D_FACE_ARRAY_LOAD: no matching staged vertex array\n");
        return;
    }

    const int fc = fa->count;
    std::vector<int> pnm((size_t)fc, 0);
    std::vector<WgVertex> verts((size_t)fc * 3u);
    std::vector<int> per_face_tex((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = fa->texture_ids ? (int)fa->texture_ids[f] : -1;
        per_face_tex[(size_t)f] = raw;
        WgVertex tri[3];
        if( !wg1_fill_face_corner_vertices_from_fa(va, fa, f, raw, pnm.data(), tri) )
        {
            wg1_vertex_fill_invisible(&tri[0]);
            wg1_vertex_fill_invisible(&tri[1]);
            wg1_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
    }

    r->model_cache.accumulate_batch_face_array(
        bid, fa_id, verts.data(), (int)sizeof(WgVertex), fc * 3, fc, per_face_tex.data());
}

void
wg1_patch_batched_va_model_verts(WebGL1RenderCtx* ctx, struct DashModel* model)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r || !model || !dashmodel__is_ground_va(model) )
        return;
    const uint32_t bid = r->current_model_batch_id;
    if( bid == 0 )
        return;
    struct DashModelVAGround* vg = dashmodel__as_ground_va(model);
    if( !vg->vertex_array || !vg->face_array )
        return;
    const int va_id = vg->vertex_array->scene2_gpu_id;
    const int fa_id = vg->face_array->scene2_gpu_id;
    auto it = r->va_staging.find(va_id);
    if( it == r->va_staging.end() || !it->second )
        return;
    struct DashVertexArray* va = it->second;
    struct DashFaceArray* fa = vg->face_array;
    const uint32_t first = dashmodel_va_first_face_index(model);
    const int fc = dashmodel_face_count(model);
    if( fc <= 0 )
        return;
    const int pnm_val = (int)first;
    Gpu3DCache<GLuint>& cache = r->model_cache;
    if( !cache.patch_face_array_pnm_va_range(fa_id, first, fc, pnm_val) )
        return;
    const int* pnm = cache.face_array_pnm_ptr(fa_id);
    if( !pnm )
        return;
    const int vs = (int)sizeof(WgVertex);
    size_t slice_len = 0;
    uint8_t* dst_base = cache.mutable_batch_face_vert_slice(bid, fa_id, first, fc, vs, &slice_len);
    if( !dst_base || slice_len == 0 )
        return;
    WgVertex tri[3];
    for( int f = (int)first; f < (int)first + fc; ++f )
    {
        int raw = fa->texture_ids ? (int)fa->texture_ids[f] : -1;
        if( !wg1_fill_face_corner_vertices_from_fa(va, fa, f, raw, pnm, tri) )
        {
            wg1_vertex_fill_invisible(&tri[0]);
            wg1_vertex_fill_invisible(&tri[1]);
            wg1_vertex_fill_invisible(&tri[2]);
        }
        uint8_t* dst = dst_base + (size_t)(f - (int)first) * 3u * (size_t)vs;
        memcpy(dst, tri, 3u * (size_t)vs);
    }
}

static void
wg1_ensure_3d_pass(WebGL1RenderCtx* ctx)
{
    if( !ctx || ctx->current_pass == 1 )
        return;
    struct ToriRSRenderCommand stub = { 0 };
    stub.kind = TORIRS_GFX_BEGIN_3D;
    wg1_frame_event_begin_3d(ctx, &stub);
}

void
wg1_frame_event_model_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->bfo3d || !ctx->game || !ctx->renderer )
        return;

    wg1_ensure_3d_pass(ctx);

    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const int mid_draw = cmd->_model_draw.model_id;
    if( mid_draw <= 0 )
        return;

    const uint64_t mk = cmd->_model_draw.model_key;
    int anim_id = 0;
    int frame_index = 0;
    torirs_model_cache_key_decode(mk, &anim_id, &frame_index);

    Gpu3DCache<GLuint>::ModelBufferRange* range =
        ctx->renderer->model_cache.get_instance(mid_draw, anim_id, frame_index);
    if( !range )
    {
        if( !wg1_build_model_instance(ctx->renderer, model, mid_draw, anim_id, frame_index) )
        {
            if( anim_id == 0 && frame_index == 0 )
                return;
            if( !wg1_build_model_instance(ctx->renderer, model, mid_draw, 0, 0) )
                return;
            range = ctx->renderer->model_cache.get_instance(mid_draw, 0, 0);
        }
        else
            range = ctx->renderer->model_cache.get_instance(mid_draw, anim_id, frame_index);
    }
    if( !range )
        return;

    WgDrawBuffersResolved buf = {};
    if( !wg1_resolve_model_draw_buffers(ctx->renderer->model_cache, mid_draw, range, &buf) )
        return;

    struct DashPosition draw_position = cmd->_model_draw.position;

    Gpu3DAngleEncoding angle_enc = Gpu3DAngleEncoding::DashR2pi2048;
    if( Gpu3DCache<GLuint>::ModelEntry* me = ctx->renderer->model_cache.get_model_entry(mid_draw) )
        angle_enc = me->angle_encoding;

    float cos_yaw = 1.0f;
    float sin_yaw = 0.0f;
    wg1_prebake_model_yaw_cos_sin(draw_position.yaw, &cos_yaw, &sin_yaw);

    while( !ctx->bfo3d->append_model(
        ctx->game->sys_dash,
        model,
        &draw_position,
        ctx->game->view_port,
        ctx->game->camera,
        buf.batch_chunk_index,
        (void*)(uintptr_t)buf.vbo,
        buf.vertex_index_base,
        buf.gpu_face_count,
        cos_yaw,
        sin_yaw,
        angle_enc) )
    {
        wg1_flush_3d(ctx, ctx->bfo3d);
    }
}

void
wg1_frame_event_sprite_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    struct DashSprite* sp = cmd->_sprite_load.sprite;
    if( !r || !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
        return;

    const int sp_el = cmd->_sprite_load.element_id;
    const int sp_ai = cmd->_sprite_load.atlas_index;
    {
        auto* existing = r->sprite_cache.get_sprite(sp_el, sp_ai);
        if( existing && existing->atlas_texture && !existing->is_batched )
            glDeleteTextures(1, &existing->atlas_texture);
        r->sprite_cache.unload_sprite(sp_el, sp_ai);
    }

    const uint32_t sbid = r->current_sprite_batch_id;
    if( sbid != 0 && r->sprite_cache.is_batch_open(sbid) )
    {
        r->sprite_cache.add_to_batch(
            sbid, sp_el, sp_ai, (const uint32_t*)sp->pixels_argb, sp->width, sp->height);
        return;
    }

    const int sw = sp->width;
    const int sh = sp->height;
    r->rgba_scratch.resize((size_t)sw * (size_t)sh * 4u);
    torirs_copy_sprite_argb_pixels_to_rgba8(
        (const uint32_t*)sp->pixels_argb, r->rgba_scratch.data(), sw * sh);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        sw,
        sh,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        r->rgba_scratch.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    r->sprite_cache.register_standalone(sp_el, sp_ai, tex);
}

void
wg1_frame_event_sprite_unload(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !r )
        return;
    const int unl_el = cmd->_sprite_load.element_id;
    const int unl_ai = cmd->_sprite_load.atlas_index;
    auto* e = r->sprite_cache.get_sprite(unl_el, unl_ai);
    if( e && e->atlas_texture && !e->is_batched )
        glDeleteTextures(1, &e->atlas_texture);
    r->sprite_cache.unload_sprite(unl_el, unl_ai);
}

void
wg1_frame_event_sprite_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->bsp2d || !ctx->b2d_order )
        return;
    struct DashSprite* sp = cmd->_sprite_draw.sprite;
    if( !sp || sp->width <= 0 || sp->height <= 0 )
        return;

    const int draw_el = cmd->_sprite_draw.element_id;
    const int draw_ai = cmd->_sprite_draw.atlas_index;
    auto* spriteEntry = ctx->renderer->sprite_cache.get_sprite(draw_el, draw_ai);
    if( !spriteEntry || !spriteEntry->atlas_texture )
        return;
    GLuint spriteTex = spriteEntry->atlas_texture;

    const int iw = cmd->_sprite_draw.src_bb_w > 0 ? cmd->_sprite_draw.src_bb_w : sp->width;
    const int ih = cmd->_sprite_draw.src_bb_h > 0 ? cmd->_sprite_draw.src_bb_h : sp->height;
    const int ix = cmd->_sprite_draw.src_bb_x;
    const int iy = cmd->_sprite_draw.src_bb_y;
    if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
        return;
    const float tw = (float)sp->width;
    const float th = (float)sp->height;

    const bool rotated = cmd->_sprite_draw.rotated;
    if( rotated && !ctx->renderer->program_ui_sprite_invrot )
        return;
    if( !rotated && !ctx->renderer->program_ui_sprite )
        return;

    float px[4];
    float py[4];
    float log_u[4];
    float log_v[4];
    SpriteInverseRotParams inv_params{};

    if( rotated )
    {
        const int dw = cmd->_sprite_draw.dst_bb_w;
        const int dh = cmd->_sprite_draw.dst_bb_h;
        if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
            return;
        const int dst_bb_x = cmd->_sprite_draw.dst_bb_x;
        const int dst_bb_y = cmd->_sprite_draw.dst_bb_y;
        px[0] = px[3] = (float)dst_bb_x;
        px[1] = px[2] = (float)(dst_bb_x + dw);
        py[0] = py[1] = (float)dst_bb_y;
        py[2] = py[3] = (float)(dst_bb_y + dh);
        log_u[0] = log_u[3] = (float)dst_bb_x;
        log_u[1] = log_u[2] = (float)(dst_bb_x + dw);
        log_v[0] = log_v[1] = (float)dst_bb_y;
        log_v[2] = log_v[3] = (float)(dst_bb_y + dh);

        if( !torirs_sprite_inverse_rot_params_from_cmd(
                cmd,
                ix,
                iy,
                iw,
                ih,
                (float)sp->width,
                (float)sp->height,
                spriteEntry->u0,
                spriteEntry->v0,
                &inv_params) )
            return;
    }
    else
    {
        const int dst_x = cmd->_sprite_draw.dst_bb_x + sp->crop_x;
        const int dst_y = cmd->_sprite_draw.dst_bb_y + sp->crop_y;
        const float w = (float)iw;
        const float h = (float)ih;
        const float x0 = (float)dst_x;
        const float y0 = (float)dst_y;
        px[0] = px[3] = x0;
        px[1] = px[2] = x0 + w;
        py[0] = py[1] = y0;
        py[2] = py[3] = y0 + h;
    }

    const float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : ctx->renderer->width);
    const float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : ctx->renderer->height);
    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
    torirs_logical_pixel_to_ndc(px[0], py[0], fbw, fbh, &c0x, &c0y);
    torirs_logical_pixel_to_ndc(px[1], py[1], fbw, fbh, &c1x, &c1y);
    torirs_logical_pixel_to_ndc(px[2], py[2], fbw, fbh, &c2x, &c2y);
    torirs_logical_pixel_to_ndc(px[3], py[3], fbw, fbh, &c3x, &c3y);

    float verts[6 * 4];
    if( rotated )
    {
        verts[0] = c0x;
        verts[1] = c0y;
        verts[2] = log_u[0];
        verts[3] = log_v[0];
        verts[4] = c1x;
        verts[5] = c1y;
        verts[6] = log_u[1];
        verts[7] = log_v[1];
        verts[8] = c2x;
        verts[9] = c2y;
        verts[10] = log_u[2];
        verts[11] = log_v[2];
        verts[12] = c0x;
        verts[13] = c0y;
        verts[14] = log_u[0];
        verts[15] = log_v[0];
        verts[16] = c2x;
        verts[17] = c2y;
        verts[18] = log_u[2];
        verts[19] = log_v[2];
        verts[20] = c3x;
        verts[21] = c3y;
        verts[22] = log_u[3];
        verts[23] = log_v[3];
    }
    else
    {
        const float u0 = (float)ix / tw;
        const float v0 = (float)iy / th;
        const float u1 = (float)(ix + iw) / tw;
        const float v1 = (float)(iy + ih) / th;
        verts[0] = c0x;
        verts[1] = c0y;
        verts[2] = u0;
        verts[3] = v0;
        verts[4] = c1x;
        verts[5] = c1y;
        verts[6] = u1;
        verts[7] = v0;
        verts[8] = c2x;
        verts[9] = c2y;
        verts[10] = u1;
        verts[11] = v1;
        verts[12] = c0x;
        verts[13] = c0y;
        verts[14] = u0;
        verts[15] = v0;
        verts[16] = c2x;
        verts[17] = c2y;
        verts[18] = u1;
        verts[19] = v1;
        verts[20] = c3x;
        verts[21] = c3y;
        verts[22] = u0;
        verts[23] = v1;
    }

    SpriteQuadVertex six[6];
    for( int i = 0; i < 6; ++i )
    {
        six[i].cx = verts[i * 4 + 0];
        six[i].cy = verts[i * 4 + 1];
        six[i].u = verts[i * 4 + 2];
        six[i].v = verts[i * 4 + 3];
    }

    if( rotated )
        ctx->bsp2d->enqueue(
            (void*)(uintptr_t)spriteTex,
            six,
            nullptr,
            ctx->b2d_order,
            &ctx->split_sprite_before_next_enqueue,
            &inv_params);
    else
        ctx->bsp2d->enqueue(
            (void*)(uintptr_t)spriteTex,
            six,
            nullptr,
            ctx->b2d_order,
            &ctx->split_sprite_before_next_enqueue);
    ctx->split_font_before_next_set_font = true;
}

static void
wg1_font_load_impl(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    int font_id,
    struct DashPixFont* font)
{
    if( !font || !font->atlas || !renderer->font_program )
        return;
    if( font_id < 0 || font_id >= GpuFontCache<GLuint>::kMaxFonts )
        return;
    if( renderer->font_cache.get_font(font_id) )
        return;

    struct DashFontAtlas* atlas = font->atlas;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        atlas->atlas_width,
        atlas->atlas_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlas->rgba_pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    renderer->font_cache.register_font(font_id, tex);
}

void
wg1_frame_event_font_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    wg1_font_load_impl(ctx->renderer, cmd->_font_load.font_id, cmd->_font_load.font);
}

void
wg1_frame_event_font_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->bft2d || !ctx->b2d_order )
        return;
    struct DashPixFont* f = cmd->_font_draw.font;
    if( !f || !f->atlas || !cmd->_font_draw.text || f->height2d <= 0 )
        return;

    const int font_id = cmd->_font_draw.font_id;
    GpuFontCache<GLuint>::FontEntry* fe = ctx->renderer->font_cache.get_font(font_id);
    if( !fe )
    {
        wg1_font_load_impl(ctx->renderer, font_id, f);
        fe = ctx->renderer->font_cache.get_font(font_id);
        if( !fe )
            return;
    }

    ctx->bft2d->set_font(
        font_id,
        (void*)(uintptr_t)fe->atlas_texture,
        ctx->b2d_order,
        &ctx->split_font_before_next_set_font);

    struct DashFontAtlas* atlas = f->atlas;
    const float inv_aw = 1.0f / (float)atlas->atlas_width;
    const float inv_ah = 1.0f / (float)atlas->atlas_height;

    const uint8_t* text = cmd->_font_draw.text;
    int length = (int)strlen((const char*)text);
    int color_rgb = cmd->_font_draw.color_rgb;
    float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
    float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
    float cb = (float)(color_rgb & 0xFF) / 255.0f;
    float ca = 1.0f;
    int pen_x = cmd->_font_draw.x;
    int base_y = cmd->_font_draw.y;

    for( int i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            int new_color = dashfont_evaluate_color_tag((const char*)&text[i + 1]);
            if( new_color >= 0 )
            {
                color_rgb = new_color;
                cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                cb = (float)(color_rgb & 0xFF) / 255.0f;
            }
            if( i + 6 <= length && text[i + 5] == ' ' )
                i += 5;
            else
                i += 4;
            continue;
        }

        int c = dashfont_charcode_to_glyph(text[i]);
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int gw = atlas->glyph_w[c];
            int gh = atlas->glyph_h[c];
            if( gw > 0 && gh > 0 )
            {
                float x0 = (float)(pen_x + f->char_offset_x[c]);
                float y0 = (float)(base_y + f->char_offset_y[c]);
                float x1 = x0 + (float)gw;
                float y1 = y0 + (float)gh;

                float u0 = (float)atlas->glyph_x[c] * inv_aw;
                float v0 = (float)atlas->glyph_y[c] * inv_ah;
                float u1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                float v1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;

                float vbuf[6 * 8] = {
                    x0, y0, u0, v0, cr, cg, cb, ca, x1, y0, u1, v0, cr, cg, cb, ca,
                    x1, y1, u1, v1, cr, cg, cb, ca, x0, y0, u0, v0, cr, cg, cb, ca,
                    x1, y1, u1, v1, cr, cg, cb, ca, x0, y1, u0, v1, cr, cg, cb, ca,
                };
                ctx->bft2d->append_glyph_quad(vbuf);
            }
        }
        int adv = f->char_advance[c];
        if( adv <= 0 )
            adv = 4;
        pen_x += adv;
    }
    if( ctx->bft2d )
        ctx->bft2d->close_open_segment(ctx->b2d_order);
    ctx->split_sprite_before_next_enqueue = true;
}

void
wg1_frame_event_batch_sprite_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const uint32_t sbid = cmd->_batch.batch_id;
    ctx->renderer->current_sprite_batch_id = sbid;
    ctx->renderer->sprite_cache.begin_batch(sbid, 2048, 2048);
}

void
wg1_frame_event_batch_sprite_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t sbid = cmd->_batch.batch_id;
    auto atlas = r->sprite_cache.finalize_batch(sbid);
    if( atlas.pixels && atlas.w > 0 && atlas.h > 0 )
    {
        r->rgba_scratch.resize((size_t)atlas.w * (size_t)atlas.h * 4u);
        torirs_copy_sprite_argb_pixels_to_rgba8(atlas.pixels, r->rgba_scratch.data(), atlas.w * atlas.h);
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            atlas.w,
            atlas.h,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            r->rgba_scratch.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        r->sprite_cache.set_batch_atlas_handle(sbid, tex);
    }
    r->current_sprite_batch_id = 0;
}

void
wg1_frame_event_batch_font_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const uint32_t fbid = cmd->_batch.batch_id;
    ctx->renderer->current_font_batch_id = fbid;
    ctx->renderer->font_cache.begin_batch(fbid, 1024, 1024);
}

void
wg1_frame_event_batch_font_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !ctx || !r )
        return;
    const uint32_t fbid = cmd->_batch.batch_id;
    auto atlas = r->font_cache.finalize_batch(fbid);
    if( atlas.pixels && atlas.w > 0 && atlas.h > 0 )
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            atlas.w,
            atlas.h,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            atlas.pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        r->font_cache.set_batch_atlas_handle(fbid, tex);
    }
    r->current_font_batch_id = 0;
}

void
wg1_frame_event_clear_rect(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    int rx = cmd->_clear_rect.x;
    int ry = cmd->_clear_rect.y;
    int rw = cmd->_clear_rect.w;
    int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
        return;
    LogicalViewportRect lr = { rx, ry, rw, rh };
    const GLViewportRect glr_lb = wg1_compute_world_viewport_in_letterbox(
        ctx->lb_w,
        ctx->lb_h,
        ctx->win_width,
        ctx->win_height,
        lr);
    const GLViewportRect glr = { ctx->lb_x + glr_lb.x,
                                 ctx->lb_gl_y + glr_lb.y,
                                 glr_lb.width,
                                 glr_lb.height };

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glViewport(0, 0, ctx->renderer->width, ctx->renderer->height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(glr.x, glr.y, glr.width, glr.height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void
wg1_frame_event_begin_3d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    (void)cmd;
    if( !ctx || !ctx->renderer || !ctx->game )
        return;
    wg1_flush_2d(ctx);

    GLViewportRect wvp = wg1_compute_world_viewport_in_letterbox(
        ctx->lb_w,
        ctx->lb_h,
        ctx->win_width,
        ctx->win_height,
        ctx->logical_vp);
    glViewport(ctx->lb_x + wvp.x, ctx->lb_gl_y + wvp.y, wvp.width, wvp.height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(ctx->lb_x + wvp.x, ctx->lb_gl_y + wvp.y, wvp.width, wvp.height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    if( ctx->bfo3d )
        ctx->bfo3d->begin_pass();
    ctx->current_pass = 1;
}

void
wg1_frame_event_end_3d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    (void)cmd;
    if( !ctx || !ctx->bfo3d )
        return;
    wg1_flush_3d(ctx, ctx->bfo3d);
    ctx->bfo3d->begin_pass();
    glViewport(0, 0, ctx->renderer->width, ctx->renderer->height);
    ctx->current_pass = 0;
}

void
wg1_frame_event_begin_2d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    (void)cmd;
    if( !ctx || !ctx->renderer )
        return;
    if( ctx->bfo3d )
        wg1_flush_3d(ctx, ctx->bfo3d);
    if( ctx->bfo3d )
        ctx->bfo3d->begin_pass();
    if( ctx->bsp2d )
        ctx->bsp2d->begin_pass();
    if( ctx->bft2d )
        ctx->bft2d->begin_pass();
    if( ctx->b2d_order )
        ctx->b2d_order->begin_pass();
    ctx->split_sprite_before_next_enqueue = false;
    ctx->split_font_before_next_set_font = false;
    glViewport(ctx->lb_x, ctx->lb_gl_y, ctx->lb_w, ctx->lb_h);
    ctx->current_pass = 2;
}

void
wg1_frame_event_end_2d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    (void)cmd;
    if( !ctx )
        return;
    wg1_flush_2d(ctx);
    ctx->current_pass = 0;
}
