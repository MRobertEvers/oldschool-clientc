#include "dash.h"
#include "dash_model_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint8_t
dashmodel__pack_flags_type(unsigned type_bits)
{
    return (uint8_t)(DASHMODEL_FLAG_VALID | ((type_bits & 7u) << DASHMODEL_TYPE_SHIFT));
}

static const faceint_t g_dashmodel_fast_tex_p[1] = { 0 };
static const faceint_t g_dashmodel_fast_tex_m[1] = { 1 };
static const faceint_t g_dashmodel_fast_tex_n[1] = { 3 };

static void
dashmodel__free_fast_arrays(struct DashModelFast* m)
{
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    free(m->face_colors_a);
    free(m->face_colors_b);
    free(m->face_colors_c);
    free(m->face_indices_a);
    free(m->face_indices_b);
    free(m->face_indices_c);
    free(m->face_textures);
    free(m->bounds_cylinder);
}

static void
dashmodel__free_full_arrays(struct DashModelFull* m)
{
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    free(m->face_colors_a);
    free(m->face_colors_b);
    free(m->face_colors_c);
    free(m->face_indices_a);
    free(m->face_indices_b);
    free(m->face_indices_c);
    free(m->face_textures);
    free(m->original_vertices_x);
    free(m->original_vertices_y);
    free(m->original_vertices_z);
    free(m->face_alphas);
    free(m->original_face_alphas);
    free(m->face_infos);
    free(m->face_priorities);
    free(m->face_colors);
    free(m->textured_p_coordinate);
    free(m->textured_m_coordinate);
    free(m->textured_n_coordinate);
    free(m->face_texture_coords);
    if( m->normals )
    {
        free(m->normals->lighting_vertex_normals);
        free(m->normals->lighting_face_normals);
        free(m->normals);
    }
    if( m->merged_normals )
    {
        free(m->merged_normals->lighting_vertex_normals);
        free(m->merged_normals->lighting_face_normals);
        free(m->merged_normals);
    }
    if( m->vertex_bones )
    {
        for( int i = 0; i < m->vertex_bones->bones_count; i++ )
            free(m->vertex_bones->bones[i]);
        free(m->vertex_bones->bones);
        free(m->vertex_bones->bones_sizes);
        free(m->vertex_bones);
    }
    if( m->face_bones )
    {
        for( int i = 0; i < m->face_bones->bones_count; i++ )
            free(m->face_bones->bones[i]);
        free(m->face_bones->bones);
        free(m->face_bones->bones_sizes);
        free(m->face_bones);
    }
    free(m->bounds_cylinder);
}

static void
dashmodel__free_va_shell(struct DashModelVA* m)
{
    free(m->bounds_cylinder);
}

struct DashModel*
dashmodel_fast_new(void)
{
    struct DashModelFast* m = (struct DashModelFast*)malloc(sizeof(struct DashModelFast));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelFast));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_FAST);
    return (struct DashModel*)m;
}

struct DashModel*
dashmodel_va_new(struct DashVertexArray* vertex_array)
{
    struct DashModelVA* m = (struct DashModelVA*)malloc(sizeof(struct DashModelVA));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelVA));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_VA);
    m->vertex_array = vertex_array;
    if( vertex_array )
    {
        m->vertex_count = vertex_array->vertex_count;
        m->face_count = vertex_array->face_count;
    }
    return (struct DashModel*)m;
}

struct DashModel*
dashmodelfull_new(void)
{
    struct DashModelFull* m = (struct DashModelFull*)malloc(sizeof(struct DashModelFull));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelFull));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_FULL);
    return (struct DashModel*)m;
}

struct DashModel*
dashmodel_new(void)
{
    return dashmodelfull_new();
}

void
dashvertexarray_free(struct DashVertexArray* va)
{
    if( !va )
        return;
    free(va->vertices_x);
    free(va->vertices_y);
    free(va->vertices_z);
    free(va->face_colors_a);
    free(va->face_colors_b);
    free(va->face_colors_c);
    free(va->face_indices_a);
    free(va->face_indices_b);
    free(va->face_indices_c);
    free(va->face_textures);
    free(va);
}

