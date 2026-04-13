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

static const faceint_t g_dashmodel_tile_tex_p[1] = { 0 };
static const faceint_t g_dashmodel_tile_tex_m[1] = { 1 };
static const faceint_t g_dashmodel_tile_tex_n[1] = { 2 };

/** VA terrain: all-zero face_texture_coords like legacy full terrain (terrain_decode_tile); read-only in practice. */
static faceint_t g_dashmodel_va_tile_face_texture_coords_zero[4096];

static void
dashmodel__free_tile_arrays(struct DashModelTile* m)
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
    free(m->original_face_alphas);
    free(m->face_alphas);
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
dashmodel__free_flyweight_arrays(struct DashModelFlyWeight* m)
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
    free(m->original_face_alphas);
    free(m->face_alphas);
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
dashmodel__free_lite_arrays(struct DashModelLite* m)
{
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    free(m->face_colors_a);
    free(m->face_colors_b);
    free(m->face_colors_c);
    free(m->face_alphas);
    if( m->merged_normals )
    {
        dashmodel_normals_free(m->merged_normals);
        m->merged_normals = NULL;
    }
}

static void
dashmodel__free_va_tile_shell(struct DashModelVATile* m)
{
    free(m->bounds_cylinder);
}

struct DashModel*
dashmodel_tile_new(void)
{
    struct DashModelTile* m = (struct DashModelTile*)malloc(sizeof(struct DashModelTile));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelTile));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_TILE);
    return (struct DashModel*)m;
}

struct DashFaceArray*
dashfacearray_new(int capacity)
{
    struct DashFaceArray* fa = (struct DashFaceArray*)malloc(sizeof(struct DashFaceArray));
    if( !fa )
        return NULL;
    memset(fa, 0, sizeof(struct DashFaceArray));
    fa->capacity = capacity > 0 ? capacity : 0;
    if( fa->capacity > 0 )
    {
        fa->indices_a = (faceint_t*)malloc((size_t)fa->capacity * sizeof(faceint_t));
        fa->indices_b = (faceint_t*)malloc((size_t)fa->capacity * sizeof(faceint_t));
        fa->indices_c = (faceint_t*)malloc((size_t)fa->capacity * sizeof(faceint_t));
        fa->colors_a = (hsl16_t*)malloc((size_t)fa->capacity * sizeof(hsl16_t));
        fa->colors_b = (hsl16_t*)malloc((size_t)fa->capacity * sizeof(hsl16_t));
        fa->colors_c = (hsl16_t*)malloc((size_t)fa->capacity * sizeof(hsl16_t));
        fa->texture_ids = (faceint_t*)malloc((size_t)fa->capacity * sizeof(faceint_t));
        if( !fa->indices_a || !fa->indices_b || !fa->indices_c || !fa->colors_a ||
            !fa->colors_b || !fa->colors_c || !fa->texture_ids )
        {
            free(fa->indices_a);
            free(fa->indices_b);
            free(fa->indices_c);
            free(fa->colors_a);
            free(fa->colors_b);
            free(fa->colors_c);
            free(fa->texture_ids);
            free(fa);
            return NULL;
        }
    }
    return fa;
}

void
dashfacearray_free(struct DashFaceArray* fa)
{
    if( !fa )
        return;
    free(fa->indices_a);
    free(fa->indices_b);
    free(fa->indices_c);
    free(fa->colors_a);
    free(fa->colors_b);
    free(fa->colors_c);
    free(fa->texture_ids);
    free(fa);
}

void
dashfacearray_clear(struct DashFaceArray* fa)
{
    if( fa )
        fa->count = 0;
}

bool
dashfacearray_reserve(struct DashFaceArray* fa, int need_capacity)
{
    assert(fa && need_capacity >= 0);
    if( need_capacity <= fa->capacity )
        return true;
    faceint_t* na = (faceint_t*)realloc(fa->indices_a, (size_t)need_capacity * sizeof(faceint_t));
    faceint_t* nb = (faceint_t*)realloc(fa->indices_b, (size_t)need_capacity * sizeof(faceint_t));
    faceint_t* nc = (faceint_t*)realloc(fa->indices_c, (size_t)need_capacity * sizeof(faceint_t));
    hsl16_t* ca = (hsl16_t*)realloc(fa->colors_a, (size_t)need_capacity * sizeof(hsl16_t));
    hsl16_t* cb = (hsl16_t*)realloc(fa->colors_b, (size_t)need_capacity * sizeof(hsl16_t));
    hsl16_t* cc = (hsl16_t*)realloc(fa->colors_c, (size_t)need_capacity * sizeof(hsl16_t));
    faceint_t* tx = (faceint_t*)realloc(fa->texture_ids, (size_t)need_capacity * sizeof(faceint_t));
    if( !na || !nb || !nc || !ca || !cb || !cc || !tx )
    {
        free(na);
        free(nb);
        free(nc);
        free(ca);
        free(cb);
        free(cc);
        free(tx);
        return false;
    }
    fa->indices_a = na;
    fa->indices_b = nb;
    fa->indices_c = nc;
    fa->colors_a = ca;
    fa->colors_b = cb;
    fa->colors_c = cc;
    fa->texture_ids = tx;
    fa->capacity = need_capacity;
    return true;
}

