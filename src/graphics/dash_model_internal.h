#ifndef DASH_MODEL_INTERNAL_H
#define DASH_MODEL_INTERNAL_H

#include "dash_alpha.h"
#include "dash_faceint.h"
#include "dash_hsl16.h"
#include "dash_vertexint.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct DashModel;
struct DashBoundsCylinder;
struct DashModelNormals;
struct DashModelBones;
struct DashVertexArray;

#define DASHMODEL_FLAG_LOADED       0x01u
#define DASHMODEL_FLAG_HAS_TEXTURES 0x02u
#define DASHMODEL_FLAG_VA           0x20u
#define DASHMODEL_FLAG_FAST         0x40u
#define DASHMODEL_FLAG_VALID        0x80u

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

struct DashModelVA
{
    uint8_t flags;
    int face_count;
    int vertex_offset;
    int vertex_count;
    struct DashVertexArray* vertex_array;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
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

static inline bool
dashmodel__is_va(const void* m)
{
    return (dashmodel__peek_flags(m) & DASHMODEL_FLAG_VA) != 0;
}

static inline bool
dashmodel__is_fast(const void* m)
{
    return (dashmodel__flags(m) & DASHMODEL_FLAG_FAST) != 0;
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

static inline struct DashModelFull*
dashmodel__as_full(void* m)
{
    assert(!dashmodel__is_va(m));
    assert(!dashmodel__is_fast(m));
    return (struct DashModelFull*)m;
}

static inline const struct DashModelFull*
dashmodel__as_full_const(const void* m)
{
    assert(!dashmodel__is_va(m));
    assert(!dashmodel__is_fast(m));
    return (const struct DashModelFull*)m;
}

#endif
