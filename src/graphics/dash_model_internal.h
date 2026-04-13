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
#define DASHMODEL_TYPE_TILE 1u
#define DASHMODEL_TYPE_VA_TILE 2u
#define DASHMODEL_TYPE_FLYWEIGHT 3u
#define DASHMODEL_TYPE_LITE 4u

struct DashModelTile
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
struct DashModelVATile
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

/**
 * Shared, reference-counted, immutable model geometry built once during scene construction.
 * Multiple DashModelLite instances point to the same flyweight when they share the same
 * fully-transformed and lit base mesh (same loc config + transforms + lighting).
 *
 * The flyweight owns all geometry arrays and is freed when refcount drops to 0.
 */
struct DashModelFlyWeight
{
    uint8_t flags;
    int32_t refcount;
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

    /** Original geometry for animation reset; shared by all DashModelLite instances. */
    vertexint_t* original_vertices_x;
    vertexint_t* original_vertices_y;
    vertexint_t* original_vertices_z;
    /** Snapshot for alpha animation reset; shared by DashModelLite instances. */
    alphaint_t* original_face_alphas;

    /** Animation bone mappings. */
    struct DashModelBones* vertex_bones;
    struct DashModelBones* face_bones;

    /** Shared face metadata. */
    alphaint_t* face_alphas;
    int* face_infos;
    uint8_t* face_priorities;
    hsl16_t* face_colors;

    int textured_face_count;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    faceint_t* face_texture_coords;

    /** Shared normals computed once during build. */
    struct DashModelNormals* normals;
    struct DashModelNormals* merged_normals;

    /** Axis-aligned bounds in model space; shared by all DashModelLite instances. */
    struct DashBoundsCylinder* bounds_cylinder;
};

/**
 * Per-instance model that references a shared DashModelFlyWeight.
 * When vertices_x is NULL, the renderer reads vertices from flyweight.
 * Vertex overrides are allocated only when contour-ground adjustment or
 * animation is active on this specific instance.
 */
struct DashModelLite
{
    uint8_t flags;
    int vertex_count;
    int face_count;

    /** Shared base geometry (refcounted; never NULL). */
    struct DashModelFlyWeight* flyweight;

    /** Per-instance vertex overrides (contour ground + per-instance animation).
     *  NULL when vertices are identical to flyweight->vertices_*. */
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;

    /** Per-instance face color overrides (animation alpha changes).
     *  NULL when identical to flyweight->face_colors_*. */
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    alphaint_t* face_alphas;

    /**
     * Per-instance merged vertex normals for sharelight (not shared; base normals live on
     * the flyweight).
     */
    struct DashModelNormals* merged_normals;

    /** True after dashmodel_lite_prepare_sharelight_instance (sharelight merge + late lighting). */
    bool sharelight_instance;
};

static inline size_t
dashmodel__face_priorities_byte_count(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static inline int
dashmodel__get_face_priority(const uint8_t* packed, int index)
{
    uint8_t byte = packed[index >> 1];
    return (index & 1) ? (int)(byte >> 4) : (int)(byte & 0x0Fu);
}

static inline void
dashmodel__set_face_priority(uint8_t* packed, int index, int value)
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
dashmodel__is_tile(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_TILE;
}

static inline bool
dashmodel__is_va_tile(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_VA_TILE;
}

static inline struct DashVertexArray*
dashmodel__va_tile_array_mut(struct DashModel* m)
{
    assert(dashmodel__is_va_tile(m));
    struct DashModelVATile* v = (struct DashModelVATile*)(void*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline const struct DashVertexArray*
dashmodel__va_tile_array_const(const void* m)
{
    assert(dashmodel__is_va_tile(m));
    const struct DashModelVATile* v = (const struct DashModelVATile*)m;
    assert(v->vertex_array != NULL);
    return v->vertex_array;
}

static inline struct DashModelTile*
dashmodel__as_tile(void* m)
{
    assert(dashmodel__is_tile(m));
    return (struct DashModelTile*)m;
}

static inline const struct DashModelTile*
dashmodel__as_tile_const(const void* m)
{
    assert(dashmodel__is_tile(m));
    return (const struct DashModelTile*)m;
}

static inline struct DashModelVATile*
dashmodel__as_va_tile(void* m)
{
    assert(dashmodel__is_va_tile(m));
    return (struct DashModelVATile*)m;
}

static inline const struct DashModelVATile*
dashmodel__as_va_tile_const(const void* m)
{
    assert(dashmodel__is_va_tile(m));
    return (const struct DashModelVATile*)m;
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

static inline bool
dashmodel__is_flyweight(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_FLYWEIGHT;
}

static inline struct DashModelFlyWeight*
dashmodel__as_flyweight(void* m)
{
    assert(dashmodel__is_flyweight(m));
    return (struct DashModelFlyWeight*)m;
}

static inline const struct DashModelFlyWeight*
dashmodel__as_flyweight_const(const void* m)
{
    assert(dashmodel__is_flyweight(m));
    return (const struct DashModelFlyWeight*)m;
}

static inline bool
dashmodel__is_lite(const void* m)
{
    return dashmodel__type(m) == DASHMODEL_TYPE_LITE;
}

static inline struct DashModelLite*
dashmodel__as_lite(void* m)
{
    assert(dashmodel__is_lite(m));
    return (struct DashModelLite*)m;
}

static inline const struct DashModelLite*
dashmodel__as_lite_const(const void* m)
{
    assert(dashmodel__is_lite(m));
    return (const struct DashModelLite*)m;
}

#endif