void
dashfacearray_shrink_to_fit(struct DashFaceArray* fa)
{
    if( !fa || fa->count >= fa->capacity )
        return;
    int n = fa->count;
    if( n == 0 )
    {
        free(fa->indices_a);
        free(fa->indices_b);
        free(fa->indices_c);
        free(fa->colors_a);
        free(fa->colors_b);
        free(fa->colors_c);
        free(fa->texture_ids);
        fa->indices_a = NULL;
        fa->indices_b = NULL;
        fa->indices_c = NULL;
        fa->colors_a = NULL;
        fa->colors_b = NULL;
        fa->colors_c = NULL;
        fa->texture_ids = NULL;
        fa->capacity = 0;
        return;
    }
    fa->indices_a =
        (faceint_t*)realloc(fa->indices_a, (size_t)n * sizeof(faceint_t));
    fa->indices_b =
        (faceint_t*)realloc(fa->indices_b, (size_t)n * sizeof(faceint_t));
    fa->indices_c =
        (faceint_t*)realloc(fa->indices_c, (size_t)n * sizeof(faceint_t));
    fa->colors_a = (hsl16_t*)realloc(fa->colors_a, (size_t)n * sizeof(hsl16_t));
    fa->colors_b = (hsl16_t*)realloc(fa->colors_b, (size_t)n * sizeof(hsl16_t));
    fa->colors_c = (hsl16_t*)realloc(fa->colors_c, (size_t)n * sizeof(hsl16_t));
    fa->texture_ids =
        (faceint_t*)realloc(fa->texture_ids, (size_t)n * sizeof(faceint_t));
    fa->capacity = n;
}

int
dashfacearray_push(struct DashFaceArray* fa, const struct DashFace* face)
{
    assert(fa && face);
    if( fa->count >= fa->capacity )
    {
        int nc = fa->capacity > 0 ? fa->capacity * 2 : 256;
        if( nc < fa->count + 1 )
            nc = fa->count + 1;
        if( !dashfacearray_reserve(fa, nc) )
            return -1;
    }
    int idx = fa->count;
    fa->indices_a[idx] = face->indices[0];
    fa->indices_b[idx] = face->indices[1];
    fa->indices_c[idx] = face->indices[2];
    fa->colors_a[idx] = face->colors[0];
    fa->colors_b[idx] = face->colors[1];
    fa->colors_c[idx] = face->colors[2];
    fa->texture_ids[idx] = face->texture_id;
    fa->count = idx + 1;
    return idx;
}

struct DashModel*
dashmodel_va_tile_new(struct DashVertexArray* vertex_array)
{
    struct DashModelVATile* m = (struct DashModelVATile*)malloc(sizeof(struct DashModelVATile));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelVATile));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_VA_TILE);
    m->vertex_array = vertex_array;
    if( vertex_array )
        m->vertex_count = vertex_array->vertex_count;
    return (struct DashModel*)m;
}

void
dashmodel_va_tile_set_face_array_ref(
    struct DashModel* m,
    struct DashFaceArray* face_array,
    uint32_t first_face_index,
    int face_count)
{
    assert(m && dashmodel__is_va_tile(m));
    dashmodel__flags(m);
    struct DashModelVATile* v = dashmodel__as_va_tile(m);
    v->face_array = face_array;
    v->first_face_index = first_face_index;
    v->face_count = face_count;
}

const struct DashFaceArray*
dashmodel_va_tile_face_array_const(const struct DashModel* m)
{
    assert(m && dashmodel__is_va_tile(m));
    return dashmodel__as_va_tile_const(m)->face_array;
}

uint32_t
dashmodel_va_tile_first_face_index(const struct DashModel* m)
{
    assert(m && dashmodel__is_va_tile(m));
    return dashmodel__as_va_tile_const(m)->first_face_index;
}

void
dashmodel_va_tile_set_tile_cull_center(
    struct DashModel* m,
    int tile_sw_x,
    int tile_sw_z)
{
    assert(m && dashmodel__is_va_tile(m));
    dashmodel__flags(m);
    struct DashModelVATile* v = dashmodel__as_va_tile(m);
    v->va_tile_cull_center_x = tile_sw_x;
    v->va_tile_cull_center_z = tile_sw_z;
}

void
dashmodel_va_tile_set_bounds_cylinder_from_local(
    struct DashModel* m,
    int num_vertices,
    const vertexint_t* vx,
    const vertexint_t* vy,
    const vertexint_t* vz)
{
    assert(m && dashmodel__is_va_tile(m));
    assert(num_vertices > 0 && vx && vy && vz);
    dashmodel__flags(m);
    struct DashModelVATile* v = dashmodel__as_va_tile(m);
    free(v->bounds_cylinder);
    v->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    dash3d_calculate_bounds_cylinder(
        v->bounds_cylinder,
        num_vertices,
        (vertexint_t*)(void*)vx,
        (vertexint_t*)(void*)vy,
        (vertexint_t*)(void*)vz);
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

struct DashModel*
dashmodel_flyweight_new(void)
{
    struct DashModelFlyWeight* m =
        (struct DashModelFlyWeight*)malloc(sizeof(struct DashModelFlyWeight));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelFlyWeight));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_FLYWEIGHT);
    m->refcount = 1;
    return (struct DashModel*)m;
}

