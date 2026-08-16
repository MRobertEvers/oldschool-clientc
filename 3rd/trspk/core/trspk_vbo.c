#include "trspk_vbo.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static size_t
trspk_vbo_vertex_size(enum TRSPK_VertexFormat format)
{
    switch( format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        return sizeof(struct TRSPK_VertexWebGL1);
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        return sizeof(struct TRSPK_VertexOpenGl3);
    case TRSPK_VERTEX_FORMAT_D3D9:
        return sizeof(struct TRSPK_VertexD3D9);
    default:
        assert(false);
        return 0;
    }
}

static void
trspk_vbo_realloc_if_needed(
    struct TRSPK_VBO* vbo,
    uint32_t new_count)
{
    if( new_count <= vbo->capacity )
        return;

    uint32_t new_capacity = vbo->capacity ? vbo->capacity * 2u : new_count;
    if( new_capacity < new_count )
        new_capacity = new_count;

    void* grown = realloc(
        vbo->vertices.as_webgl1, trspk_vbo_vertex_size(vbo->format) * new_capacity);
    assert(grown != NULL);
    vbo->vertices.as_webgl1 = (struct TRSPK_VertexWebGL1*)grown;
    vbo->capacity = new_capacity;
}

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format)
{
    struct TRSPK_VBO* vbo = (struct TRSPK_VBO*)malloc(sizeof(struct TRSPK_VBO));
    assert(vbo != NULL);
    memset(vbo, 0, sizeof(struct TRSPK_VBO));

    vbo->format = format;

    if( index_count_hint > 0 )
        trspk_vbo_growby(vbo, index_count_hint);

    return vbo;
}

void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount)
{
    if( amount == 0 )
    {
        return;
    }

    const uint32_t old_count = vbo->vertex_count;
    const uint32_t new_count = old_count + amount;

    trspk_vbo_realloc_if_needed(vbo, new_count);

    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        memset(&vbo->vertices.as_webgl1[old_count], 0, sizeof(struct TRSPK_VertexWebGL1) * amount);
        break;
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        memset(
            &vbo->vertices.as_opengl3[old_count], 0, sizeof(struct TRSPK_VertexOpenGl3) * amount);
        break;
    case TRSPK_VERTEX_FORMAT_D3D9:
        memset(&vbo->vertices.as_d3d9[old_count], 0, sizeof(struct TRSPK_VertexD3D9) * amount);
        break;
    default:
        assert(false);
        return;
    }

    vbo->vertex_count = new_count;
    trspk_vbo_set_dirty(vbo);
}

void
trspk_vbo_ensure_capacity(
    struct TRSPK_VBO* vbo,
    uint32_t min_vertex_count)
{
    if( vbo->vertex_count >= min_vertex_count )
        return;

    trspk_vbo_growby(vbo, min_vertex_count - vbo->vertex_count);
}

void
trspk_vbo_free(struct TRSPK_VBO* vbo)
{
    if( vbo == NULL )
    {
        return;
    }

    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        free(vbo->vertices.as_webgl1);
        break;
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        free(vbo->vertices.as_opengl3);
        break;
    case TRSPK_VERTEX_FORMAT_D3D9:
        free(vbo->vertices.as_d3d9);
        break;
    default:
        break;
    }

    free(vbo);
}

void
trspk_vbo_reset(struct TRSPK_VBO* vbo)
{
    if( vbo == NULL )
        return;

    vbo->vertex_count = 0u;
    trspk_vbo_set_dirty(vbo);
}
