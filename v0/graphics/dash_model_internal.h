#ifndef DASH_MODEL_INTERNAL_H
#define DASH_MODEL_INTERNAL_H

#include "dash.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct DashModelNormals;
struct DashModelBones;

/** Bits 0–1: booleans. Bits 2–4: model struct type (3 bits). Bit 7: valid marker. */
#define DASHMODEL_FLAG_LOADED 0x01u
#define DASHMODEL_FLAG_HAS_TEXTURES 0x02u
#define DASHMODEL_FLAG_VALID 0x80u

#define DASHMODEL_TYPE_SHIFT 2u
#define DASHMODEL_TYPE_MASK (7u << DASHMODEL_TYPE_SHIFT)

#define DASHMODEL_TYPE_FULL 0u
#define DASHMODEL_TYPE_GROUND 1u
#define DASHMODEL_TYPE_GROUND_VA 2u

struct DashModelGround
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

/** Weak ref to DashVertexArray (vertices only); face data owned by this model shell.
 *  When va_has_tile_cull_center: culling uses va_tile_cull_center_x/z (tile SW in world)
 *  as the model-space origin for the bounds cylinder; vertices stay absolute world. */
struct DashModelVAGround
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    struct DashVertexArray* vertex_array;
    struct DashFaceArray* face_array;
    uint32_t first_face_index;
    uint16_t va_tile_cull_center_x;
    uint16_t va_tile_cull_center_z;
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
    /** Two 4-bit priorities per byte (low nibble = even face index). Values 0–12. */
    uint8_t* face_priorities;
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

static inline size_t
dashmodel__face_priorities_byte_count(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static inline int
dashmodel__get_face_priority(
    const uint8_t* packed,
    int index)
{
    uint8_t byte = packed[index >> 1];
    return (index & 1) ? (int)(byte >> 4) : (int)(byte & 0x0Fu);
}

static inline void
dashmodel__set_face_priority(
    uint8_t* packed,
    int index,
    int value)
{
    assert(value >= 0 && value <= 15);
    int byte_idx = index >> 1;
    if( index & 1 )
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0x0Fu) | (uint8_t)(value << 4));
    else
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0xF0u) | (uint8_t)(value & 0x0F));
}

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
dashmodel__is_ground(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_GROUND;
}

static inline bool
dashmodel__is_ground_va(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_GROUND_VA;
}

static inline bool
dashmodel__is_ground_any(const void* m)
{
    uint8_t type = dashmodel__type(m);
    switch( type )
    {
    case DASHMODEL_TYPE_GROUND:
    case DASHMODEL_TYPE_GROUND_VA:
        return true;
    default:
        return false;
    }
}

static inline struct DashVertexArray*
dashmodel__va_array_mut(struct DashModel* m)
{
    assert(dashmodel__is_ground_va(m));
    struct DashModelVAGround* v = (struct DashModelVAGround*)(void*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline const struct DashVertexArray*
dashmodel__va_array_const(const void* m)
{
    assert(dashmodel__is_ground_va(m));
    const struct DashModelVAGround* v = (const struct DashModelVAGround*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline struct DashModelGround*
dashmodel__as_ground(void* m)
{
    assert(dashmodel__is_ground(m));
    return (struct DashModelGround*)m;
}

static inline const struct DashModelGround*
dashmodel__as_ground_const(const void* m)
{
    assert(dashmodel__is_ground(m));
    return (const struct DashModelGround*)m;
}

static inline struct DashModelVAGround*
dashmodel__as_ground_va(void* m)
{
    assert(dashmodel__is_ground_va(m));
    return (struct DashModelVAGround*)m;
}

static inline const struct DashModelVAGround*
dashmodel__as_ground_va_const(const void* m)
{
    assert(dashmodel__is_ground_va(m));
    const struct DashModelVAGround* v = (const struct DashModelVAGround*)m;
    return v;
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