struct DashModel*
dashmodel_lite_new(struct DashModel* flyweight)
{
    assert(flyweight && dashmodel__is_flyweight(flyweight));
    struct DashModelFlyWeight* fw = dashmodel__as_flyweight(flyweight);

    struct DashModelLite* m = (struct DashModelLite*)malloc(sizeof(struct DashModelLite));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashModelLite));
    m->flags = dashmodel__pack_flags_type(DASHMODEL_TYPE_LITE);
    /* Projection / raster use lite flags for HAS_TEXTURES; inherit from flyweight. */
    m->flags = (uint8_t)(m->flags |
                        (dashmodel__flags(flyweight) &
                         (DASHMODEL_FLAG_LOADED | DASHMODEL_FLAG_HAS_TEXTURES)));
    m->flyweight = fw;
    m->vertex_count = fw->vertex_count;
    m->face_count = fw->face_count;
    fw->refcount++;
    return (struct DashModel*)m;
}

void
dashmodel_lite_alloc_vertex_overrides(struct DashModel* dm)
{
    assert(dm && dashmodel__is_lite(dm));
    struct DashModelLite* m = dashmodel__as_lite(dm);
    struct DashModelFlyWeight* fw = m->flyweight;
    assert(fw);
    int vc = m->vertex_count;
    if( vc <= 0 )
        return;
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    m->vertices_x = (vertexint_t*)malloc((size_t)vc * sizeof(vertexint_t));
    m->vertices_y = (vertexint_t*)malloc((size_t)vc * sizeof(vertexint_t));
    m->vertices_z = (vertexint_t*)malloc((size_t)vc * sizeof(vertexint_t));
    if( fw->vertices_x )
        memcpy(m->vertices_x, fw->vertices_x, (size_t)vc * sizeof(vertexint_t));
    if( fw->vertices_y )
        memcpy(m->vertices_y, fw->vertices_y, (size_t)vc * sizeof(vertexint_t));
    if( fw->vertices_z )
        memcpy(m->vertices_z, fw->vertices_z, (size_t)vc * sizeof(vertexint_t));
}

void
dashmodel_lite_prepare_sharelight_instance(struct DashModel* dm)
{
    assert(dm && dashmodel__is_lite(dm));
    struct DashModelLite* m = dashmodel__as_lite(dm);
    int fc = m->face_count;
    if( fc <= 0 || m->sharelight_instance )
        return;
    if( !m->face_colors_a )
    {
        m->face_colors_a = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
        m->face_colors_b = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
        m->face_colors_c = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
    }
    m->sharelight_instance = true;
}

bool
dashmodel_lite_is_sharelight_instance(const struct DashModel* m)
{
    if( !m || dashmodel__type(m) != DASHMODEL_TYPE_LITE )
        return false;
    return dashmodel__as_lite_const(m)->sharelight_instance;
}

void
dashvertexarray_free(struct DashVertexArray* va)
{
    if( !va )
        return;
    free(va->vertices_x);
    free(va->vertices_y);
    free(va->vertices_z);
    free(va);
}

void
dashmodel_free(struct DashModel* model)
{
    if( !model )
        return;
    uint8_t f = dashmodel__peek_flags(model);
    assert((f & DASHMODEL_FLAG_VALID) != 0);
    switch( (unsigned)((f & DASHMODEL_TYPE_MASK) >> DASHMODEL_TYPE_SHIFT) )
    {
    case DASHMODEL_TYPE_TILE:
        dashmodel__free_tile_arrays((struct DashModelTile*)(void*)model);
        free(model);
        return;
    case DASHMODEL_TYPE_VA_TILE:
        dashmodel__free_va_tile_shell((struct DashModelVATile*)(void*)model);
        free(model);
        return;
    case DASHMODEL_TYPE_FULL:
        dashmodel__free_full_arrays((struct DashModelFull*)(void*)model);
        free(model);
        return;
    case DASHMODEL_TYPE_FLYWEIGHT:
    {
        struct DashModelFlyWeight* fw = (struct DashModelFlyWeight*)(void*)model;
        assert(fw->refcount > 0);
        fw->refcount--;
        if( fw->refcount == 0 )
        {
            dashmodel__free_flyweight_arrays(fw);
            free(fw);
        }
        return;
    }
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* lite = (struct DashModelLite*)(void*)model;
        struct DashModelFlyWeight* fw = lite->flyweight;
        dashmodel__free_lite_arrays(lite);
        free(lite);
        if( fw )
        {
            assert(fw->refcount > 0);
            fw->refcount--;
            if( fw->refcount == 0 )
            {
                dashmodel__free_flyweight_arrays(fw);
                free(fw);
            }
        }
        return;
    }
    default:
        assert(0);
        return;
    }
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
    uint8_t type = dashmodel__type(m);
    if( type == DASHMODEL_TYPE_FULL || type == DASHMODEL_TYPE_FLYWEIGHT )
        return true;
    if( type == DASHMODEL_TYPE_LITE )
        return dashmodel__as_lite_const(m)->sharelight_instance;
    return false;
}

