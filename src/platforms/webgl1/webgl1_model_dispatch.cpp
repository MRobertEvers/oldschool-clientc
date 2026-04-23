#include "platforms/webgl1/webgl1_internal.h"

#include <vector>

static void
wg1_mirror_erase(struct Platform2_SDL2_Renderer_WebGL1* r, GLuint vbo)
{
    if( !r || !vbo )
        return;
    r->geometry_mirror.erase(vbo);
}

static void
wg1_mirror_store(struct Platform2_SDL2_Renderer_WebGL1* r, GLuint vbo, std::vector<WgVertex>&& verts)
{
    if( !r || !vbo )
        return;
    r->geometry_mirror[vbo] = std::move(verts);
}

void
wg1_release_model_gpu_buffers(struct Platform2_SDL2_Renderer_WebGL1* r, int mid)
{
    if( !r || mid <= 0 )
        return;
    Gpu3DCache<GLuint>& C = r->model_cache;
    Gpu3DCache<GLuint>::ModelEntry* ent = C.get_model_entry(mid);
    if( !ent )
        return;
    for( Gpu3DCache<GLuint>::AnimEntry& anim : ent->anims )
    {
        for( Gpu3DCache<GLuint>::ModelBufferRange& fr : anim.frames )
        {
            if( fr.loaded && fr.owns_buffer && fr.buffer )
            {
                wg1_mirror_erase(r, fr.buffer);
                glDeleteBuffers(1, &fr.buffer);
                fr.buffer = 0;
            }
            if( fr.loaded && fr.owns_index_buffer && fr.index_buffer )
            {
                glDeleteBuffers(1, &fr.index_buffer);
                fr.index_buffer = 0;
            }
        }
    }
    C.unload_model(mid);
}