void
dashmodel_free(struct DashModel* model)
{
    if( !model )
        return;
    uint8_t f = dashmodel__peek_flags(model);
    assert((f & DASHMODEL_FLAG_VALID) != 0);
    unsigned t = (unsigned)((f & DASHMODEL_TYPE_MASK) >> DASHMODEL_TYPE_SHIFT);
    if( t == DASHMODEL_TYPE_FAST )
    {
        dashmodel__free_fast_arrays((struct DashModelFast*)(void*)model);
        free(model);
        return;
    }
    if( t == DASHMODEL_TYPE_VA )
    {
        dashmodel__free_va_shell((struct DashModelVA*)(void*)model);
        free(model);
        return;
    }
    dashmodel__free_full_arrays((struct DashModelFull*)(void*)model);
    free(model);
}

bool
dashmodel_is_loaded(const struct DashModel* m)
{
    if( !m )
        return false;
    return (dashmodel__flags(m) & DASHMODEL_FLAG_LOADED) != 0;
}

bool
dashmodel_is_lightable(const struct DashModel* m)
{
    if( !m )
        return false;
    return dashmodel__is_full_layout(m);
}

void
dashmodel_set_loaded(struct DashModel* m, bool v)
{
    assert(m);
    uint8_t f = dashmodel__flags(m);
    if( v )
        *(uint8_t*)(void*)m = (uint8_t)(f | DASHMODEL_FLAG_LOADED);
    else
        *(uint8_t*)(void*)m = (uint8_t)(f & ~DASHMODEL_FLAG_LOADED);
}

bool
dashmodel_has_textures(const struct DashModel* m)
{
    if( !m )
        return false;
    return (dashmodel__flags(m) & DASHMODEL_FLAG_HAS_TEXTURES) != 0;
}

void
dashmodel_set_has_textures(struct DashModel* m, bool v)
{
    assert(m);
    uint8_t f = dashmodel__flags(m);
    if( v )
        *(uint8_t*)(void*)m = (uint8_t)(f | DASHMODEL_FLAG_HAS_TEXTURES);
    else
        *(uint8_t*)(void*)m = (uint8_t)(f & ~DASHMODEL_FLAG_HAS_TEXTURES);
}

int
dashmodel_vertex_count(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__as_va_const(m)->vertex_count;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->vertex_count;
    return dashmodel__as_full_const(m)->vertex_count;
}

int
dashmodel_face_count(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__as_va_const(m)->face_count;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_count;
    return dashmodel__as_full_const(m)->face_count;
}

vertexint_t*
dashmodel_vertices_x(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->vertices_x;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->vertices_x;
    return dashmodel__as_full(m)->vertices_x;
}

vertexint_t*
dashmodel_vertices_y(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->vertices_y;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->vertices_y;
    return dashmodel__as_full(m)->vertices_y;
}

vertexint_t*
dashmodel_vertices_z(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->vertices_z;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->vertices_z;
    return dashmodel__as_full(m)->vertices_z;
}

const vertexint_t*
dashmodel_vertices_x_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->vertices_x;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->vertices_x;
    return dashmodel__as_full_const(m)->vertices_x;
}

const vertexint_t*
dashmodel_vertices_y_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->vertices_y;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->vertices_y;
    return dashmodel__as_full_const(m)->vertices_y;
}

const vertexint_t*
dashmodel_vertices_z_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->vertices_z;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->vertices_z;
    return dashmodel__as_full_const(m)->vertices_z;
}

hsl16_t*
dashmodel_face_colors_a(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_colors_a;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_colors_a;
    return dashmodel__as_full(m)->face_colors_a;
}

hsl16_t*
dashmodel_face_colors_b(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_colors_b;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_colors_b;
    return dashmodel__as_full(m)->face_colors_b;
}

hsl16_t*
dashmodel_face_colors_c(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_colors_c;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_colors_c;
    return dashmodel__as_full(m)->face_colors_c;
}

const hsl16_t*
dashmodel_face_colors_a_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_colors_a;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_colors_a;
    return dashmodel__as_full_const(m)->face_colors_a;
}

const hsl16_t*
dashmodel_face_colors_b_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_colors_b;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_colors_b;
    return dashmodel__as_full_const(m)->face_colors_b;
}

const hsl16_t*
dashmodel_face_colors_c_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_colors_c;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_colors_c;
    return dashmodel__as_full_const(m)->face_colors_c;
}

faceint_t*
dashmodel_face_indices_a(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_indices_a;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_indices_a;
    return dashmodel__as_full(m)->face_indices_a;
}

faceint_t*
dashmodel_face_indices_b(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_indices_b;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_indices_b;
    return dashmodel__as_full(m)->face_indices_b;
}