void
dashmodel_set_loaded(
    struct DashModel* m,
    bool v)
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
dashmodel_set_has_textures(
    struct DashModel* m,
    bool v)
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
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__as_va_tile_const(m)->vertex_count;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->vertex_count;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->vertex_count;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->vertex_count;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->vertex_count;
    default:
        assert(0);
        return 0;
    }
}

int
dashmodel_face_count(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__as_va_tile_const(m)->face_count;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_count;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_count;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_count;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->face_count;
    default:
        assert(0);
        return 0;
    }
}

vertexint_t*
dashmodel_vertices_x(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_mut(m)->vertices_x;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->vertices_x;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->vertices_x;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->vertices_x;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->vertices_x ? l->vertices_x : l->flyweight->vertices_x;
    }
    default:
        assert(0);
        return NULL;
    }
}

vertexint_t*
dashmodel_vertices_y(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_mut(m)->vertices_y;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->vertices_y;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->vertices_y;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->vertices_y;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->vertices_y ? l->vertices_y : l->flyweight->vertices_y;
    }
    default:
        assert(0);
        return NULL;
    }
}

vertexint_t*
dashmodel_vertices_z(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_mut(m)->vertices_z;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->vertices_z;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->vertices_z;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->vertices_z;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->vertices_z ? l->vertices_z : l->flyweight->vertices_z;
    }
    default:
        assert(0);
        return NULL;
    }
}

const vertexint_t*
dashmodel_vertices_x_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_const(m)->vertices_x;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->vertices_x;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->vertices_x;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->vertices_x;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->vertices_x ? l->vertices_x : l->flyweight->vertices_x;
    }
    default:
        assert(0);
        return NULL;
    }
}

const vertexint_t*
dashmodel_vertices_y_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_const(m)->vertices_y;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->vertices_y;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->vertices_y;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->vertices_y;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->vertices_y ? l->vertices_y : l->flyweight->vertices_y;
    }
    default:
        assert(0);
        return NULL;
    }
}

const vertexint_t*
dashmodel_vertices_z_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__va_tile_array_const(m)->vertices_z;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->vertices_z;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->vertices_z;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->vertices_z;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->vertices_z ? l->vertices_z : l->flyweight->vertices_z;
    }
    default:
        assert(0);
        return NULL;
    }
}

hsl16_t*
dashmodel_face_colors_a(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_a + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_colors_a;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_colors_a;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_colors_a;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->face_colors_a ? l->face_colors_a : l->flyweight->face_colors_a;
    }
    default:
        assert(0);
        return NULL;
    }
}

hsl16_t*
dashmodel_face_colors_b(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_b + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_colors_b;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_colors_b;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_colors_b;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->face_colors_b ? l->face_colors_b : l->flyweight->face_colors_b;
    }
    default:
        assert(0);
        return NULL;
    }
}

hsl16_t*
dashmodel_face_colors_c(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_c + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_colors_c;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_colors_c;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_colors_c;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->face_colors_c ? l->face_colors_c : l->flyweight->face_colors_c;
    }
    default:
        assert(0);
        return NULL;
    }
}

const hsl16_t*
dashmodel_face_colors_a_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_a + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_colors_a;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_colors_a;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_colors_a;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->face_colors_a ? l->face_colors_a : l->flyweight->face_colors_a;
    }
    default:
        assert(0);
        return NULL;
    }
}

const hsl16_t*
dashmodel_face_colors_b_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_b + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_colors_b;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_colors_b;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_colors_b;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->face_colors_b ? l->face_colors_b : l->flyweight->face_colors_b;
    }
    default:
        assert(0);
        return NULL;
    }
}

const hsl16_t*
dashmodel_face_colors_c_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->colors_c + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_colors_c;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_colors_c;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_colors_c;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->face_colors_c ? l->face_colors_c : l->flyweight->face_colors_c;
    }
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_face_indices_a(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_a + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_indices_a;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_indices_a;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_indices_a;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_indices_a;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_face_indices_b(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_b + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_indices_b;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_indices_b;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_indices_b;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_indices_b;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_face_indices_c(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_c + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_indices_c;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_indices_c;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_indices_c;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_indices_c;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_face_indices_a_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_a + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_indices_a;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_indices_a;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_indices_a;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_indices_a;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_face_indices_b_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_b + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_indices_b;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_indices_b;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_indices_b;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_indices_b;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_face_indices_c_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->indices_c + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_indices_c;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_indices_c;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_indices_c;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_indices_c;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_face_textures(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->texture_ids + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->face_textures;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_textures;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_textures;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_textures;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_face_textures_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(m);
        return v->face_array ? v->face_array->texture_ids + v->first_face_index : NULL;
    }
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->face_textures;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_textures;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_textures;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_textures;
    default:
        assert(0);
        return NULL;
    }
}

struct DashBoundsCylinder*
dashmodel_bounds_cylinder(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__as_va_tile(m)->bounds_cylinder;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile(m)->bounds_cylinder;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->bounds_cylinder;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->bounds_cylinder;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->bounds_cylinder;
    default:
        assert(0);
        return NULL;
    }
}