bool
wg1_resolve_model_draw_buffers(
    Gpu3DCache<GLuint>& cache,
    int model_gpu_id,
    Gpu3DCache<GLuint>::ModelBufferRange* range,
    WgDrawBuffersResolved* out)
{
    if( !range || !out )
        return false;
    const int stride = (int)sizeof(WgVertex);
    if( stride <= 0 )
        return false;

    if( range->fa_gpu_id >= 0 )
    {
        Gpu3DCache<GLuint>::FaceArrayEntry* fa = cache.get_face_array(range->fa_gpu_id);
        if( !fa )
            return false;
        if( fa->batch_id < 0 )
            return false;
        GLuint vbo = cache.get_batch_vbo_for_chunk((uint32_t)fa->batch_id, fa->batch_chunk_index);
        if( !vbo )
            return false;
        out->vbo = vbo;
        out->batch_chunk_index = fa->batch_chunk_index;
        out->vertex_index_base = (int)(range->vtx_byte_offset / (uint32_t)stride);
        out->gpu_face_count = range->face_count;
        return out->vbo != 0;
    }

    Gpu3DCache<GLuint>::ModelEntry* entry = cache.get_model_entry(model_gpu_id);
    if( entry && entry->is_batched && !range->buffer )
    {
        GLuint vbo = cache.get_batch_vbo_for_model(model_gpu_id);
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
    return out->vbo != 0;
}

bool
wg1_build_model_instance(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    const struct DashModel* model,
    int model_id,
    int anim_id,
    int frame_index)
{
    if( !model || !r || model_id <= 0 )
        return false;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return false;

    const int fc = dashmodel_face_count(model);
    const faceint_t* ftex = dashmodel_face_textures_const(model);
    std::vector<WgVertex> verts((size_t)fc * 3u);
    std::vector<int> per_face_tex((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = ftex ? (int)ftex[f] : -1;
        per_face_tex[(size_t)f] = raw;
        WgVertex tri[3];
        if( !wg1_fill_model_face_vertices_model_local(model, f, raw, tri) )
        {
            wg1_vertex_fill_invisible(&tri[0]);
            wg1_vertex_fill_invisible(&tri[1]);
            wg1_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
    }

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    if( !vbo )
        return false;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)(verts.size() * sizeof(WgVertex)),
        verts.data(),
        GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    std::vector<uint32_t> idx((size_t)fc * 3u);
    for( size_t i = 0; i < idx.size(); ++i )
        idx[i] = (uint32_t)i;
    GLuint ibo = 0;
    glGenBuffers(1, &ibo);
    if( !ibo )
    {
        glDeleteBuffers(1, &vbo);
        return false;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLsizeiptr)(idx.size() * sizeof(uint32_t)),
        idx.data(),
        GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    wg1_mirror_store(r, vbo, std::move(verts));

    r->model_cache.register_instance(
        model_id,
        anim_id,
        frame_index,
        vbo,
        0,
        (uint32_t)(fc * 3),
        fc,
        per_face_tex.data(),
        true,
        ibo,
        true,
        0,
        0);
    return true;
}

static bool
wg1_load_va_model(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int mid,
    struct DashModel* model)
{
    if( !dashmodel__is_ground_va(model) || !r || mid <= 0 )
        return false;
    const struct DashModelVAGround* vg = (const struct DashModelVAGround*)model;
    if( !vg->vertex_array || !vg->face_array )
        return false;
    const int va_id = vg->vertex_array->scene2_gpu_id;
    const int fa_id = vg->face_array->scene2_gpu_id;
    const uint32_t first = (uint32_t)dashmodel_va_first_face_index(model);
    const int fc = dashmodel_face_count(model);
    if( fc <= 0 )
        return false;

    r->model_cache.register_va_model(mid, 0, 0, va_id, fa_id, first, fc, (int)sizeof(WgVertex));
    return true;
}

static bool
wg1_load_owning_model(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int mid,
    struct DashModel* model,
    bool in_batch,
    uint32_t batch_id)
{
    if( !model || !r || mid <= 0 )
        return false;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return false;

    if( !in_batch )
        return wg1_build_model_instance(r, model, mid, 0, 0);

    const int fc = dashmodel_face_count(model);
    const faceint_t* ftex = dashmodel_face_textures_const(model);
    std::vector<WgVertex> verts((size_t)fc * 3u);
    std::vector<int> per_face_tex((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = ftex ? (int)ftex[f] : -1;
        per_face_tex[(size_t)f] = raw;
        WgVertex tri[3];
        if( !wg1_fill_model_face_vertices_model_local(model, f, raw, tri) )
        {
            wg1_vertex_fill_invisible(&tri[0]);
            wg1_vertex_fill_invisible(&tri[1]);
            wg1_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
    }
    r->model_cache.accumulate_batch_model(
        batch_id, mid, verts.data(), (int)sizeof(WgVertex), fc * 3, fc, per_face_tex.data());
    return true;
}

bool
wg1_dispatch_model_load(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int model_id,
    struct DashModel* model,
    bool in_batch,
    uint32_t batch_id)
{
    if( !model || model_id <= 0 || !r )
        return false;

    switch( dashmodel__type(model) )
    {
    case DASHMODEL_TYPE_GROUND_VA:
        if( !in_batch )
            return false;
        return wg1_load_va_model(r, model_id, model);
    case DASHMODEL_TYPE_GROUND:
    case DASHMODEL_TYPE_FULL:
        return wg1_load_owning_model(r, model_id, model, in_batch, batch_id);
    default:
        fprintf(stderr, "[webgl1] MODEL_LOAD: unknown dashmodel type\n");
        return false;
    }
}

int
wg1_count_loaded_model_slots(Gpu3DCache<GLuint>& C)
{
    int total = 0;
    const int cap = C.get_model_capacity();
    for( int m = 0; m < cap; ++m )
    {
        Gpu3DCache<GLuint>::ModelEntry* e = C.get_model_entry(m);
        if( !e )
            continue;
        for( Gpu3DCache<GLuint>::AnimEntry& a : e->anims )
        {
            for( Gpu3DCache<GLuint>::ModelBufferRange& fr : a.frames )
            {
                if( fr.loaded )
                    ++total;
            }
        }
    }
    return total;
}