faceint_t*
dashmodel_face_indices_c(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_indices_c;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_indices_c;
    return dashmodel__as_full(m)->face_indices_c;
}

const faceint_t*
dashmodel_face_indices_a_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_indices_a;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_indices_a;
    return dashmodel__as_full_const(m)->face_indices_a;
}

const faceint_t*
dashmodel_face_indices_b_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_indices_b;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_indices_b;
    return dashmodel__as_full_const(m)->face_indices_b;
}

const faceint_t*
dashmodel_face_indices_c_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_indices_c;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_indices_c;
    return dashmodel__as_full_const(m)->face_indices_c;
}

faceint_t*
dashmodel_face_textures(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_mut(m)->face_textures;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->face_textures;
    return dashmodel__as_full(m)->face_textures;
}

const faceint_t*
dashmodel_face_textures_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__va_array_const(m)->face_textures;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->face_textures;
    return dashmodel__as_full_const(m)->face_textures;
}

struct DashBoundsCylinder*
dashmodel_bounds_cylinder(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__as_va(m)->bounds_cylinder;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast(m)->bounds_cylinder;
    return dashmodel__as_full(m)->bounds_cylinder;
}

const struct DashBoundsCylinder*
dashmodel_bounds_cylinder_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_va(m) )
        return dashmodel__as_va_const(m)->bounds_cylinder;
    if( dashmodel__is_fast(m) )
        return dashmodel__as_fast_const(m)->bounds_cylinder;
    return dashmodel__as_full_const(m)->bounds_cylinder;
}

alphaint_t*
dashmodel_face_alphas(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_alphas;
}

const alphaint_t*
dashmodel_face_alphas_const(const struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full_const(m)->face_alphas;
}

alphaint_t*
dashmodel_original_face_alphas(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->original_face_alphas;
}

int*
dashmodel_face_infos(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_infos;
}

const int*
dashmodel_face_infos_const(const struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full_const(m)->face_infos;
}

int*
dashmodel_face_infos_ensure_zero(struct DashModel* m)
{
    assert(m);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    int fc = dashmodel_face_count(m);
    if( fc <= 0 )
        return dashmodel_face_infos(m);
    struct DashModelFull* u = dashmodel__as_full(m);
    if( !u->face_infos )
        u->face_infos = (int*)calloc((size_t)fc, sizeof(int));
    return u->face_infos;
}

int*
dashmodel_face_priorities(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_priorities;
}

hsl16_t*
dashmodel_face_colors_flat(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_colors;
}

vertexint_t*
dashmodel_original_vertices_x(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->original_vertices_x;
}

vertexint_t*
dashmodel_original_vertices_y(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->original_vertices_y;
}

vertexint_t*
dashmodel_original_vertices_z(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->original_vertices_z;
}

int
dashmodel_textured_face_count(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return 0;
        return 1;
    }
    return dashmodel__as_full_const(m)->textured_face_count;
}

faceint_t*
dashmodel_textured_p_coordinate(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_fast_tex_p[0];
    }
    return dashmodel__as_full(m)->textured_p_coordinate;
}

faceint_t*
dashmodel_textured_m_coordinate(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_fast_tex_m[0];
    }
    return dashmodel__as_full(m)->textured_m_coordinate;
}

faceint_t*
dashmodel_textured_n_coordinate(struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_fast_tex_n[0];
    }
    return dashmodel__as_full(m)->textured_n_coordinate;
}

const faceint_t*
dashmodel_textured_p_coordinate_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_fast_tex_p;
    }
    return dashmodel__as_full_const(m)->textured_p_coordinate;
}

const faceint_t*
dashmodel_textured_m_coordinate_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_fast_tex_m;
    }
    return dashmodel__as_full_const(m)->textured_m_coordinate;
}

const faceint_t*
dashmodel_textured_n_coordinate_const(const struct DashModel* m)
{
    assert(m);
    if( dashmodel__is_fast(m) || dashmodel__is_va(m) )
    {
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_fast_tex_n;
    }
    return dashmodel__as_full_const(m)->textured_n_coordinate;
}

faceint_t*
dashmodel_face_texture_coords(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_texture_coords;
}

const faceint_t*
dashmodel_face_texture_coords_const(const struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full_const(m)->face_texture_coords;
}

struct DashModelNormals*
dashmodel_normals(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->normals;
}

struct DashModelNormals*
dashmodel_merged_normals(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->merged_normals;
}