const struct DashBoundsCylinder*
dashmodel_bounds_cylinder_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__as_va_tile_const(m)->bounds_cylinder;
    case DASHMODEL_TYPE_TILE:
        return dashmodel__as_tile_const(m)->bounds_cylinder;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->bounds_cylinder;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->bounds_cylinder;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->bounds_cylinder;
    default:
        assert(0);
        return NULL;
    }
}

alphaint_t*
dashmodel_face_alphas(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_alphas;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_alphas;
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        return l->face_alphas ? l->face_alphas : l->flyweight->face_alphas;
    }
    default:
        return NULL;
    }
}

const alphaint_t*
dashmodel_face_alphas_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_alphas;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_alphas;
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = dashmodel__as_lite_const(m);
        return l->face_alphas ? l->face_alphas : l->flyweight->face_alphas;
    }
    default:
        return NULL;
    }
}

alphaint_t*
dashmodel_original_face_alphas(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->original_face_alphas;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->original_face_alphas;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->original_face_alphas;
    default:
        return NULL;
    }
}

int*
dashmodel_face_infos(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_infos;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_infos;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_infos;
    default:
        return NULL;
    }
}

const int*
dashmodel_face_infos_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_infos;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_infos;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_infos;
    default:
        return NULL;
    }
}

int*
dashmodel_face_infos_ensure_zero(struct DashModel* m)
{
    assert(m);
    dashmodel__flags(m);
    int fc = dashmodel_face_count(m);
    if( fc <= 0 )
        return dashmodel_face_infos(m);

    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
    {
        struct DashModelFull* u = dashmodel__as_full(m);
        if( !u->face_infos )
            u->face_infos = (int*)calloc((size_t)fc, sizeof(int));
        return u->face_infos;
    }
    case DASHMODEL_TYPE_FLYWEIGHT:
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(m);
        if( !fw->face_infos )
            fw->face_infos = (int*)calloc((size_t)fc, sizeof(int));
        return fw->face_infos;
    }
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelFlyWeight* fw = dashmodel__as_lite(m)->flyweight;
        assert(fw);
        if( !fw->face_infos )
            fw->face_infos = (int*)calloc((size_t)fc, sizeof(int));
        return fw->face_infos;
    }
    default:
        assert(0 && "dashmodel_face_infos_ensure_zero: unsupported model type");
        return NULL;
    }
}

const uint8_t*
dashmodel_face_priorities(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_priorities;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_priorities;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_priorities;
    default:
        return NULL;
    }
}

hsl16_t*
dashmodel_face_colors_flat(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_colors;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_colors;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_colors;
    default:
        return NULL;
    }
}

vertexint_t*
dashmodel_original_vertices_x(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->original_vertices_x;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->original_vertices_x;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->original_vertices_x;
    default:
        return NULL;
    }
}

vertexint_t*
dashmodel_original_vertices_y(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->original_vertices_y;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->original_vertices_y;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->original_vertices_y;
    default:
        return NULL;
    }
}

vertexint_t*
dashmodel_original_vertices_z(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->original_vertices_z;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->original_vertices_z;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->original_vertices_z;
    default:
        return NULL;
    }
}

int
dashmodel_textured_face_count(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return 0;
        return 1;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->textured_face_count;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->textured_face_count;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->textured_face_count;
    default:
        assert(0);
        return 0;
    }
}

faceint_t*
dashmodel_textured_p_coordinate(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_tile_tex_p[0];
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->textured_p_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->textured_p_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->textured_p_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_textured_m_coordinate(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_tile_tex_m[0];
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->textured_m_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->textured_m_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->textured_m_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_textured_n_coordinate(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return (faceint_t*)(void*)&g_dashmodel_tile_tex_n[0];
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->textured_n_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->textured_n_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->textured_n_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_textured_p_coordinate_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_tile_tex_p;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->textured_p_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->textured_p_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->textured_p_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_textured_m_coordinate_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_tile_tex_m;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->textured_m_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->textured_m_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->textured_m_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

const faceint_t*
dashmodel_textured_n_coordinate_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_VA_TILE:
        if( !dashmodel_has_textures(m) )
            return NULL;
        return g_dashmodel_tile_tex_n;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->textured_n_coordinate;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->textured_n_coordinate;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->textured_n_coordinate;
    default:
        assert(0);
        return NULL;
    }
}

faceint_t*
dashmodel_face_texture_coords(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel_face_count(m) > 0 ? g_dashmodel_va_tile_face_texture_coords_zero : NULL;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_texture_coords;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_texture_coords;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_texture_coords;
    default:
        return NULL;
    }
}

const faceint_t*
dashmodel_face_texture_coords_const(const struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel_face_count(m) > 0 ? g_dashmodel_va_tile_face_texture_coords_zero : NULL;
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full_const(m)->face_texture_coords;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight_const(m)->face_texture_coords;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite_const(m)->flyweight->face_texture_coords;
    default:
        return NULL;
    }
}

struct DashModelNormals*
dashmodel_normals(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->normals;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->normals;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->normals;
    default:
        return NULL;
    }
}

struct DashModelNormals*
dashmodel_merged_normals(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->merged_normals;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->merged_normals;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->merged_normals;
    default:
        return NULL;
    }
}

