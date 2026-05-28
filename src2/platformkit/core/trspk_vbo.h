#ifndef TRSPK_VBO_H
#define TRSPK_VBO_H

#include "trspk_indices.h"
#include "trspk_vertex.h"

// clang-format off
#include "../opengl3/opengl3_vertex.h"
#include "../webgl1/webgl1_vertex.h"
// clang-format on

#include <stdint.h>

struct TRSPK_VBO
{
    uint32_t vertex_count;

    enum TRSPK_VertexFormat format;
    union
    {
        struct TRSPK_VertexWebGL1* as_webgl1;
        struct TRSPK_VertexOpenGl3* as_opengl3;
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

static inline void
trspk_vbo_write_vertex_webgl1(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v)
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
    vertex->tex_id = 0.0f;
    vertex->uv_mode = 0.0f;
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
}

#endif