struct DashModelBones*
dashmodel_vertex_bones(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->vertex_bones;
}

struct DashModelBones*
dashmodel_face_bones(struct DashModel* m)
{
    assert(m);
    if( !dashmodel__is_full_layout(m) )
        return NULL;
    return dashmodel__as_full(m)->face_bones;
}

static struct DashModelFast*
dashmodel__writable_fast(struct DashModel* m)
{
    assert(dashmodel__is_fast(m));
    return dashmodel__as_fast(m);
}

static struct DashModelFull*
dashmodel__writable_full(struct DashModel* m)
{
    assert(dashmodel__is_full_layout(m));
    return dashmodel__as_full(m);
}

void
dashmodel_set_vertices_i16(
    struct DashModel* m,
    int count,
    const int16_t* src_x,
    const int16_t* src_y,
    const int16_t* src_z)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->vertices_x);
        free(f->vertices_y);
        free(f->vertices_z);
        f->vertex_count = count;
        if( count <= 0 )
        {
            f->vertices_x = f->vertices_y = f->vertices_z = NULL;
            return;
        }
        f->vertices_x = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        f->vertices_y = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        f->vertices_z = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        memcpy(f->vertices_x, src_x, (size_t)count * sizeof(vertexint_t));
        memcpy(f->vertices_y, src_y, (size_t)count * sizeof(vertexint_t));
        memcpy(f->vertices_z, src_z, (size_t)count * sizeof(vertexint_t));
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->vertices_x);
    free(u->vertices_y);
    free(u->vertices_z);
    u->vertex_count = count;
    if( count <= 0 )
    {
        u->vertices_x = u->vertices_y = u->vertices_z = NULL;
        return;
    }
    u->vertices_x = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    u->vertices_y = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    u->vertices_z = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    memcpy(u->vertices_x, src_x, (size_t)count * sizeof(vertexint_t));
    memcpy(u->vertices_y, src_y, (size_t)count * sizeof(vertexint_t));
    memcpy(u->vertices_z, src_z, (size_t)count * sizeof(vertexint_t));
}

void
dashmodel_set_vertices_i32(
    struct DashModel* m,
    int count,
    const int32_t* src_x,
    const int32_t* src_y,
    const int32_t* src_z)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->vertices_x);
        free(f->vertices_y);
        free(f->vertices_z);
        f->vertex_count = count;
        if( count <= 0 )
        {
            f->vertices_x = f->vertices_y = f->vertices_z = NULL;
            return;
        }
        f->vertices_x = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        f->vertices_y = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        f->vertices_z = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        for( int i = 0; i < count; i++ )
        {
            f->vertices_x[i] = (vertexint_t)src_x[i];
            f->vertices_y[i] = (vertexint_t)src_y[i];
            f->vertices_z[i] = (vertexint_t)src_z[i];
        }
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->vertices_x);
    free(u->vertices_y);
    free(u->vertices_z);
    u->vertex_count = count;
    if( count <= 0 )
    {
        u->vertices_x = u->vertices_y = u->vertices_z = NULL;
        return;
    }
    u->vertices_x = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    u->vertices_y = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    u->vertices_z = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
    for( int i = 0; i < count; i++ )
    {
        u->vertices_x[i] = (vertexint_t)src_x[i];
        u->vertices_y[i] = (vertexint_t)src_y[i];
        u->vertices_z[i] = (vertexint_t)src_z[i];
    }
}

void
dashmodel_set_face_indices_i16(
    struct DashModel* m,
    int count,
    const int16_t* src_a,
    const int16_t* src_b,
    const int16_t* src_c)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_indices_a);
        free(f->face_indices_b);
        free(f->face_indices_c);
        f->face_count = count;
        if( count <= 0 )
        {
            f->face_indices_a = f->face_indices_b = f->face_indices_c = NULL;
            return;
        }
        f->face_indices_a = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        f->face_indices_b = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        f->face_indices_c = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        memcpy(f->face_indices_a, src_a, (size_t)count * sizeof(faceint_t));
        memcpy(f->face_indices_b, src_b, (size_t)count * sizeof(faceint_t));
        memcpy(f->face_indices_c, src_c, (size_t)count * sizeof(faceint_t));
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_indices_a);
    free(u->face_indices_b);
    free(u->face_indices_c);
    u->face_count = count;
    if( count <= 0 )
    {
        u->face_indices_a = u->face_indices_b = u->face_indices_c = NULL;
        return;
    }
    u->face_indices_a = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    u->face_indices_b = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    u->face_indices_c = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    memcpy(u->face_indices_a, src_a, (size_t)count * sizeof(faceint_t));
    memcpy(u->face_indices_b, src_b, (size_t)count * sizeof(faceint_t));
    memcpy(u->face_indices_c, src_c, (size_t)count * sizeof(faceint_t));
}