struct DashModelBones*
dashmodel_vertex_bones(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->vertex_bones;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->vertex_bones;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->vertex_bones;
    default:
        return NULL;
    }
}

struct DashModelBones*
dashmodel_face_bones(struct DashModel* m)
{
    assert(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_FULL:
        return dashmodel__as_full(m)->face_bones;
    case DASHMODEL_TYPE_FLYWEIGHT:
        return dashmodel__as_flyweight(m)->face_bones;
    case DASHMODEL_TYPE_LITE:
        return dashmodel__as_lite(m)->flyweight->face_bones;
    default:
        return NULL;
    }
}

static struct DashModelTile*
dashmodel__writable_tile(struct DashModel* m)
{
    assert(dashmodel__is_tile(m));
    return dashmodel__as_tile(m);
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
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
    assert(!dashmodel__is_va_tile(m));
    int fc = dashmodel_face_count(m);
    assert(fc > 0);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
    assert(!dashmodel__is_va_tile(m));
    int fc = dashmodel_face_count(m);
    assert(fc > 0);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
dashmodel_set_face_textures_i16(
    struct DashModel* m,
    const int16_t* src_textures,
    int count)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
dashmodel_set_face_textures_i32(
    struct DashModel* m,
    const int32_t* src_textures,
    int count)
{
    assert(m && count >= 0);
    dashmodel__flags(m);
    assert(!dashmodel__is_va_tile(m));
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
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
    case DASHMODEL_TYPE_FULL:
        break;
    default:
        assert(0);
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
dashmodel_set_face_alphas(
    struct DashModel* m,
    const alphaint_t* src,
    int count)
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
dashmodel_set_face_infos(
    struct DashModel* m,
    const int* infos,
    int count)
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
dashmodel_set_face_priorities(
    struct DashModel* m,
    const int* priorities,
    int count)
{
    assert(m && priorities && count > 0);
    assert(dashmodel__is_full_layout(m));
    dashmodel__flags(m);
    struct DashModelFull* u = dashmodel__writable_full(m);
    assert(count == u->face_count);
    free(u->face_priorities);
    size_t nbytes = dashmodel__face_priorities_byte_count(count);
    u->face_priorities = (uint8_t*)calloc(nbytes, 1);
    assert(u->face_priorities != NULL);
    for( int i = 0; i < count; i++ )
    {
        assert(priorities[i] >= 0 && priorities[i] <= 15);
        dashmodel__set_face_priority(u->face_priorities, i, priorities[i]);
    }
}

void
dashmodel_set_face_colors_flat(
    struct DashModel* m,
    const hsl16_t* src,
    int count)
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
        memcpy(
            u->face_texture_coords, face_texture_coords, (size_t)face_count_in * sizeof(faceint_t));
    }
}

