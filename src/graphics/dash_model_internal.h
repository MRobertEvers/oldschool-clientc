#ifndef DASH_MODEL_INTERNAL_H
#define DASH_MODEL_INTERNAL_H

#include "dash.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct DashModelNormals;
struct DashModelBones;

/** Bits 0–1: booleans. Bits 2–4: model struct type (3 bits). Bit 7: valid marker. */
#define DASHMODEL_FLAG_LOADED       0x01u
#define DASHMODEL_FLAG_HAS_TEXTURES 0x02u
#define DASHMODEL_FLAG_VALID        0x80u

#define DASHMODEL_TYPE_SHIFT        2u
#define DASHMODEL_TYPE_MASK         (7u << DASHMODEL_TYPE_SHIFT)

#define DASHMODEL_TYPE_FULL         0u
#define DASHMODEL_TYPE_FAST         1u
#define DASHMODEL_TYPE_VA           2u

struct DashModelFast
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
    struct DashBoundsCylinder* bounds_cylinder;
};

/** Same header as fast; geometry is a weak ref to DashVertexArray. */
struct DashModelVA
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    struct DashVertexArray* vertex_array;
    struct DashBoundsCylinder* bounds_cylinder;
};

struct DashModelFull
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;

    vertexint_t* original_vertices_x;
    vertexint_t* original_vertices_y;
    vertexint_t* original_vertices_z;

    alphaint_t* face_alphas;
    alphaint_t* original_face_alphas;
    int* face_infos;
    int* face_priorities;
    hsl16_t* face_colors;

    int textured_face_count;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    faceint_t* face_texture_coords;

    struct DashModelNormals* normals;
    struct DashModelNormals* merged_normals;
    struct DashModelBones* vertex_bones;
    struct DashModelBones* face_bones;
    struct DashBoundsCylinder* bounds_cylinder;
};

static inline void*
dashmodel__ptr(struct DashModel* m)
{
    return (void*)m;
}

static inline uint8_t
dashmodel__peek_flags(const void* m)
{
    assert(m != NULL);
    return *(const uint8_t*)m;
}

static inline uint8_t
dashmodel__flags(const void* m)
{
    uint8_t f = dashmodel__peek_flags(m);
    assert((f & DASHMODEL_FLAG_VALID) != 0 && "DashModel: invalid flags");
    return f;
}

static inline unsigned
dashmodel__type(const void* m)
{
    return (unsigned)((dashmodel__flags(m) & DASHMODEL_TYPE_MASK) >> DASHMODEL_TYPE_SHIFT);
}

static inline bool
dashmodel__is_full_layout(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_FULL;
}

static inline bool
dashmodel__is_fast(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_FAST;
}

static inline bool
dashmodel__is_va(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_VA;
}

static inline struct DashVertexArray*
dashmodel__va_array_mut(struct DashModel* m)
{
    assert(dashmodel__is_va(m));
    struct DashModelVA* v = (struct DashModelVA*)(void*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline const struct DashVertexArray*
dashmodel__va_array_const(const void* m)
{
    assert(dashmodel__is_va(m));
    const struct DashModelVA* v = (const struct DashModelVA*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline struct DashModelFast*
dashmodel__as_fast(void* m)
{
    assert(dashmodel__is_fast(m));
    return (struct DashModelFast*)m;
}

static inline const struct DashModelFast*
dashmodel__as_fast_const(const void* m)
{
    assert(dashmodel__is_fast(m));
    return (const struct DashModelFast*)m;
}

static inline struct DashModelVA*
dashmodel__as_va(void* m)
{
    assert(dashmodel__is_va(m));
    return (struct DashModelVA*)m;
}

static inline const struct DashModelVA*
dashmodel__as_va_const(const void* m)
{
    assert(dashmodel__is_va(m));
    return (const struct DashModelVA*)m;
}

static inline struct DashModelFull*
dashmodel__as_full(void* m)
{
    assert(dashmodel__is_full_layout(m));
    return (struct DashModelFull*)m;
}

static inline const struct DashModelFull*
dashmodel__as_full_const(const void* m)
{
    assert(dashmodel__is_full_layout(m));
    return (const struct DashModelFull*)m;
}

#endif