void
dashmodel_set_face_indices_i32(
    struct DashModel* m,
    int count,
    const int32_t* src_a,
    const int32_t* src_b,
    const int32_t* src_c)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_indices_a);
        free(f->face_indices_b);
        free(f->face_indices_c);
        f->face_count = count;
        if( count <= 0 )
        {
            f->face_indices_a = f->face_indices_b = f->face_indices_c = NULL;
            return;
        }
        f->face_indices_a = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        f->face_indices_b = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        f->face_indices_c = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        for( int i = 0; i < count; i++ )
        {
            f->face_indices_a[i] = (faceint_t)src_a[i];
            f->face_indices_b[i] = (faceint_t)src_b[i];
            f->face_indices_c[i] = (faceint_t)src_c[i];
        }
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_indices_a);
    free(u->face_indices_b);
    free(u->face_indices_c);
    u->face_count = count;
    if( count <= 0 )
    {
        u->face_indices_a = u->face_indices_b = u->face_indices_c = NULL;
        return;
    }
    u->face_indices_a = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    u->face_indices_b = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    u->face_indices_c = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    for( int i = 0; i < count; i++ )
    {
        u->face_indices_a[i] = (faceint_t)src_a[i];
        u->face_indices_b[i] = (faceint_t)src_b[i];
        u->face_indices_c[i] = (faceint_t)src_c[i];
    }
}

void
dashmodel_set_face_colors_i16(
    struct DashModel* m,
    const uint16_t* src_a,
    const uint16_t* src_b,
    const uint16_t* src_c)
{
    assert(m && src_a && src_b && src_c);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    int fc = dashmodel_face_count(m);
    assert(fc > 0);
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_colors_a);
        free(f->face_colors_b);
        free(f->face_colors_c);
        f->face_colors_a = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        f->face_colors_b = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        f->face_colors_c = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        memcpy(f->face_colors_a, src_a, (size_t)fc * sizeof(hsl16_t));
        memcpy(f->face_colors_b, src_b, (size_t)fc * sizeof(hsl16_t));
        memcpy(f->face_colors_c, src_c, (size_t)fc * sizeof(hsl16_t));
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_colors_a);
    free(u->face_colors_b);
    free(u->face_colors_c);
    u->face_colors_a = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    u->face_colors_b = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    u->face_colors_c = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    memcpy(u->face_colors_a, src_a, (size_t)fc * sizeof(hsl16_t));
    memcpy(u->face_colors_b, src_b, (size_t)fc * sizeof(hsl16_t));
    memcpy(u->face_colors_c, src_c, (size_t)fc * sizeof(hsl16_t));
}

void
dashmodel_set_face_colors_i32(
    struct DashModel* m,
    const int32_t* src_a,
    const int32_t* src_b,
    const int32_t* src_c)
{
    assert(m && src_a && src_b && src_c);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    int fc = dashmodel_face_count(m);
    assert(fc > 0);
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_colors_a);
        free(f->face_colors_b);
        free(f->face_colors_c);
        f->face_colors_a = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        f->face_colors_b = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        f->face_colors_c = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        for( int i = 0; i < fc; i++ )
        {
            f->face_colors_a[i] = (hsl16_t)(unsigned)(src_a[i] & 0xffff);
            f->face_colors_b[i] = (hsl16_t)(unsigned)(src_b[i] & 0xffff);
            f->face_colors_c[i] = (hsl16_t)(unsigned)(src_c[i] & 0xffff);
        }
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_colors_a);
    free(u->face_colors_b);
    free(u->face_colors_c);
    u->face_colors_a = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    u->face_colors_b = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    u->face_colors_c = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
    for( int i = 0; i < fc; i++ )
    {
        u->face_colors_a[i] = (hsl16_t)(unsigned)(src_a[i] & 0xffff);
        u->face_colors_b[i] = (hsl16_t)(unsigned)(src_b[i] & 0xffff);
        u->face_colors_c[i] = (hsl16_t)(unsigned)(src_c[i] & 0xffff);
    }
}