void
dashmodel_set_bounds_cylinder(struct DashModel* m)
{
    assert(m);
    dashmodel__flags(m);
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        struct DashModelVATile* v = dashmodel__as_va_tile(m);
        struct DashVertexArray* va = v->vertex_array;
        assert(va != NULL);
        free(v->bounds_cylinder);
        v->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            v->bounds_cylinder, va->vertex_count, va->vertices_x, va->vertices_y, va->vertices_z);
        return;
    }
    case DASHMODEL_TYPE_TILE:
    {
        struct DashModelTile* f = dashmodel__writable_tile(m);
        free(f->bounds_cylinder);
        f->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            f->bounds_cylinder, f->vertex_count, f->vertices_x, f->vertices_y, f->vertices_z);
        return;
    }
    case DASHMODEL_TYPE_FULL:
    {
        struct DashModelFull* u = dashmodel__writable_full(m);
        free(u->bounds_cylinder);
        u->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            u->bounds_cylinder, u->vertex_count, u->vertices_x, u->vertices_y, u->vertices_z);
        return;
    }
    case DASHMODEL_TYPE_FLYWEIGHT:
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(m);
        free(fw->bounds_cylinder);
        fw->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        dash3d_calculate_bounds_cylinder(
            fw->bounds_cylinder, fw->vertex_count, fw->vertices_x, fw->vertices_y, fw->vertices_z);
        return;
    }
    case DASHMODEL_TYPE_LITE:
    {
        struct DashModelLite* l = dashmodel__as_lite(m);
        struct DashModelFlyWeight* fw = l->flyweight;
        assert(fw);
        free(fw->bounds_cylinder);
        fw->bounds_cylinder = (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
        vertexint_t* vx = dashmodel_vertices_x(m);
        vertexint_t* vy = dashmodel_vertices_y(m);
        vertexint_t* vz = dashmodel_vertices_z(m);
        dash3d_calculate_bounds_cylinder(
            fw->bounds_cylinder, fw->vertex_count, vx, vy, vz);
        return;
    }
    default:
        assert(0);
        return;
    }
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
    switch( (unsigned)((f & DASHMODEL_TYPE_MASK) >> DASHMODEL_TYPE_SHIFT) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* m = (const struct DashModelVATile*)(const void*)model;
        size_t total = sizeof(struct DashModelVATile);
        if( m->bounds_cylinder )
            total += sizeof(struct DashBoundsCylinder);
        return total;
    }
    case DASHMODEL_TYPE_TILE:
    {
        const struct DashModelTile* m = (const struct DashModelTile*)(const void*)model;
        size_t total = sizeof(struct DashModelTile);
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
    case DASHMODEL_TYPE_FULL:
        break;
    case DASHMODEL_TYPE_FLYWEIGHT:
    {
        const struct DashModelFlyWeight* fw =
            (const struct DashModelFlyWeight*)(const void*)model;
        size_t total = sizeof(struct DashModelFlyWeight);
        int vc = fw->vertex_count;
        int fc = fw->face_count;
        int tfc = fw->textured_face_count;
        if( vc > 0 )
        {
            if( fw->vertices_x ) total += (size_t)vc * sizeof(vertexint_t);
            if( fw->vertices_y ) total += (size_t)vc * sizeof(vertexint_t);
            if( fw->vertices_z ) total += (size_t)vc * sizeof(vertexint_t);
            if( fw->original_vertices_x ) total += (size_t)vc * sizeof(vertexint_t);
            if( fw->original_vertices_y ) total += (size_t)vc * sizeof(vertexint_t);
            if( fw->original_vertices_z ) total += (size_t)vc * sizeof(vertexint_t);
        }
        if( fc > 0 )
        {
            if( fw->face_indices_a ) total += (size_t)fc * sizeof(faceint_t);
            if( fw->face_indices_b ) total += (size_t)fc * sizeof(faceint_t);
            if( fw->face_indices_c ) total += (size_t)fc * sizeof(faceint_t);
            if( fw->face_alphas ) total += (size_t)fc * sizeof(alphaint_t);
            if( fw->original_face_alphas ) total += (size_t)fc * sizeof(alphaint_t);
            if( fw->face_infos ) total += (size_t)fc * sizeof(int);
            if( fw->face_priorities ) total += dashmodel__face_priorities_byte_count(fc);
            if( fw->face_colors ) total += (size_t)fc * sizeof(hsl16_t);
            if( fw->face_colors_a ) total += (size_t)fc * sizeof(hsl16_t);
            if( fw->face_colors_b ) total += (size_t)fc * sizeof(hsl16_t);
            if( fw->face_colors_c ) total += (size_t)fc * sizeof(hsl16_t);
            if( fw->face_textures ) total += (size_t)fc * sizeof(faceint_t);
            if( fw->face_texture_coords ) total += (size_t)fc * sizeof(faceint_t);
        }
        if( tfc > 0 )
        {
            if( fw->textured_p_coordinate ) total += (size_t)tfc * sizeof(faceint_t);
            if( fw->textured_m_coordinate ) total += (size_t)tfc * sizeof(faceint_t);
            if( fw->textured_n_coordinate ) total += (size_t)tfc * sizeof(faceint_t);
        }
        total += dashmodel_normals_heap_bytes(fw->normals);
        total += dashmodel_normals_heap_bytes(fw->merged_normals);
        total += dashmodel_bones_heap_bytes(fw->vertex_bones);
        total += dashmodel_bones_heap_bytes(fw->face_bones);
        if( fw->bounds_cylinder ) total += sizeof(struct DashBoundsCylinder);
        return total;
    }
    case DASHMODEL_TYPE_LITE:
    {
        const struct DashModelLite* l = (const struct DashModelLite*)(const void*)model;
        size_t total = sizeof(struct DashModelLite);
        int vc = l->flyweight->vertex_count;
        int fc = l->flyweight->face_count;
        if( vc > 0 )
        {
            if( l->vertices_x ) total += (size_t)vc * sizeof(vertexint_t);
            if( l->vertices_y ) total += (size_t)vc * sizeof(vertexint_t);
            if( l->vertices_z ) total += (size_t)vc * sizeof(vertexint_t);
        }
        if( fc > 0 )
        {
            if( l->face_colors_a ) total += (size_t)fc * sizeof(hsl16_t);
            if( l->face_colors_b ) total += (size_t)fc * sizeof(hsl16_t);
            if( l->face_colors_c ) total += (size_t)fc * sizeof(hsl16_t);
            if( l->face_alphas ) total += (size_t)fc * sizeof(alphaint_t);
        }
        total += dashmodel_normals_heap_bytes(l->merged_normals);
        return total;
    }
    default:
        assert(0);
        return 0;
    }
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
            total += dashmodel__face_priorities_byte_count(fc);
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
    switch( dashmodel__type(model) )
    {
    case DASHMODEL_TYPE_VA_TILE:
    {
        const struct DashModelVATile* v = dashmodel__as_va_tile_const(model);
        if( v->face_count > 0 )
        {
            if( !v->face_array || !v->face_array->indices_a )
                return false;
            if( (uint64_t)v->first_face_index + (uint64_t)v->face_count >
                (uint64_t)v->face_array->count )
                return false;
        }
        break;
    }
    case DASHMODEL_TYPE_TILE:
    case DASHMODEL_TYPE_FULL:
        if( dashmodel_face_colors_a(model) == NULL )
            return false;
        break;
    case DASHMODEL_TYPE_FLYWEIGHT:
        if( dashmodel_face_colors_a(model) == NULL )
            return false;
        break;
    case DASHMODEL_TYPE_LITE:
        if( dashmodel_face_colors_a(model) == NULL )
            return false;
        break;
    default:
        return false;
    }
    if( dashmodel_bounds_cylinder(model) == NULL )
        return false;
    return true;
}

