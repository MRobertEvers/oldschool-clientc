#ifndef TRSPK_VBO_H
#define TRSPK_VBO_H

#include "trspk_flags.h"
#include "trspk_indices.h"
#include "trspk_vertex.h"

// clang-format off
#include "../opengl3/opengl3_vertex.h"
#include "../webgl1/webgl1_vertex.h"
#include "../d3d9/d3d9_vertex.h"
// clang-format on

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_VBO_FLAG_DIRTY TRSPK_FLAG(0)

struct TRSPK_VBO
{
    uint32_t vertex_count;
    uint32_t capacity;
    uint32_t flags;

    enum TRSPK_VertexFormat format;
    union
    {
        struct TRSPK_VertexWebGL1* as_webgl1;
        struct TRSPK_VertexOpenGl3* as_opengl3;
        struct TRSPK_VertexD3D9* as_d3d9;
    } vertices;
};

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format);

void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount);

void
trspk_vbo_ensure_capacity(
    struct TRSPK_VBO* vbo,
    uint32_t min_vertex_count);

void
trspk_vbo_free(struct TRSPK_VBO* vbo);

void
trspk_vbo_reset(struct TRSPK_VBO* vbo);

static inline bool
trspk_vbo_is_dirty(const struct TRSPK_VBO* vbo)
{
    return trspk_flags_test(vbo->flags, TRSPK_VBO_FLAG_DIRTY);
}

static inline void
trspk_vbo_set_dirty(struct TRSPK_VBO* vbo)
{
    trspk_flags_set(&vbo->flags, TRSPK_VBO_FLAG_DIRTY);
}

static inline void
trspk_vbo_clear_dirty(struct TRSPK_VBO* vbo)
{
    trspk_flags_clear(&vbo->flags, TRSPK_VBO_FLAG_DIRTY);
}

static inline void
trspk_vbo_write_vertex_webgl1(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexWebGL1* vertex = &vbo->vertices.as_webgl1[index];

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->position[3] = 1.0f;
    vertex->color[0] = color[0];
    vertex->color[1] = color[1];
    vertex->color[2] = color[2];
    vertex->color[3] = color[3];
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tex_id = tex_id;
    vertex->uv_mode = 0.0f;
    trspk_vbo_set_dirty(vbo);
}

static inline void
trspk_vbo_write_vertex_opengl3(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexOpenGl3* vertex = &vbo->vertices.as_opengl3[index];

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->position[3] = 1.0f;
    vertex->color[0] = color[0];
    vertex->color[1] = color[1];
    vertex->color[2] = color[2];
    vertex->color[3] = color[3];
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tex_id = tex_id;
    vertex->uv_mode = 0.0f;
    trspk_vbo_set_dirty(vbo);
}

/* Pack four float [0,1] RGBA components into a D3D ARGB uint32_t. */
static inline uint32_t
trspk_d3d9_pack_argb(
    float r,
    float g,
    float b,
    float a)
{
    uint32_t ir = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t ig = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t ib = (uint32_t)(b * 255.0f + 0.5f);
    uint32_t ia = (uint32_t)(a * 255.0f + 0.5f);
    if( ir > 255u )
        ir = 255u;
    if( ig > 255u )
        ig = 255u;
    if( ib > 255u )
        ib = 255u;
    if( ia > 255u )
        ia = 255u;
    return (ia << 24u) | (ir << 16u) | (ig << 8u) | ib;
}

static inline void
trspk_vbo_write_vertex_d3d9(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    struct TRSPK_VertexD3D9* vertex = &vbo->vertices.as_d3d9[index];

    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->color = trspk_d3d9_pack_argb(color[0], color[1], color[2], color[3]);
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->texdata[0] = tex_id;
    vertex->texdata[1] = 0.0f;
    trspk_vbo_set_dirty(vbo);
}

#endif