void
dashmodel_set_face_textures_i16(struct DashModel* m, const int16_t* src_textures, int count)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_textures);
        if( count <= 0 || !src_textures )
        {
            f->face_textures = NULL;
            return;
        }
        f->face_textures = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        memcpy(f->face_textures, src_textures, (size_t)count * sizeof(faceint_t));
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_textures);
    if( count <= 0 || !src_textures )
    {
        u->face_textures = NULL;
        return;
    }
    u->face_textures = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    memcpy(u->face_textures, src_textures, (size_t)count * sizeof(faceint_t));
}

void
dashmodel_set_face_textures_i32(struct DashModel* m, const int32_t* src_textures, int count)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va(m));
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->face_textures);
        if( count <= 0 || !src_textures )
        {
            f->face_textures = NULL;
            return;
        }
        f->face_textures = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        for( int i = 0; i < count; i++ )
            f->face_textures[i] = (faceint_t)src_textures[i];
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->face_textures);
    if( count <= 0 || !src_textures )
    {
        u->face_textures = NULL;
        return;
    }
    u->face_textures = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
    for( int i = 0; i < count; i++ )
        u->face_textures[i] = (faceint_t)src_textures[i];
}

void
dashmodel_set_face_alphas(struct DashModel* m, const alphaint_t* src, int count)
{
    assert(m && src && count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(count == u->face_count);
    free(u->face_alphas);
    u->face_alphas = (alphaint_t*)malloc((size_t)count * sizeof(alphaint_t));
    memcpy(u->face_alphas, src, (size_t)count * sizeof(alphaint_t));
}

void
dashmodel_set_face_infos(struct DashModel* m, const int* infos, int count)
{
    assert(m && infos && count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(count == u->face_count);
    free(u->face_infos);
    u->face_infos = (int*)malloc((size_t)count * sizeof(int));
    memcpy(u->face_infos, infos, (size_t)count * sizeof(int));
}

void
dashmodel_set_face_priorities(struct DashModel* m, const int* priorities, int count)
{
    assert(m && priorities && count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(count == u->face_count);
    free(u->face_priorities);
    u->face_priorities = (int*)malloc((size_t)count * sizeof(int));
    memcpy(u->face_priorities, priorities, (size_t)count * sizeof(int));
}

void
dashmodel_set_face_colors_flat(struct DashModel* m, const hsl16_t* src, int count)
{
    assert(m && src && count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(count == u->face_count);
    free(u->face_colors);
    u->face_colors = (hsl16_t*)malloc((size_t)count * sizeof(hsl16_t));
    memcpy(u->face_colors, src, (size_t)count * sizeof(hsl16_t));
}

void
dashmodel_set_texture_coords(
    struct DashModel* m,
    int textured_face_count_in,
    const faceint_t* p,
    const faceint_t* m_coord,
    const faceint_t* n,
    const faceint_t* face_texture_coords,
    int face_count_in)
{
    assert(m);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->textured_p_coordinate);
    free(u->textured_m_coordinate);
    free(u->textured_n_coordinate);
    free(u->face_texture_coords);
    u->textured_p_coordinate = u->textured_m_coordinate = u->textured_n_coordinate = NULL;
    u->face_texture_coords = NULL;
    u->textured_face_count = textured_face_count_in;
    if( textured_face_count_in > 0 && p && m_coord && n )
    {
        int tfc = textured_face_count_in;
        u->textured_p_coordinate = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        u->textured_m_coordinate = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        u->textured_n_coordinate = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        memcpy(u->textured_p_coordinate, p, (size_t)tfc * sizeof(faceint_t));
        memcpy(u->textured_m_coordinate, m_coord, (size_t)tfc * sizeof(faceint_t));
        memcpy(u->textured_n_coordinate, n, (size_t)tfc * sizeof(faceint_t));
    }
    if( face_count_in > 0 && face_texture_coords )
    {
        u->face_texture_coords = (faceint_t*)malloc((size_t)face_count_in * sizeof(faceint_t));
        memcpy(u->face_texture_coords, face_texture_coords, (size_t)face_count_in * sizeof(faceint_t));
    }
}

void
dashmodel_set_bounds_cylinder(struct DashModel* m)
{
    assert(m);
    dashmodel__flags(m);
    if( dashmodel__is_va(m) )
    {
        struct DashModelVA* v = dashmodel__as_va(m);
        struct DashVertexArray* va = v->vertex_array;
        assert(va != NULL);
        free(v->bounds_cylinder);
        v->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            v->bounds_cylinder,
            va->vertex_count,
            va->vertices_x,
            va->vertices_y,
            va->vertices_z);
        return;
    }
    if( dashmodel__is_fast(m) )
    {
        struct DashModelFast* f = dashmodel__writable_fast(m);
        free(f->bounds_cylinder);
        f->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            f->bounds_cylinder,
            f->vertex_count,
            f->vertices_x,
            f->vertices_y,
            f->vertices_z);
        return;
    }
    struct DashModelFull* u = dashmodel__writable_full(m);
    free(u->bounds_cylinder);
    u->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    dash3d_calculate_bounds_cylinder(
        u->bounds_cylinder,
        u->vertex_count,
        u->vertices_x,
        u->vertices_y,
        u->vertices_z);
}

static size_t
dashmodel_normals_heap_bytes(const struct DashModelNormals* nm)
{
    if( !nm )
        return 0;
    size_t n = sizeof(struct DashModelNormals);
    if( nm->lighting_vertex_normals && nm->lighting_vertex_normals_count > 0 )
        n += (size_t)nm->lighting_vertex_normals_count * sizeof(struct LightingNormal);
    if( nm->lighting_face_normals && nm->lighting_face_normals_count > 0 )
        n += (size_t)nm->lighting_face_normals_count * sizeof(struct LightingNormal);
    return n;
}

static size_t
dashmodel_bones_heap_bytes(const struct DashModelBones* bones)
{
    if( !bones )
        return 0;
    size_t n = sizeof(struct DashModelBones);
    if( bones->bones_count > 0 && bones->bones )
        n += (size_t)bones->bones_count * sizeof(boneint_t*);
    if( bones->bones_count > 0 && bones->bones_sizes )
        n += (size_t)bones->bones_count * sizeof(boneint_t);
    if( bones->bones && bones->bones_sizes )
    {
        for( int i = 0; i < bones->bones_count; i++ )
        {
            if( bones->bones[i] && bones->bones_sizes[i] > 0 )
                n += (size_t)bones->bones_sizes[i] * sizeof(boneint_t);
        }
    }
    return n;
}

size_t
dashmodel_heap_bytes(const struct DashModel* model)
{
    if( !model )
        return 0;
    uint8_t f = dashmodel__peek_flags(model);
    assert((f & DASHMODEL_FLAG_VALID) != 0);
    unsigned t = (unsigned)((f & DASHMODEL_TYPE_MASK) >> DASHMODEL_TYPE_SHIFT);
    if( t == DASHMODEL_TYPE_VA )
    {
        const struct DashModelVA* m = (const struct DashModelVA*)(const void*)model;
        size_t total = sizeof(struct DashModelVA);
        if( m->bounds_cylinder )
            total += sizeof(struct DashBoundsCylinder);
        return total;
    }
    if( t == DASHMODEL_TYPE_FAST )
    {
        const struct DashModelFast* m = (const struct DashModelFast*)(const void*)model;
        size_t total = sizeof(struct DashModelFast);
        int vc = m->vertex_count;
        int fc = m->face_count;
        if( vc > 0 )
        {
            if( m->vertices_x )
                total += (size_t)vc * sizeof(vertexint_t);
            if( m->vertices_y )
                total += (size_t)vc * sizeof(vertexint_t);
            if( m->vertices_z )
                total += (size_t)vc * sizeof(vertexint_t);
        }
        if( fc > 0 )
        {
            if( m->face_indices_a )
                total += (size_t)fc * sizeof(faceint_t);
            if( m->face_indices_b )
                total += (size_t)fc * sizeof(faceint_t);
            if( m->face_indices_c )
                total += (size_t)fc * sizeof(faceint_t);
            if( m->face_colors_a )
                total += (size_t)fc * sizeof(hsl16_t);
            if( m->face_colors_b )
                total += (size_t)fc * sizeof(hsl16_t);
            if( m->face_colors_c )
                total += (size_t)fc * sizeof(hsl16_t);
            if( m->face_textures )
                total += (size_t)fc * sizeof(faceint_t);
        }
        if( m->bounds_cylinder )
            total += sizeof(struct DashBoundsCylinder);
        return total;
    }
    assert(t == DASHMODEL_TYPE_FULL);
    const struct DashModelFull* m = (const struct DashModelFull*)(const void*)model;
    size_t total = sizeof(struct DashModelFull);
    int vc = m->vertex_count;
    int fc = m->face_count;
    int tfc = m->textured_face_count;
    if( vc > 0 )
    {
        if( m->vertices_x )
            total += (size_t)vc * sizeof(vertexint_t);
        if( m->vertices_y )
            total += (size_t)vc * sizeof(vertexint_t);
        if( m->vertices_z )
            total += (size_t)vc * sizeof(vertexint_t);
        if( m->original_vertices_x )
            total += (size_t)vc * sizeof(vertexint_t);
        if( m->original_vertices_y )
            total += (size_t)vc * sizeof(vertexint_t);
        if( m->original_vertices_z )
            total += (size_t)vc * sizeof(vertexint_t);
    }
    if( fc > 0 )
    {
        if( m->face_indices_a )
            total += (size_t)fc * sizeof(faceint_t);
        if( m->face_indices_b )
            total += (size_t)fc * sizeof(faceint_t);
        if( m->face_indices_c )
            total += (size_t)fc * sizeof(faceint_t);
        if( m->face_alphas )
            total += (size_t)fc * sizeof(alphaint_t);
        if( m->original_face_alphas )
            total += (size_t)fc * sizeof(alphaint_t);
        if( m->face_infos )
            total += (size_t)fc * sizeof(int);
        if( m->face_priorities )
            total += (size_t)fc * sizeof(int);
        if( m->face_colors )
            total += (size_t)fc * sizeof(hsl16_t);
        if( m->face_colors_a )
            total += (size_t)fc * sizeof(hsl16_t);
        if( m->face_colors_b )
            total += (size_t)fc * sizeof(hsl16_t);
        if( m->face_colors_c )
            total += (size_t)fc * sizeof(hsl16_t);
        if( m->face_textures )
            total += (size_t)fc * sizeof(faceint_t);
        if( m->face_texture_coords )
            total += (size_t)fc * sizeof(faceint_t);
    }
    if( tfc > 0 )
    {
        if( m->textured_p_coordinate )
            total += (size_t)tfc * sizeof(faceint_t);
        if( m->textured_m_coordinate )
            total += (size_t)tfc * sizeof(faceint_t);
        if( m->textured_n_coordinate )
            total += (size_t)tfc * sizeof(faceint_t);
    }
    total += dashmodel_normals_heap_bytes(m->normals);
    total += dashmodel_normals_heap_bytes(m->merged_normals);
    total += dashmodel_bones_heap_bytes(m->vertex_bones);
    total += dashmodel_bones_heap_bytes(m->face_bones);
    if( m->bounds_cylinder )
        total += sizeof(struct DashBoundsCylinder);
    return total;
}

bool
dashmodel_valid(struct DashModel* model)
{
    if( !model )
        return false;
    uint8_t f = dashmodel__peek_flags(model);
    if( (f & DASHMODEL_FLAG_VALID) == 0 )
        return false;
    if( dashmodel_face_colors_a(model) == NULL )
        return false;
    if( dashmodel_bounds_cylinder(model) == NULL )
        return false;
    return true;
}

void
dashmodel_alloc_lit_face_colors_zero(struct DashModel* m, int face_count)
{
    assert(m && face_count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(u->face_count == face_count);
    free(u->face_colors_a);
    free(u->face_colors_b);
    free(u->face_colors_c);
    u->face_colors_a = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
    u->face_colors_b = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
    u->face_colors_c = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
}

void
dashmodel_alloc_normals(struct DashModel* model)
{
    assert(model);
    dashmodel__flags(model);
    if( !dashmodel__is_full_layout(model) )
        return;
    struct DashModelFull* m = dashmodel__writable_full(model);
    if( m->normals )
        return;
    m->normals = dashmodel_normals_new(m->vertex_count, m->face_count);
    m->merged_normals = dashmodel_normals_new(m->vertex_count, m->face_count);
}

void
dashmodel_free_normals(struct DashModel* model)
{
    assert(model);
    if( !dashmodel__is_full_layout(model) )
        return;
    struct DashModelFull* m = dashmodel__writable_full(model);
    if( !m->normals )
        return;
    dashmodel_normals_free(m->normals);
    dashmodel_normals_free(m->merged_normals);
    m->normals = NULL;
    m->merged_normals = NULL;
}

const struct DashVertexArray*
dashmodel_vertex_array_const(const struct DashModel* m)
{
    if( !m )
        return NULL;
    if( !dashmodel__is_va(m) )
        return NULL;
    return dashmodel__as_va_const(m)->vertex_array;
}