void
dashmodel_alloc_lit_face_colors_zero(
    struct DashModel* m,
    int face_count)
{
    assert(m && face_count > 0);
    dashmodel__flags(m);
    if( dashmodel__is_full_layout(m) )
    {
        struct DashModelFull* u = dashmodel__as_full(m);
        assert(u->face_count == face_count);
        free(u->face_colors_a);
        free(u->face_colors_b);
        free(u->face_colors_c);
        u->face_colors_a = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
        u->face_colors_b = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
        u->face_colors_c = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
    }
    else if( dashmodel__is_flyweight(m) )
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(m);
        assert(fw->face_count == face_count);
        free(fw->face_colors_a);
        free(fw->face_colors_b);
        free(fw->face_colors_c);
        fw->face_colors_a = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
        fw->face_colors_b = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
        fw->face_colors_c = (hsl16_t*)calloc((size_t)face_count, sizeof(hsl16_t));
    }
    else
    {
        assert(0 && "dashmodel_alloc_lit_face_colors_zero: unsupported model type");
    }
}

static int
dashmodel__needs_face_normals(struct DashModel* model)
{
    const int* fi = dashmodel_face_infos_const(model);
    if( !fi )
        return 0;
    int fc = dashmodel_face_count(model);
    for( int i = 0; i < fc; i++ )
    {
        if( (fi[i] & 0x3) == 1 )
            return 1;
    }
    return 0;
}

void
dashmodel_alloc_normals(struct DashModel* model)
{
    assert(model);
    dashmodel__flags(model);
    uint8_t type = dashmodel__type(model);
    if( type == DASHMODEL_TYPE_FULL )
    {
        struct DashModelFull* m = dashmodel__writable_full(model);
        if( m->normals )
            return;
        int face_n = dashmodel__needs_face_normals(model) ? m->face_count : 0;
        m->normals = dashmodel_normals_new(m->vertex_count, face_n);
    }
    else if( type == DASHMODEL_TYPE_FLYWEIGHT )
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(model);
        if( fw->normals )
            return;
        int face_n = dashmodel__needs_face_normals(model) ? fw->face_count : 0;
        fw->normals = dashmodel_normals_new(fw->vertex_count, face_n);
    }
    else if( type == DASHMODEL_TYPE_LITE )
    {
        struct DashModelLite* lite = dashmodel__as_lite(model);
        struct DashModelFlyWeight* fw = lite->flyweight;
        assert(fw);
        if( fw->normals )
            return;
        int face_n = dashmodel__needs_face_normals(model) ? lite->face_count : 0;
        fw->normals = dashmodel_normals_new(fw->vertex_count, face_n);
    }
}

void
dashmodel_alloc_merged_normals(struct DashModel* model)
{
    assert(model);
    dashmodel__flags(model);
    uint8_t type = dashmodel__type(model);
    if( type == DASHMODEL_TYPE_FULL )
    {
        struct DashModelFull* m = dashmodel__writable_full(model);
        if( m->merged_normals )
            return;
        m->merged_normals = dashmodel_normals_new(m->vertex_count, 0);
    }
    else if( type == DASHMODEL_TYPE_FLYWEIGHT )
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(model);
        if( fw->merged_normals )
            return;
        fw->merged_normals = dashmodel_normals_new(fw->vertex_count, 0);
    }
    else if( type == DASHMODEL_TYPE_LITE )
    {
        struct DashModelLite* lite = dashmodel__as_lite(model);
        if( lite->merged_normals )
            return;
        lite->merged_normals = dashmodel_normals_new(lite->vertex_count, 0);
    }
}

void
dashmodel_free_normals(struct DashModel* model)
{
    assert(model);
    uint8_t type = dashmodel__type(model);
    if( type == DASHMODEL_TYPE_FULL )
    {
        struct DashModelFull* m = dashmodel__writable_full(model);
        if( !m->normals )
            return;
        dashmodel_normals_free(m->normals);
        dashmodel_normals_free(m->merged_normals);
        m->normals = NULL;
        m->merged_normals = NULL;
    }
    else if( type == DASHMODEL_TYPE_FLYWEIGHT )
    {
        struct DashModelFlyWeight* fw = dashmodel__as_flyweight(model);
        if( !fw->normals )
            return;
        dashmodel_normals_free(fw->normals);
        dashmodel_normals_free(fw->merged_normals);
        fw->normals = NULL;
        fw->merged_normals = NULL;
    }
    else if( type == DASHMODEL_TYPE_LITE )
    {
        struct DashModelLite* lite = dashmodel__as_lite(model);
        if( lite->merged_normals )
        {
            dashmodel_normals_free(lite->merged_normals);
            lite->merged_normals = NULL;
        }
    }
}

const struct DashVertexArray*
dashmodel_vertex_array_const(const struct DashModel* m)
{
    if( !m )
        return NULL;
    switch( dashmodel__type(m) )
    {
    case DASHMODEL_TYPE_VA_TILE:
        return dashmodel__as_va_tile_const(m)->vertex_array;
    default:
        return NULL;
    }
}
